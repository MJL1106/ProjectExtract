// BT task — holds post and faces the suspicion stimulus while Suspicious. Exits only via decorator abort.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyFaceSuspicion.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_EnemyFaceSuspicion : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemyFaceSuspicion();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FFaceSuspicionMemory
	{
		FVector FocalPoint = FVector::ZeroVector;
	};
};
