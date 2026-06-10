// BT task — anchors the heavy in place and drives sustained burst fire at target or last-known location.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_HeavySuppress.generated.h"

UENUM()
enum class EHeavySuppressPhase : uint8
{
	Fire,
	Pause,
};

UCLASS()
class EXTRACTION_API UBTTask_HeavySuppress : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_HeavySuppress();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FHeavySuppressMemory
	{
		EHeavySuppressPhase Phase = EHeavySuppressPhase::Fire;
		float PhaseTimer = 0.f;
		bool bAimOverrideActive = false;
	};

	void CleanUp(UBehaviorTreeComponent& OwnerComp, FHeavySuppressMemory* Mem) const;
};
