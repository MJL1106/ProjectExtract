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
