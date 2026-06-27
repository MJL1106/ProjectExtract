// ATakedownVolume — implementation.

#include "TakedownVolume.h"
#include "Components/BoxComponent.h"
#include "EnemyCharacter.h"

ATakedownVolume::ATakedownVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	OverlapBox = CreateDefaultSubobject<UBoxComponent>(TEXT("OverlapBox"));
	SetRootComponent(OverlapBox);

	OverlapBox->SetBoxExtent(FVector(200.f, 200.f, 100.f));
	OverlapBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	OverlapBox->SetGenerateOverlapEvents(true);
}

void ATakedownVolume::BeginPlay()
{
	Super::BeginPlay();

	OverlapBox->OnComponentBeginOverlap.AddDynamic(this, &ATakedownVolume::OnBoxBeginOverlap);
	OverlapBox->OnComponentEndOverlap.AddDynamic(this, &ATakedownVolume::OnBoxEndOverlap);

	// BeginOverlap does NOT fire for actors already inside the volume at start (designer-placed
	// enemies). Register them explicitly so they're flagged in-volume from the first frame.
	OverlapBox->UpdateOverlaps();
	TArray<AActor*> AlreadyInside;
	OverlapBox->GetOverlappingActors(AlreadyInside, AEnemyCharacter::StaticClass());
	for (AActor* A : AlreadyInside)
	{
		if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(A))
		{
			if (ContainedEnemies.Contains(Enemy)) continue;   // already registered by the overlap delegate
			ContainedEnemies.Add(Enemy);
			Enemy->SetInTakedownVolume(true);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[TakedownVolume] %s BeginPlay: registered %d already-inside enemies"),
		*GetNameSafe(this), AlreadyInside.Num());
}

void ATakedownVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Decrement volume count on all still-contained enemies so they don't stay flagged.
	for (const TWeakObjectPtr<AEnemyCharacter>& WeakEnemy : ContainedEnemies)
	{
		if (WeakEnemy.IsValid())
			WeakEnemy->SetInTakedownVolume(false);
	}
	ContainedEnemies.Empty();

	Super::EndPlay(EndPlayReason);
}

void ATakedownVolume::OnBoxBeginOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(OtherActor);
	if (!IsValid(Enemy)) return;

	ContainedEnemies.Add(Enemy);
	Enemy->SetInTakedownVolume(true);
}

void ATakedownVolume::OnBoxEndOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(OtherActor);
	if (!IsValid(Enemy)) return;

	ContainedEnemies.Remove(Enemy);
	Enemy->SetInTakedownVolume(false);
}

bool ATakedownVolume::ContainsEnemy(const AEnemyCharacter* Enemy) const
{
	if (!Enemy) return false;
	// TSet<TWeakObjectPtr> uses hash/equality on the raw pointer, so construct a temporary for lookup.
	return ContainedEnemies.Contains(TWeakObjectPtr<AEnemyCharacter>(const_cast<AEnemyCharacter*>(Enemy)));
}

void ATakedownVolume::AppendEligibleEnemies(TSet<AEnemyCharacter*>& Out) const
{
	for (const TWeakObjectPtr<AEnemyCharacter>& WeakEnemy : ContainedEnemies)
	{
		if (WeakEnemy.IsValid() && WeakEnemy->IsTakedownEligible())
			Out.Add(WeakEnemy.Get());
	}
}
