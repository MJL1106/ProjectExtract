// BT service — periodically re-evaluates whether the companion's current cover slot
// is still the best available, and clears BB_HasCoverPosition to trigger a switch.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_CoverSwitchMonitor.generated.h"

struct FCoverSwitchMonitorMemory
{
	float TimeSinceArrival  = 0.f;
	float TimeSinceReEval   = 0.f;
	bool  bWasInCoverLastTick = false;
};

UCLASS()
class EXTRACTION_API UBTService_CoverSwitchMonitor : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_CoverSwitchMonitor();

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FCoverSwitchMonitorMemory); }
	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasCoverPositionKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverSlotKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector NextCoverSlotKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

	// Search radius passed to FindBestCoverFor. Matches BTTask_MoveToCover default.
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "100.0"))
	float SearchRadius = 1200.f;

};
