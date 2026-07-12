// BT task — companion moves to a breachable target's stand point, aligns to face it,
// plays the per-type breach montage, then swings the door at the montage's contact time.
// Reads BB_CommandTargetActor (set by IssueCommand) and BB_BreachType (set at confirm time).

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Companion/CompanionCommandTypes.h"
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
	/** Accept radius for the move to the breach stand point (cm). Small — the align phase
	 *  closes the last gap so the montage plays from the pose it was authored for. */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "10.0"))
	float StandPointAcceptRadius = 30.f;

	/** If the move-to path completes farther than this from the stand point (nav gap / blocked
	 *  approach), the breach fails instead of playing the anim from the wrong spot. (cm) */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "10.0"))
	float MaxAlignSnapDistance = 250.f;

	/** Seconds the companion blends from its arrival pose onto the stand point + facing. */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "0.05"))
	float AlignDuration = 0.25f;

	/** Seconds from montage start until the door swings when the tuning asset has no
	 *  BreachOpenDelay entry for the type. */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "0.0"))
	float DefaultOpenDelay = 0.4f;

	/** Seconds the companion holds at the door after the swing starts (on top of any montage
	 *  remainder) when the door provides no post-breach point, before the task finishes. */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "0.0"))
	float PostBreachWaitTime = 1.5f;

	/** Accept radius for the post-breach reposition move (cm). */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "10.0"))
	float RepositionAcceptRadius = 50.f;

	/** Max seconds the post-breach reposition may take before the task finishes anyway. */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "0.5"))
	float RepositionTimeout = 3.f;

	/** After a push-through (enter-room) reposition the companion waits inside for the player.
	 *  The wait ends when the player comes within this distance of the companion (cm). */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "50.0"))
	float WaitRejoinRadius = 350.f;

	/** The wait also ends when the player moves farther than this from the door (cm) — the player
	 *  ran off, so the companion returns to normal follow. Roughly double the follow distance. */
	UPROPERTY(EditAnywhere, Category = "Breach", meta = (ClampMin = "100.0"))
	float WaitAbandonDistance = 700.f;

private:
	enum class EBreachPhase : uint8
	{
		MovingToDoor,
		Aligning,
		PlayingMontage, // montage running, door not yet open
		Holding,          // door open, waiting out the montage remainder
		Repositioning,    // clearing the doorway: Loud walks in past the leaf, others sidestep outside
		WaitingForPlayer, // push-through only: holding inside until the player follows or runs off
	};

	/** Cached door from the blackboard. Weak — the door may be destroyed mid-task. */
	TWeakObjectPtr<AActor> CachedDoor;

	/** True when a combat target already existed when the breach started — the ping was a
	 *  deliberate mid-fight override, so a held-over target must not break the breach off. */
	bool bHadCombatTargetAtStart = false;

	/** One-shot: the post-breach reposition was attempted and declined (no point / no path). */
	bool bRepositionDeclined = false;

	/** True while the active reposition is an enter-room push-through — gates the post-arrival
	 *  wait-for-player phase (sidesteps finish immediately as before). */
	bool bEnterRoomReposition = false;

	EBreachPhase Phase = EBreachPhase::MovingToDoor;

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

	/** Begin the align phase from the pawn's current pose. */
	void StartAlign(APawn* Pawn);

	/** Swing the door + emit the per-type noise (montage contact time reached). */
	void OpenDoorNow(class ACompanionAIController* AIC, AActor* Door);

	/** Suppress/release the door's AI auto-open trigger while this task owns it — the companion
	 *  walking to its stand point would otherwise trip the trigger and pop the door with no
	 *  montage, failing the commanded breach. No-op for non-ADoorBase breachables. */
	void SetDoorAutoOpenSuppressed(bool bSuppressed);

	/** Clean up and fail. */
	void FailAndClear(UBehaviorTreeComponent& OwnerComp);

	/** Shared success finish for the post-swing phases: releases the door, stamps the post-breach
	 *  engagement grant, and either chains a loot sweep into a quiet room (IssueCommand(Loot) — no
	 *  ClearActiveCommand on this path so the follow-up command survives) or clears the command so
	 *  the combat brain takes over. Unbroken Stealth skips the chain entirely. */
	void FinishBreachSuccess(UBehaviorTreeComponent& OwnerComp, class ACompanionAIController* AIC,
		APawn* Pawn, AActor* Door);

	/** Chain IssueCommand(Loot) into a quiet room: nearest still-lootable container within the explore
	 *  radius of Anchor, only when no combat target and no live enemy in range. Returns true when the
	 *  command was chained (caller must then NOT ClearActiveCommand). Does not stamp the grant. */
	bool ChainLootFromAnchor(UBehaviorTreeComponent& OwnerComp, class ACompanionAIController* AIC,
		APawn* Pawn, const FVector& Anchor);
};
