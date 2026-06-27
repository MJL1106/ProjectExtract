// BT task — companion moves to a breachable target and executes Breach().
// Reads BB_CommandTargetActor (set by IssueCommand), validates IBreachable,
// drives MoveToActor, then calls Execute_Breach on arrival.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_CompanionBreach.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_CompanionBreach : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionBreach();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	/** How close the companion must be to the door before executing Breach (cm). */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "10.0"))
	float InteractionRange = 150.f;

	/** Accept radius passed to MoveToActor (cm). Slightly less than InteractionRange so the pawn stops inside it. */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "10.0"))
	float MoveAcceptRadius = 120.f;

	/** If the move-to path completes (companion got as close as the navmesh allows) within this
	 *  distance of the door, breach anyway. Covers free-standing doors / nav gaps where the pawn
	 *  can't reach InteractionRange. Should be >= InteractionRange. (cm) */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "10.0"))
	float ArrivalBreachRange = 300.f;

	/** Seconds the companion holds at the door after breaching, before the task finishes and
	 *  it returns to following the player. (s) */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "0.0"))
	float PostBreachWaitTime = 3.f;

private:
	/** Cached door from the blackboard. Weak — the door may be destroyed mid-task. */
	TWeakObjectPtr<AActor> CachedDoor;

	/** True once MoveToActor has been issued. */
	bool bMoveRequested = false;

	/** True once Breach has been called — waiting for the task to wrap up. */
	bool bBreachTriggered = false;

	/** Time accumulated since the breach fired, while holding at the door. */
	float BreachWaitElapsed = 0.f;

	/** Clean up and fail. */
	void FailAndClear(UBehaviorTreeComponent& OwnerComp);
};
