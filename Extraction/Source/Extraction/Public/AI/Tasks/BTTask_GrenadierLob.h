// BT task — accumulates LOS-blocked time and triggers a grenade lob when threshold is met.
// Returns Failed immediately when not triggering so the subtree falls through to normal fire.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_GrenadierLob.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_GrenadierLob : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_GrenadierLob();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FGrenadierLobMemory
	{
		float LOSBlockedAccum = 0.f;
		bool bWaitingForThrow = false;
	};
};
