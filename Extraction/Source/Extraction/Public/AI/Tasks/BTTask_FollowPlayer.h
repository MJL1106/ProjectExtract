// BT task — companion follows player in formation or sprints to reach them.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Companion/CompanionTypes.h"
#include "BTTask_FollowPlayer.generated.h"

class UCompanionTuningDataAsset;
class ACompanionAIController;
class ACompanionCharacter;
class UEnvQuery;
struct FEnvQueryResult;

UCLASS()
class EXTRACTION_API UBTTask_FollowPlayer : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_FollowPlayer();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

	/** Always sprint (used for revive branch) */
	UPROPERTY(EditAnywhere, Category = "Follow")
	bool bSprintToTarget = false;

	/** Optional EQS query for picking a follow slot near the player. If unset, falls back to formation-offset target. */
	UPROPERTY(EditAnywhere, Category = "EQS")
	TObjectPtr<UEnvQuery> FollowSlotQuery;

	/** Minimum seconds between EQS follow-slot requests. */
	UPROPERTY(EditAnywhere, Category = "EQS", meta = (ClampMin = "0.1"))
	float EqsQueryInterval = 0.5f;

private:
	FVector LastMoveTarget = FVector::ZeroVector;
	bool bIsIdling = false;

	/** ~1Hz throttle for the sprint-mode rescue-approach diagnostic log. */
	float SprintLogAccumulator = 0.f;

	/** Accumulates DeltaSeconds in the formation branch; gates the blocked-move re-issue below so a
	 *  normal brief Idle blip (arrival, replan) doesn't spam repath. */
	float BlockedMoveReissueAccumulator = 0.f;

	/** Minimum seconds between forced LastMoveTarget resets when the path-following component
	 *  reports Idle while the companion is still outside the formation's acceptance radius (e.g.
	 *  the player is standing exactly on the anchor point, so MoveToLocation can't path there). */
	static constexpr float BlockedMoveReissueCooldown = 0.5f;

	/** Mode seen last tick — a change drops the idle latch so the new formation applies immediately
	 *  (e.g. switching to Combat while stationary sends the companion to the lead point). */
	ECompanionMode LastSeenMode = ECompanionMode::Normal;

	// EQS slot state
	bool bEqsQueryInProgress = false;
	bool bHasEqsTarget = false;
	float TimeSinceLastEqs = 0.f;
	FVector EqsTarget = FVector::ZeroVector;

	void OnFollowQueryFinished(TSharedPtr<FEnvQueryResult> Result);

	UPROPERTY()
	TObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;

	// Cached at ExecuteTask, cleared at OnTaskFinished. bCreateNodeInstance = true keeps
	// per-instance state safe; the casts in TickTask were ~120 Hz of churn otherwise.
	TWeakObjectPtr<ACompanionAIController> CachedController;
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;
};
