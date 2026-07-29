// AEnemySpawnZone — registration + deterministic spawn-point spread.

#include "EnemySpawnZone.h"
#include "EnemyDirectorSubsystem.h"
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

	if (Index == 0)
	{
		FVector Loc = Origin;
		Loc.Z = Origin.Z - Extent.Z;
		return FTransform(GetActorRotation(), Loc);
	}

	// Scale the radius fraction against SquadSize so a squad of N spreads across the full usable
	// box. Both call sites pass a strictly positive size; the floor handles defensive callers.
	const int32 Denominator = FMath::Max(SquadSize, MinSpreadDenominator);

	const float AngleDeg = FMath::Fmod(static_cast<float>(Index) * GoldenAngle, 360.f);
	const float RadiusFrac = FMath::Sqrt(static_cast<float>(Index) / static_cast<float>(Denominator));
	const float ClampedFrac = FMath::Min(RadiusFrac, MaxUsableRadiusFraction);

	const float Rad = FMath::DegreesToRadians(AngleDeg);
	const float LocalX = FMath::Cos(Rad) * Extent.X * ClampedFrac;
	const float LocalY = FMath::Sin(Rad) * Extent.Y * ClampedFrac;

	FVector WorldPoint = Origin + GetActorForwardVector() * LocalX + GetActorRightVector() * LocalY;
	WorldPoint.Z = Origin.Z - Extent.Z;

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
