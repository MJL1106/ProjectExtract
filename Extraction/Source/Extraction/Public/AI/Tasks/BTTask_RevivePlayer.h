// BT task — companion sprints to downed player and revives them after a hold timer.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_RevivePlayer.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_RevivePlayer : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_RevivePlayer();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

private:
	float ReviveElapsed = 0.0f;
	bool bIsHoldingRevive = false;
};
