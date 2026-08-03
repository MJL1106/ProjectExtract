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

	/** Projects a loot container's collision base onto the navmesh. Shared by both the loot sweep
	 *  and the explore room evaluation so nav projection logic lives in one place. Returns false
	 *  when the container cannot be projected (candidate should be skipped at selection time). */
	static bool ProjectContainerToNav(UWorld* World, const AActor* Container,
		float HorizExtent, float VertExtent, float AboveRejectTolerance, FVector& OutNavPoint);

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

	/** How far past a container's collision SURFACE a line-of-sight blocker may sit and still count
	 *  as that container rather than a wall (cm). A loot volume carries no mesh — it is draped over
	 *  an existing drawer/table/shelf and that world geometry IS the visual, so the probe point sits
	 *  inside the furniture and the furniture occludes the volume on every trace. A blocker within
	 *  this distance of the surface is the prop being looted; one further out is a wall, i.e. another
	 *  room, and rejects.
	 *
	 *  40 is measured, not guessed, and is bounded ABOVE — do not raise it casually. Across DemoMap
	 *  the farthest draped-furniture hit sits 53.9 cm off the surface while the TIGHTEST structural
	 *  wall sits 43.1 cm off it (BP_Floor_One_Walls1), so the two ranges overlap and no single value
	 *  clears both. 40 is the largest round number under that 43.1 cm ceiling: zero wall false-accepts
	 *  anywhere in the level, and 96.5% of draped-prop hits accepted. 50 makes two structural walls
	 *  passable and the sweep starts reaching into the next room again.
	 *
	 *  ROTATING a container eats that 3.1 cm of headroom: the test measures against the world-space
	 *  AABB, and rotation inflates it (a yaw-rotated 100-cube reads a 70.7 cm half-extent at 45
	 *  degrees instead of 50, so its AABB surface sits up to 20.7 cm nearer the walls than its real
	 *  one). Turning a container to match a diagonal desk can therefore pull a wall inside tolerance
	 *  — re-measure that container's surroundings if you do. */
	UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "0.0"))
	float LootOcclusionTolerance = 40.f;

	/** Maximum vertical distance (cm) between the pawn and the container's collision bounds that
	 *  still counts as "close enough to loot". Containers draped over furniture can sit hundreds of
	 *  units above the floor navmesh; a 3D range check rejects them. This tolerance is checked
	 *  independently of InteractionRange / ArrivalLootRange (which measure horizontal distance). */
	UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "0.0"))
	float VerticalLootTolerance = 500.f;

	/** Maximum distance (cm) the pawn may sit below the container's collision base (Bounds.Min.Z)
	 *  and still loot it. The generous VerticalLootTolerance covers the draped-furniture case where
	 *  the volume sits above the pawn on the same floor; this tighter gate rejects a pawn on the
	 *  storey below that would otherwise loot through the floor slab. */
	UPROPERTY(EditAnywhere, Category = "Loot", meta = (ClampMin = "0.0"))
	float BelowContainerRejectHeight = 100.f;

	/** Horizontal navmesh search half-extent (cm) when projecting a container's origin onto the
	 *  navmesh. Constraining this prevents a container in a sealed closet from projecting metres
	 *  into the next room. Keep in sync with BTTask_CompanionExplore::NavProjectHorizontalExtent. */
	UPROPERTY(EditAnywhere, Category = "Loot|NavProject", meta = (ClampMin = "1.0"))
	float NavProjectHorizontalExtent = 150.f;

	/** Vertical navmesh search half-extent (cm) when projecting a container's collision base
	 *  onto the navmesh. Kept small (roughly one capsule height) so the query box cannot reach
	 *  through a floor slab to the storey below. Keep in sync with
	 *  BTTask_CompanionExplore::NavProjectVerticalExtent. */
	UPROPERTY(EditAnywhere, Category = "Loot|NavProject", meta = (ClampMin = "1.0"))
	float NavProjectVerticalExtent = 200.f;

	/** A nav-projected point whose Z exceeds the container origin Z by more than this is rejected
	 *  (it landed on the floor above). Keep in sync with BTTask_CompanionExplore's equivalent. */
	UPROPERTY(EditAnywhere, Category = "Loot|NavProject", meta = (ClampMin = "0.0"))
	float NavProjectAboveRejectTolerance = 50.f;

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

	/** Issues MoveToLocation toward CurrentTarget's nav goal (or flags for immediate loot when
	 *  already close). When PrecomputedNavGoal is set, skips the projection (FindNextContainer
	 *  already computed it). */
	bool StartMoveToCurrentTarget(UBehaviorTreeComponent& OwnerComp, const FVector* PrecomputedNavGoal = nullptr);

	/** Loots CurrentTarget: stop movement, montage, Execute_Loot, start the pause. */
	void LootCurrentTarget(UBehaviorTreeComponent& OwnerComp);

	/** Picks the nearest still-lootable container within SweepRadius of SweepAnchor.
	 *  Returns null when the sweep is exhausted. On success, OutNavGoal receives the
	 *  pre-computed navmesh goal so StartMoveToCurrentTarget can skip re-projecting. */
	AActor* FindNextContainer(UBehaviorTreeComponent& OwnerComp, FVector& OutNavGoal) const;

	/** Chain to the next container, or finish the task (Succeeded if anything was looted). */
	void AdvanceSweep(UBehaviorTreeComponent& OwnerComp);

	/** Clears the active command ONLY if it is still Loot — an abort triggered by a fresh
	 *  replacement command (breach/takedown/new loot) must not wipe that new command. */
	static void ClearCommandIfStillLoot(UBehaviorTreeComponent& OwnerComp);

	/** Clean up state and (conditionally) clear the active command. */
	void FailAndClear(UBehaviorTreeComponent& OwnerComp);
};
