// BT task — companion approaches the player's traversed obstacle and plays its own traversal.
// Triggered by ACompanionAIController when the player's UTraversalComponent broadcasts OnTraversalStarted.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_MirrorPlayerTraversal.generated.h"

class UTraversalComponent;
class UBehaviorTreeComponent;

/** Per-instance memory for the latent mirror task. Allocated by BT framework via GetInstanceMemorySize(). */
struct FMirrorMemory
{
	enum class EPhase : uint8
	{
		Approach,
		WaitForTraversalEnd,
	};

	EPhase Phase = EPhase::Approach;
	float Elapsed = 0.f;
	float DebugLogAccumulator = 0.f;
	FVector ApproachPoint = FVector::ZeroVector;
	TWeakObjectPtr<UTraversalComponent> OwnTraversalComp;
	FDelegateHandle EndedHandle;
};

UCLASS()
class EXTRACTION_API UBTTask_MirrorPlayerTraversal : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_MirrorPlayerTraversal();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FMirrorMemory); }
	virtual FString GetStaticDescription() const override;
};
