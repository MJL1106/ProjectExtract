// AEnemySpawnZone — registration + deterministic spawn-point spread.

#include "EnemySpawnZone.h"
#include "EnemyDirectorSubsystem.h"
#include "EnemyAIController.h" // LogEnemyAI
#include "Components/BoxComponent.h"
#include "Components/BillboardComponent.h"

/** Golden angle (degrees) for sunflower-spiral point distribution. Produces the most uniform
 *  angular spread for any number of samples. */
static constexpr float GoldenAngle = 137.508f;

/** Spawns should not ride the very edge of the box -- nav-projection tends to fail where the
 *  floor polygon ends before the box does. 0.9 keeps points inside the practical walkable area. */
static constexpr float MaxUsableRadiusFraction = 0.9f;

/** Floor on the denominator used for the radius fraction. Prevents small squads (1-3 members)
 *  from dividing by a tiny number and bunching indices at the box edge. 4 spreads a 3-member
 *  squad across the usable area (indices land at radius fractions 0, 0.5, 0.71). */
static constexpr int32 MinSpreadDenominator = 4;

AEnemySpawnZone::AEnemySpawnZone()
{
	PrimaryActorTick.bCanEverTick = false;

	ZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ZoneBox"));
	SetRootComponent(ZoneBox);
	ZoneBox->SetBoxExtent(FVector(400.f, 400.f, 200.f));
	ZoneBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneBox->SetGenerateOverlapEvents(false);
	ZoneBox->ShapeColor = FColor::Cyan;

#if WITH_EDITORONLY_DATA
	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	Billboard->SetupAttachment(RootComponent);
	Billboard->bIsScreenSizeScaled = true;
	Billboard->SetHiddenInGame(true);
#endif
}

void AEnemySpawnZone::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* W = GetWorld())
	{
		if (UEnemyDirectorSubsystem* Director = W->GetSubsystem<UEnemyDirectorSubsystem>())
			Director->RegisterSpawnZone(this);
	}
}

void AEnemySpawnZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* W = GetWorld())
	{
		if (UEnemyDirectorSubsystem* Director = W->GetSubsystem<UEnemyDirectorSubsystem>())
			Director->UnregisterSpawnZone(this);
	}

	Super::EndPlay(EndPlayReason);
}

bool AEnemySpawnZone::IsActiveForPhase(EMissionPhase Phase) const
{
	if (ActivePhases.IsEmpty()) return true;
	return ActivePhases.Contains(Phase);
}

FTransform AEnemySpawnZone::GetSpawnTransform(int32 Index, int32 SquadSize) const
{
	const FVector Extent = ZoneBox->GetScaledBoxExtent();
	const FVector Origin = ZoneBox->GetComponentLocation();
	const float FloorZ = Origin.Z - Extent.Z;
	const float MinSep = FMath::Max(MinSpawnSeparation, 0.f);

	// Minimum radius so that adjacent golden-angle points are MinSep apart. Chord-based:
	// chord = 2*r*sin(angle/2), so r >= MinSep / (2*sin(GoldenAngleRad/2)).
	const float GoldenAngleRad = FMath::DegreesToRadians(GoldenAngle);
	const float ChordDenom = 2.f * FMath::Sin(GoldenAngleRad * 0.5f);
	const float MinRadiusForSep = (ChordDenom > 0.f) ? (MinSep / ChordDenom) : 0.f;

	// The constraining axis governs whether the box can fit the squad. Consistent with
	// the MinFrac floor in ComputeSpiralPoint so the warning and the guarantee agree.
	const float ConstrainingExtent = FMath::Min(Extent.X, Extent.Y);
	const float UsableRadius = ConstrainingExtent * MaxUsableRadiusFraction;

	if (!bUndersizedWarningLogged && MinSep > 0.f && SquadSize > 1 && MinRadiusForSep > UsableRadius)
	{
		bUndersizedWarningLogged = true;
		UE_LOG(LogEnemyAI, Warning,
			TEXT("EnemySpawnZone '%s': box too small to separate %d members at %.0f cm (usable %.0f, needed %.0f)"),
			*GetName(), SquadSize, MinSep, UsableRadius, MinRadiusForSep);
	}

	if (Index == 0)
	{
		if (MinSep > 0.f && SquadSize > 1)
		{
			const float PushRadius = FMath::Min(MinRadiusForSep, UsableRadius);
			const FVector Offset = GetActorForwardVector() * PushRadius;
			return FTransform(GetActorRotation(), FVector(Origin.X + Offset.X, Origin.Y + Offset.Y, FloorZ));
		}
		return FTransform(GetActorRotation(), FVector(Origin.X, Origin.Y, FloorZ));
	}

	return ComputeSpiralPoint(Index, SquadSize, Extent, Origin, FloorZ, MinRadiusForSep, ConstrainingExtent);
}

FTransform AEnemySpawnZone::ComputeSpiralPoint(int32 Index, int32 SquadSize, const FVector& Extent,
	const FVector& Origin, float FloorZ, float MinRadiusForSep, float ConstrainingExtent) const
{
	const int32 Denominator = FMath::Max(SquadSize, MinSpreadDenominator);

	const float AngleDeg = FMath::Fmod(static_cast<float>(Index) * GoldenAngle, 360.f);
	float RadiusFrac = FMath::Sqrt(static_cast<float>(Index) / static_cast<float>(Denominator));
	float ClampedFrac = FMath::Min(RadiusFrac, MaxUsableRadiusFraction);

	// Floor the radius fraction so the constraining axis meets MinSep.
	if (MinRadiusForSep > 0.f && ConstrainingExtent > 0.f)
	{
		const float MinFrac = MinRadiusForSep / ConstrainingExtent;
		ClampedFrac = FMath::Clamp(FMath::Max(ClampedFrac, MinFrac), 0.f, MaxUsableRadiusFraction);
	}

	const float Rad = FMath::DegreesToRadians(AngleDeg);
	const float LocalX = FMath::Cos(Rad) * Extent.X * ClampedFrac;
	const float LocalY = FMath::Sin(Rad) * Extent.Y * ClampedFrac;

	FVector WorldPoint = Origin + GetActorForwardVector() * LocalX + GetActorRightVector() * LocalY;
	WorldPoint.Z = FloorZ;

	const float YawDeg = FMath::Fmod(AngleDeg + 180.f, 360.f);
	return FTransform(FRotator(0.f, YawDeg, 0.f), WorldPoint);
}

FVector AEnemySpawnZone::GetZoneOrigin() const
{
	return ZoneBox->GetComponentLocation();
}

FVector AEnemySpawnZone::GetClosestPointInZone(const FVector& WorldPoint) const
{
	const FTransform& BoxTM = ZoneBox->GetComponentTransform();
	const FVector Extent = ZoneBox->GetUnscaledBoxExtent();

	FVector Local = BoxTM.InverseTransformPosition(WorldPoint);
	Local.X = FMath::Clamp(Local.X, -Extent.X, Extent.X);
	Local.Y = FMath::Clamp(Local.Y, -Extent.Y, Extent.Y);
	Local.Z = FMath::Clamp(Local.Z, -Extent.Z, Extent.Z);

	return BoxTM.TransformPosition(Local);
}
