// BT task (latent) -- owns out-of-combat commanded-cover state. Enters the cover pose via
// CompanionAnimInstance::EnterCoverPose (the same path combat uses), re-asserts crouch and
// fire-arc facing each tick, and finishes when the hold releases or a combat target appears.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Companion/CompanionTypes.h"
#include "BTTask_CompanionHoldCover.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_CompanionHoldCover : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionHoldCover();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

protected:
	/** BB key holding the Cover type (FCover via AICS plugin). Must match the rest of the branch. */
	UPROPERTY(EditAnywhere, Category = "Cover")
	FBlackboardKeySelector CoverTargetKey;

	/** BB key holding the combat target actor. When valid, the task finishes so the combat
	 *  branch can run. Resolved from the BT asset, not read by name. */
	UPROPERTY(EditAnywhere, Category = "Cover")
	FBlackboardKeySelector CombatTargetKey;

private:
	/** True once EnterCoverPose has been called -- avoids replaying the enter montage every tick. */
	bool bCoverPoseEntered = false;

	/** Side resolved at entry from the cover's baked flags. */
	EPeekSide ResolvedSide = EPeekSide::Left;
};
