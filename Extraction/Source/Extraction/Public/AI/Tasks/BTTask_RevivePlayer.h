// BT task — companion sprints to downed player and revives them after a hold timer.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_RevivePlayer.generated.h"

class ACompanionCharacter;

UCLASS()
class EXTRACTION_API UBTTask_RevivePlayer : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_RevivePlayer();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

private:
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;
	float ReviveElapsed = 0.0f;
	bool bIsHoldingRevive = false;
};
