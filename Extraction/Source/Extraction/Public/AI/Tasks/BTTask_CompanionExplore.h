// BT task — companion commanded search: breach-enter the pinged door (BB_CommandTargetActor).
// Moves to the door's stand point, aligns, plays the per-type breach montage with the door
// swinging at the montage's contact time (same contract as BTTask_CompanionBreach), then steps
// into the room. A hot room hands straight to the combat brain (engagement grant), a quiet room
// with a container chains a loot sweep, an empty room holds a brief dwell before returning to
// follow. An already-open door skips the montage and walks straight in.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Companion/CompanionCommandTypes.h"
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
	/** Accept radius for the move to the door's stand point (cm). Small — the align phase closes
	 *  the last gap so the montage plays from the pose it was authored for. */
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "10.0"))
	float StandPointAcceptRadius = 30.f;

	/** If the move-to path completes farther than this from the stand point (nav gap / blocked
	 *  approach), the search fails instead of playing the anim from the wrong spot. (cm) */
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "10.0"))
	float MaxAlignSnapDistance = 250.f;

	/** Seconds the companion blends from its arrival pose onto the stand point + facing. */
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "0.05"))
	float AlignDuration = 0.25f;

	/** Seconds from montage start until the door swings when the tuning asset has no
	 *  BreachOpenDelay entry for the type. */
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "0.0"))
	float DefaultOpenDelay = 0.4f;

	/** Accept radius for the enter-room move past the doorway (cm). */
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "10.0"))
	float EnterAcceptRadius = 50.f;

	/** Max seconds the enter-room move may take before the room check runs anyway. */
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "0.5"))
	float EnterTimeout = 3.f;

	/** Max seconds the walk to the door may take before the search fails cleanly. */
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "1.0"))
	float MoveTimeout = 20.f;

private:
	enum class ESearchPhase : uint8
	{
		MovingToDoor,
		Aligning,
		PlayingMontage, // montage running, door not yet open
		Holding,        // door open, waiting out the montage remainder
		Entering,       // stepping through the doorway to the room's interior point
		Dwelling,       // empty room — brief stand before handing back to follow
	};

	/** Cached door from the blackboard. Weak — the door may be destroyed mid-task. */
	TWeakObjectPtr<AActor> CachedDoor;

	/** True when a combat target already existed when the search started — the ping was a
	 *  deliberate mid-fight override, so a held-over target must not break the search off. */
	bool bHadCombatTargetAtStart = false;

	/** World time this search (or its latest re-ping restart) began — reference stamp for the
	 *  director's "has a fight started since?" check. */
	float TaskStartWorldTime = 0.f;

	/** True when a fight was already raging at ping time (director combat report within
	 *  RecentFightWindowSeconds of the start stamp) — deliberate mid-fight override, so ongoing
	 *  combat reports must not break the search off. */
	bool bFightOngoingAtStart = false;

	/** Door was already open at task start (or opened by something else on the way over) —
	 *  no montage, no swing, just walk in and run the room check. */
	bool bSkipMontage = false;

	ESearchPhase Phase = ESearchPhase::MovingToDoor;

	/** Stand point + facing the montage was authored for (from IBreachable, or a proximity
	 *  fallback when the door provides none). */
	FVector StandLocation = FVector::ZeroVector;
	FRotator StandFacing = FRotator::ZeroRotator;
	bool bHasStandPoint = false;

	/** Align blend state. */
	FVector AlignStartLocation = FVector::ZeroVector;
	float AlignStartYaw = 0.f;
	float AlignElapsed = 0.f;

	/** Montage/door sequencing. */
	EBreachType PendingBreachType = EBreachType::Tactical;
	float OpenDelay = 0.f;
	float MontageLength = 0.f;
	float PhaseElapsed = 0.f;
	float MoveElapsed = 0.f;

	/** Interior point the enter move targets — the anchor for the room's grant/loot/enemy scan. */
	FVector RoomAnchor = FVector::ZeroVector; // Shared exposure/engagement/loot/enemy-scope anchor.

	/** Rolled per search between the tuning asset's SearchDwellMin/Max. */
	float DwellDuration = 0.f;

	/** Reset all per-search state, validate the door, suppress its auto-open, and start the move
	 *  to its stand point. Shared by ExecuteTask and the mid-search re-ping restart. Fails (with
	 *  FailAndClear already applied) when the door is unusable or the move is rejected. */
	EBTNodeResult::Type BeginSearch(UBehaviorTreeComponent& OwnerComp, class ACompanionAIController* AIC, AActor* Door);

	/** Begin the align phase from the pawn's current pose. */
	void StartAlign(APawn* Pawn);

	/** Start the enter-room move toward the door's interior post-breach point. Returns false when
	 *  the door provides no reachable interior point or the move is rejected — the caller finishes
	 *  the search from where it stands instead. */
	bool StartEnterMove(ACompanionAIController* AIC, APawn* Pawn, AActor* Door);

	/** Room check at the anchor: stamp the engagement grant, then hot room -> clear (combat brain
	 *  takes over), quiet room with a container -> chain a loot sweep, empty room -> start the
	 *  dwell. Unbroken Stealth skips the grant/loot and goes straight to the dwell. Finishes the
	 *  task on every path except the dwell. */
	void EvaluateRoom(UBehaviorTreeComponent& OwnerComp, class ACompanionAIController* AIC, APawn* Pawn);

	/** Suppress/release the door's AI auto-open trigger while this task owns it — the companion
	 *  walking to its stand point would otherwise trip the trigger and pop the door with no
	 *  montage. No-op for a null/non-ADoorBase door. */
	void SetDoorAutoOpenSuppressed(bool bSuppressed);

	/** Clean up (release the door, stop movement) and clear the command — guarded: only if the
	 *  active command is still Explore, so an abort caused by a fresh replacement command must not
	 *  wipe the command it was replaced by. */
	void FailAndClear(UBehaviorTreeComponent& OwnerComp);

	/** Search complete: release the door, clear the command (guarded), finish Succeeded. */
	void FinishSearch(UBehaviorTreeComponent& OwnerComp, class ACompanionAIController* AIC);
};
