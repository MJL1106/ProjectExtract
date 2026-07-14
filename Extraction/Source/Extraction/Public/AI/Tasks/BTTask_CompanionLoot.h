// BT task — commanded loot sweep. The companion moves to the pinged container, plays its loot
// montage and loots it, then automatically chains to the nearest remaining lootable container
// within SweepRadius of the ORIGINAL ping (the sweep is anchored to the pinged spot, so the
// companion clears "this room" and never wanders across the map). Finishes when nothing lootable
// remains in range, or MaxContainersPerSweep is hit. Mirrors BTTask_CompanionBreach's shape.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_CompanionLoot.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_CompanionLoot : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionLoot();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	/** How close the companion must be to a container before looting it (cm). */
	UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "10.0"))
	float InteractionRange = 150.f;

	/** Accept radius passed to MoveToActor (cm). Slightly less than InteractionRange. */
	UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "10.0"))
	float MoveAcceptRadius = 120.f;

	/** If the move path completes within this distance of the container, loot anyway
	 *  (nav gaps / freestanding props). Should be >= InteractionRange. (cm) */
	UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "10.0"))
	float ArrivalLootRange = 300.f;

	/** Pause at each container after looting before moving to the next — sells the search beat
	 *  and roughly covers the loot montage. (s) */
	UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "0.0"))
	float InterLootPause = 2.0f;

	/** Containers within this radius of the ORIGINAL pinged container are swept automatically. (cm) */
	UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "0.0"))
	float SweepRadius = 1200.f;

	/** Safety cap on containers looted per sweep. 0 = unlimited (radius is the real bound). */
	UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "0"))
	int32 MaxContainersPerSweep = 0;

private:
	/** Container currently being approached/looted. Weak — may be destroyed mid-task. */
	TWeakObjectPtr<AActor> CurrentTarget;

	/** The pinged container the command was issued for. Compared against BB_CommandTargetActor
	 *  each tick so a fresh loot ping re-anchors the sweep instead of being silently ignored. */
	TWeakObjectPtr<AActor> CommandedTarget;

	/** Containers proven unreachable this sweep — excluded from FindNextContainer so a nav-blocked
	 *  container can't be re-selected forever (move -> path-short -> skip -> re-select loop). */
	TSet<TWeakObjectPtr<AActor>> SkippedThisSweep;

	/** World location of the originally pinged container — the sweep anchor. */
	FVector SweepAnchor = FVector::ZeroVector;

	bool bMoveRequested = false;

	/** True while holding the post-loot pause at a container. */
	bool bLootTriggered = false;

	float LootWaitElapsed = 0.f;

	int32 LootedCount = 0;

	/** Issues MoveToActor toward CurrentTarget (or flags for immediate loot when already close). */
	bool StartMoveToCurrentTarget(UBehaviorTreeComponent& OwnerComp);

	/** Loots CurrentTarget: stop movement, montage, Execute_Loot, start the pause. */
	void LootCurrentTarget(UBehaviorTreeComponent& OwnerComp);

	/** Picks the nearest still-lootable container within SweepRadius of SweepAnchor.
	 *  Returns null when the sweep is exhausted. */
	AActor* FindNextContainer(UBehaviorTreeComponent& OwnerComp) const;

	/** Chain to the next container, or finish the task (Succeeded if anything was looted). */
	void AdvanceSweep(UBehaviorTreeComponent& OwnerComp);

	/** Clears the active command ONLY if it is still Loot — an abort triggered by a fresh
	 *  replacement command (breach/takedown/new loot) must not wipe that new command. */
	static void ClearCommandIfStillLoot(UBehaviorTreeComponent& OwnerComp);

	/** Clean up state and (conditionally) clear the active command. */
	void FailAndClear(UBehaviorTreeComponent& OwnerComp);
};
