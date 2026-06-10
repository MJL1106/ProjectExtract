// BT task — sprints at the combat target, fires on the move, melees on contact.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_RusherAdvance.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_RusherAdvance : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_RusherAdvance();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FRusherMemory
	{
		float RePathTimer = 0.f;
	};

	void CleanUp(UBehaviorTreeComponent& OwnerComp) const;
};
