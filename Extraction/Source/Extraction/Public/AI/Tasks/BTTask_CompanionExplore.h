// BT task — companion commanded explore: walk to a pinged spot (BB_CommandTargetLocation), stamp
// the unaware-enemy engagement grant there, then chain a loot sweep if the area is quiet and has
// a container, else hand straight back to follow. No target actor — location command.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_CompanionExplore.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_CompanionExplore : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionExplore();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	/** Accept radius for the move to the explore point (cm). */
	UPROPERTY(EditAnywhere, Category = "Explore", meta = (ClampMin = "10.0"))
	float ArriveAcceptRadius = 100.f;

	/** Max seconds the walk to the explore point may take before finishing where it stands. */
	UPROPERTY(EditAnywhere, Category = "Explore", meta = (ClampMin = "1.0"))
	float MoveTimeout = 20.f;

private:
	FVector ExploreLocation = FVector::ZeroVector;
	float MoveElapsed = 0.f;

	/** Stop movement + clear the command (guarded: only if the active command is still Explore —
	 *  an abort caused by a fresh replacement command must not wipe the command it was replaced by). */
	void ClearIfStillExplore(UBehaviorTreeComponent& OwnerComp);

	/** Arrived (or timed out): stamp the engagement grant, chain loot if the area is quiet and has
	 *  a container, else clear. Always finishes the task with Succeeded. */
	void FinishExplore(UBehaviorTreeComponent& OwnerComp, class ACompanionAIController* AIC, APawn* Pawn);
};
