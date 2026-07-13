// BT service — periodically re-evaluates whether the companion's current cover point
// is still the best available, and commits a switch by writing the CoverTarget BB key.
// P3 AICS migration: cover source changed from AAICoverSlot line-segment slots to FCoverHandle/FCoverData points.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "CoverSystemPublicData.h"
#include "BTService_CoverSwitchMonitor.generated.h"

struct FCoverSwitchMonitorMemory
{
	float TimeSinceArrival  = 0.f;
	float TimeSinceReEval   = 0.f;
	bool  bWasInCoverLastTick = false;

	// Dwell-from-arrival: only accrue TimeSinceArrival once physically at the cover point (fixes claim-time start).
	// LastArrivalCover is the point the latch was earned at — a task-internal shuffle or monitor commit
	// swaps the BB cover without a HasCover edge, and the latch must re-earn at the new point.
	bool  bHasArrived = false;
	FCoverHandle LastArrivalCover;

	// G2 debounce — a candidate must win two consecutive re-evals before the switch commits.
	FCoverHandle PendingBestCover;
	int32 ConsecutiveBetterCount = 0;

	// Blind-current persistence — the G5 bypass + score penalty need 2 consecutive blind re-evals
	// AT THE SAME cover against the SAME target. Task-internal shuffles swap the BB cover without
	// any Mem reset firing, and a target switch is likewise invisible — either would let one stale
	// count + one fresh blind read trip the bypass on the first re-eval (the single-sample flip-flop
	// this counter damps).
	int32 ConsecutiveBlindReEvals = 0;
	FCoverHandle LastBlindEvalCover;
	TWeakObjectPtr<AActor> LastBlindEvalTarget;

	// Player-advance tracking for the Normal/Stealth advance gate: accumulated PLAYER displacement
	// projected toward the current threat, decayed per re-eval so stationary play bleeds back to
	// zero. Displacement-based, not range-based — an enemy rushing a stationary player must not
	// open the gate.
	FVector LastPlayerAdvanceLoc = FVector::ZeroVector;
	bool bHasPlayerAdvanceSample = false;
	float PlayerAdvanceProgress = 0.f;

	// Compromise break (enemy flank-break parity): fast-cadence eval timer + consecutive positive
	// evals. Two agreeing evals confirm a geometric flank; an exposed-side HIT confirms instantly.
	// Identity guard mirrors the blind-eval one — a task-internal cover shuffle or target switch
	// must not let a stale count confirm on the first eval at the new point.
	float CompromiseEvalTimer = 0.f;
	int32 CompromiseConsecutiveCount = 0;
	FCoverHandle LastCompromiseEvalCover;
	TWeakObjectPtr<AActor> LastCompromiseEvalTarget;
};

UCLASS()
class EXTRACTION_API UBTService_CoverSwitchMonitor : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_CoverSwitchMonitor();

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FCoverSwitchMonitorMemory); }
	virtual FString GetStaticDescription() const override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	// Node memory holds a TWeakObjectPtr — placement-construct/destruct instead of relying on zero-init.
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasCoverPositionKey;

	/** BB key holding the Cover type (FCover via AICS plugin). Default name "CoverTarget". */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverTargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverLocationKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

	// Search radius for candidate covers. Matches BTTask_MoveToCoverPoint's DA-driven default.
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "100.0"))
	float SearchRadius = 1200.f;

	// Pawn must be within this 2D distance of the cover point's hunker position before dwell accrues
	// (matches the arrival point BTTask_MoveToCoverPoint moves the pawn to).
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "1.0"))
	float ArrivalRadius = 100.f;

};
