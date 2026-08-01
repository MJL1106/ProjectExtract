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

	/** Returns true if Enemy is currently registered inside this volume. */
	bool ContainsEnemy(const AEnemyCharacter* Enemy) const;

	/** Appends all valid, takedown-eligible enemies inside this volume to Out.
	 *  Used by the companion BT task to union eligible sets across overlapping volumes:
	 *  paired targets are placed in one volume by design, so the union per-shared-volume
	 *  drives the lone-vs-sync takedown decision. */
	void AppendEligibleEnemies(TSet<AEnemyCharacter*>& Out) const;

	/** Unions the takedown-eligible enemies of every volume that contains Enemy into Out. This is the
	 *  one definition of "who is in this pocket": the companion BT task uses it to decide synced-vs-
	 *  autonomous, and ACompanionCharacter uses it to pick who the takedown window hushes. Kept as one
	 *  shared helper on purpose — two copies of the union would let the pair the task syncs on and the
	 *  pair the window covers drift apart, and the partner left out is the one that turns around. */
	static void GatherEligiblePeers(const AEnemyCharacter* Enemy, TSet<AEnemyCharacter*>& Out);

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
