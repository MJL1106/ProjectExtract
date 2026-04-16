// BT task — runs EQS query for cover, moves companion to best position.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "BTTask_MoveToCover.generated.h"

class UEnvQuery;

UCLASS()
class EXTRACTION_API UBTTask_MoveToCover : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_MoveToCover();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverLocationKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasCoverPositionKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	/** EQS query asset for finding cover */
	UPROPERTY(EditAnywhere, Category = "Cover")
	TObjectPtr<UEnvQuery> CoverQuery;

	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "50.0"))
	float AcceptableRadius = 100.0f;

private:
	void OnQueryFinished(TSharedPtr<FEnvQueryResult> Result);

	bool bQueryInProgress = false;

	UPROPERTY()
	TObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;
};
