// ATakedownVolume — hand-placed overlap box that marks contained enemies as eligible
// for a designer-authored takedown. Scales in the viewport; no Tick; no replication.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TakedownVolume.generated.h"

class UBoxComponent;
class AEnemyCharacter;

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking))
class EXTRACTION_API ATakedownVolume : public AActor
{
	GENERATED_BODY()

public:
	ATakedownVolume();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Takedown|Volume", meta = (AllowPrivateAccess))
	TObjectPtr<UBoxComponent> OverlapBox;

	/** Enemies currently overlapping this volume. Weak refs — enemies may die without leaving. */
	TSet<TWeakObjectPtr<AEnemyCharacter>> ContainedEnemies;

	UFUNCTION()
	void OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnBoxEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
