// BT task — instant melee strike if target is within MeleeRange; returns Failed if out of range.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyMelee.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_EnemyMelee : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemyMelee();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
};
