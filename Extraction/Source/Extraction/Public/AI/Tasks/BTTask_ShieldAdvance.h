// BT task — shield carrier advances toward target with periodic burst stops; fails when shield breaks.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ShieldAdvance.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_ShieldAdvance : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ShieldAdvance();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	/** Seconds between the periodic stop-and-burst cycles while advancing. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Shield", meta = (ClampMin = "0.5"))
	float BurstCycleInterval = 2.5f;

	/** Duration of the brief burst when pausing the advance. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Shield", meta = (ClampMin = "0.1"))
	float BurstDuration = 0.6f;

	struct FShieldAdvanceMemory
	{
		float BurstCycleTimer = 0.f;
		float BurstActiveTimer = 0.f;
		bool bBursting = false;
		float OriginalMaxWalkSpeed = 0.f;
	};

	void CleanUp(UBehaviorTreeComponent& OwnerComp, FShieldAdvanceMemory* Mem) const;
};
