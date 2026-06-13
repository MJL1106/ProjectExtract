// BT task — picks a perch, telegraphs aim, fires a single shot, and relocates when exposed.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_SniperNest.generated.h"

UENUM()
enum class ESniperNestPhase : uint8
{
	Evaluate,
	MovingToPerch,
	Telegraph,
	ReadyToFire,
};

UCLASS()
class EXTRACTION_API UBTTask_SniperNest : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_SniperNest();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FSniperNestMemory
	{
		ESniperNestPhase Phase = ESniperNestPhase::Evaluate;
		bool bSlotClaimed = false;
	};

	bool NeedsRelocation(UBehaviorTreeComponent& OwnerComp, const uint8* NodeMemory) const;
	void CleanUp(UBehaviorTreeComponent& OwnerComp, FSniperNestMemory* Mem) const;
	void ReleaseSlot(UBehaviorTreeComponent& OwnerComp) const;
};
