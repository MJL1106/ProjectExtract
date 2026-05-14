// BT task — cover-aware companion combat. State machine drives EngageFromOpen, EngageFromCover, StandUpFire.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Companion/CompanionTypes.h"
#include "BTTask_CompanionCombat.generated.h"

class UAnimMontage;

UCLASS()
class EXTRACTION_API UBTTask_CompanionCombat : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionCombat();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasCoverPositionKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverLocationKey;

	/** Seconds of continuous fire per burst */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.1"))
	float FireBurstDuration = 1.5f;

	/** Seconds of pause between bursts */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.1"))
	float FirePauseDuration = 0.5f;

	/** Minimum dwell in cover-idle before the first peek can fire. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float MinCoverIdleDwell = 0.4f;

	/** Min peek cooldown (randomised per cycle). */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float MinPeekCooldown = 0.6f;

	/** Max peek cooldown (randomised per cycle). */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float MaxPeekCooldown = 1.4f;

	/** Interval (seconds) between cover-validity LOS rechecks while in EngageFromCover. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.25", ClampMax = "5.0"))
	float CoverValidityCheckInterval = 1.0f;

	/** Minimum time at current cover before a failed re-eval can abandon the slot (prevents thrashing). */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MinCoverDwellBeforeReEval = 2.0f;

	UPROPERTY(EditAnywhere, Category="Cover", meta=(ClampMin="0.0", ClampMax="200.0",
		ToolTip="Z offset above target actor origin for the cover-validity LoS trace. ~60 for humanoids."))
	float TargetEyeHeightOffset = 60.f;

	/** Toggle verbose exit-gate logs + debug draw under LogCompanionAI. Enable per-instance in BT. */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDebugLogging = false;

private:
	float BurstTimer = 0.0f;
	bool bIsFiringBurst = false;

	float TimeInCoverIdle = 0.f;
	float PeekCooldown = 0.f;
	EPeekSide ResolvedPeekSide = EPeekSide::Right;

	/** Periodic cover-validity LOS accumulator (only ticks during EngageFromCover). */
	float CoverValidityCheckTimer = 0.f;

	/** How long companion has been at the current cover slot (only ticks during EngageFromCover). */
	float TimeAtCurrentCover = 0.f;

	/** Cached cover + target locations used for last peek-side resolution. Avoids per-tick recompute. */
	FVector LastPeekResolveCoverLoc = FVector::ZeroVector;
	FVector LastPeekResolveTargetLoc = FVector::ZeroVector;
	static constexpr float PeekResolveDistThresholdSq = 50.f * 50.f;
};
