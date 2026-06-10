// AEnemySpawnZone — registration + deterministic spawn-point spread.

#include "EnemySpawnZone.h"
#include "EnemyDirectorSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/BillboardComponent.h"

static constexpr float GoldenAngle = 137.508f;

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

FTransform AEnemySpawnZone::GetSpawnTransform(int32 Index) const
{
	const FVector Extent = ZoneBox->GetScaledBoxExtent();
	const FVector Origin = ZoneBox->GetComponentLocation();

	if (Index == 0)
	{
		FVector Loc = Origin;
		Loc.Z = Origin.Z - Extent.Z;
		return FTransform(GetActorRotation(), Loc);
	}

	const float AngleDeg = FMath::Fmod(static_cast<float>(Index) * GoldenAngle, 360.f);
	const float RadiusFrac = FMath::Sqrt(static_cast<float>(Index) / 20.f);
	const float ClampedFrac = FMath::Min(RadiusFrac, 0.9f);

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
