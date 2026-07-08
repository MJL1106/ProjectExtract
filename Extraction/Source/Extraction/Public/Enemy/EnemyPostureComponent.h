// UEnemyPostureComponent — per-enemy combat posture (Hold / Press / FallBack).
// Makes enemies trade ground: Press toward a passive threat, fall back when broken.
// Deliberately per-enemy and unsynchronised — a random timer phase staggers advances
// naturally; squad-level coordination stays the officer's job.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyPostureComponent.generated.h"

class AEnemyCharacter;
class UEnemyArchetypeData;

UENUM(BlueprintType)
enum class EEnemyPosture : uint8
{
	Hold,
	Press,
	FallBack
};

UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UEnemyPostureComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyPostureComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Enemy|Posture")
	EEnemyPosture GetPosture() const { return CurrentPosture; }

	/** True when Press has held long enough to warrant a proactive advance to closer cover.
	 *  One-shot: consuming clears the latch. The consumer stamps the cooldown via NotifyAdvanceExecuted
	 *  only when the advance actually commits, so a rejected candidate doesn't burn the cooldown. */
	bool ConsumeAdvanceRequest();

	/** Called by the fire task when an advance relocate actually commits. */
	void NotifyAdvanceExecuted();

	/** True when a DBNO back-off wants a retreat relocate to deeper cover. Consumers must check
	 *  this (and validate their cover) BEFORE ConsumeRetreatRequest, so a failed setup doesn't
	 *  burn the one-shot. */
	bool HasRetreatRequest() const { return bRetreatRequested; }

	/** One-shot: consuming clears the latch and stamps a repeat cooldown, so a retreat that leaves
	 *  the enemy inside the standoff ring re-requests after RetreatRepeatCooldown instead of spamming. */
	bool ConsumeRetreatRequest();

	/** Stops evaluation permanently (pawn died). */
	void DeactivateForDeath();

private:
	void EvaluatePosture();
	float ComputeAggression01(const AEnemyCharacter* Enemy, const UEnemyArchetypeData* DA,
		const AActor* Target) const;

	/** Downed-player standoff: returns true (and requests a one-shot retreat when inside the ring)
	 *  if a hostile player is DBNO — posture is fully overridden while this holds. */
	bool ApplyDBNOStandoffOverride(const AEnemyCharacter* Enemy, const UEnemyArchetypeData* DA);

	EEnemyPosture CurrentPosture = EEnemyPosture::Hold;
	float PressHeldSeconds = 0.f;
	bool bAdvanceRequested = false;
	float LastAdvanceWorldTime = -1e9f;
	bool bStopped = false;

	bool bRetreatRequested = false;
	float LastRetreatConsumeWorldTime = -1e9f;

	FTimerHandle EvalTimerHandle;
};
