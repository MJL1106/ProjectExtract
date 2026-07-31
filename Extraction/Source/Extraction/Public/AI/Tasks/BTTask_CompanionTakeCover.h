// BT task (instant) -- resolves the commanded cover target from the companion and writes it to
// the CoverTarget blackboard key. The existing BTTask_MoveToCoverPoint does the walk/arrival/claim.
// Never latent, never calls FinishLatentTask(Aborted).

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_CompanionTakeCover.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_CompanionTakeCover : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionTakeCover();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

protected:
	/** BB key holding the Cover type (FCover via AICS plugin). Must match BTTask_MoveToCoverPoint's key. */
	UPROPERTY(EditAnywhere, Category = "Cover")
	FBlackboardKeySelector CoverTargetKey;

	/** Leash radius (cm) around the commanded cover anchor: the player must stay within this
	 *  distance or the companion releases the hold and resumes following. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "500.0"))
	float LeashRadius = 2500.f;
};
