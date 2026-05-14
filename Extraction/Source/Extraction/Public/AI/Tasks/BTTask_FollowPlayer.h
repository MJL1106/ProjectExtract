// BT task — companion follows player in formation or sprints to reach them.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_FollowPlayer.generated.h"

class UCompanionTuningDataAsset;
class ACompanionAIController;
class ACompanionCharacter;

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

private:
	FVector LastMoveTarget = FVector::ZeroVector;
	bool bIsIdling = false;

	// Cached at ExecuteTask, cleared at OnTaskFinished. bCreateNodeInstance = true keeps
	// per-instance state safe; the casts in TickTask were ~120 Hz of churn otherwise.
	TWeakObjectPtr<ACompanionAIController> CachedController;
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;
};
