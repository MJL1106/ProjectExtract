// BT service — validates the combat target and writes LOS + range keys for the enemy combat branch.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_EnemyCombat.generated.h"

UCLASS()
class EXTRACTION_API UBTService_EnemyCombat : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_EnemyCombat();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
