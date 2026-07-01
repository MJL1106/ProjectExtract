// AEnemyDirectorScopeVolume - registers an optional director scope with the world subsystem.

#include "EnemyDirectorScopeVolume.h"
#include "EnemyDirectorSubsystem.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"

AEnemyDirectorScopeVolume::AEnemyDirectorScopeVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	ScopeBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ScopeBox"));
	SetRootComponent(ScopeBox);
	ScopeBox->SetBoxExtent(FVector(4000.f, 2000.f, 500.f));
	ScopeBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ScopeBox->SetGenerateOverlapEvents(false);
	ScopeBox->ShapeColor = FColor(255, 160, 0);

#if WITH_EDITORONLY_DATA
	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	Billboard->SetupAttachment(RootComponent);
	Billboard->bIsScreenSizeScaled = true;
	Billboard->SetHiddenInGame(true);
#endif
}

void AEnemyDirectorScopeVolume::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
		{
			Director->RegisterScopeVolume(this);
		}
	}
}

void AEnemyDirectorScopeVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
		{
			Director->UnregisterScopeVolume(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

FVector AEnemyDirectorScopeVolume::GetScopeOrigin() const
{
	return IsValid(ScopeBox) ? ScopeBox->GetComponentLocation() : GetActorLocation();
}

bool AEnemyDirectorScopeVolume::ContainsActor(const AActor* Actor) const
{
	return IsValid(Actor) && ContainsPoint(Actor->GetActorLocation());
}

bool AEnemyDirectorScopeVolume::ContainsPoint(const FVector& Point) const
{
	if (!bEnabled || !IsValid(ScopeBox)) return false;

	const FVector LocalPoint = ScopeBox->GetComponentTransform().InverseTransformPosition(Point);
	const FVector Extent = ScopeBox->GetUnscaledBoxExtent();

	return FMath::Abs(LocalPoint.X) <= Extent.X
		&& FMath::Abs(LocalPoint.Y) <= Extent.Y
		&& FMath::Abs(LocalPoint.Z) <= Extent.Z;
}
