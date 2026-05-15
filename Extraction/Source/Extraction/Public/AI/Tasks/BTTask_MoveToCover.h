// BT task — queries the cover registry, claims a slot, and moves the companion to it.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_MoveToCover.generated.h"

class AAICoverSlot;

UCLASS()
class EXTRACTION_API UBTTask_MoveToCover : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_MoveToCover();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverLocationKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasCoverPositionKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverSlotKey;

	// Generous — path-follower may stop short of exact slot when slot is near geometry.
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "50.0"))
	float AcceptableRadius = 200.0f;

	// If path-follower stops short of slot but within this distance, accept anyway (CompanionCombat teleport-snaps the gap).
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "100.0"))
	float MaxStoppedShortTolerance = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "100.0"))
	float SearchRadius = 1200.0f;

private:
	void StartMoveTo(AAICoverSlot* Slot, AAIController* Controller, APawn* Pawn);
	void ReleaseClaim(UBlackboardComponent* BB, APawn* Pawn);

	bool bMoveIssued = false;

	UPROPERTY()
	TObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
};
