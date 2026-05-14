// BT task — companion follows player in formation or sprints to reach them.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
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
