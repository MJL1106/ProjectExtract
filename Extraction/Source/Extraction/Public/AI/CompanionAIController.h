// AI controller for the companion — perception, blackboard, behaviour tree.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GenericTeamAgentInterface.h"
#include "Movement/TraversalTypes.h"
#include "Companion/CompanionCommandTypes.h"
#include "CompanionAIController.generated.h"

class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UCompanionTuningDataAsset;
class UTraversalComponent;
class ACompanionRoute;
class ACompanionCharacter;

DECLARE_LOG_CATEGORY_EXTERN(LogCompanionAI, Log, All);

UCLASS()
class EXTRACTION_API ACompanionAIController : public AAIController
{
	GENERATED_BODY()

public:
	ACompanionAIController();

	// Blackboard key names
	static const FName BB_PlayerActor;
	static const FName BB_PlayerNeedsRevive;
	static const FName BB_CombatTarget;
	static const FName BB_CoverLocation;
	static const FName BB_HasCoverPosition;
	static const FName BB_PlayerTraversalActive;
	static const FName BB_PlayerTraversalObstacle;
	static const FName BB_PlayerTraversalLanding;
	static const FName BB_PlayerTraversalType;
	static const FName BB_Posture;                       // enum: ECompanionPosture
	static const FName BB_ScoringWeight_LoSPlayer;       // float
	static const FName BB_ScoringWeight_AvoidEnemy;      // float
	static const FName BB_ScoringWeight_CoverFromTarget; // float
	static const FName BB_CoverSlot;                     // Object (AAICoverSlot)
	static const FName BB_NextCoverSlot;                 // Object (AAICoverSlot) — set by CoverSwitchMonitor, consumed by MoveToCover
	static const FName BB_ActiveRoute;                   // Object (ACompanionRoute)
	static const FName BB_RouteActive;                   // bool
	static const FName BB_RouteBlocksCombat;              // bool
	static const FName BB_CompanionCommand;              // enum ECompanionCommand
	static const FName BB_CommandTargetActor;            // Object
	static const FName BB_CommandTargetLocation;         // Vector
	static const FName BB_TakedownMethod;                // enum ETakedownMethod
	static const FName BB_BreachType;                    // enum EBreachType
	static const FName BB_FollowLeader;                  // Object (Actor) — see SetFollowLeader

	/**
	 * Write a command into the companion blackboard.
	 * Breach and Takedown both require a valid TargetActor — logs and early-returns if missing.
	 */
	void IssueCommand(ECompanionCommand Command, ETakedownMethod Method, AActor* TargetActor,
		const FVector& TargetLocation, bool bPreserveSearchRoomExposure = false);

	/** Reset BB_CompanionCommand to None and clear related keys. */
	void ClearActiveCommand();

	/** Writes BB_BreachType — call before IssueCommand(Breach). Derived from companion mode at confirm time. */
	void SetBreachType(EBreachType Type);

	UFUNCTION(BlueprintPure, Category = "Companion|AI")
	APawn* GetPlayerCharacter() const { return CachedPlayerCharacter.Get(); }

	void SetPlayerCharacter(APawn* InPlayer) { CachedPlayerCharacter = InPlayer; }

	/**
	 * Publishes the actor this companion forms up on: the player for a primary companion, the primary
	 * companion for a secondary (the armed extractee VIP). UBTTask_FollowPlayer owns the resolution —
	 * it holds the throttled primary-companion scan and the leash latches — and calls this once per
	 * follow tick with its result, so nothing here rescans. Deduped on the pointer, so only a genuine
	 * handover reaches the blackboard.
	 */
	void SetFollowLeader(AActor* Leader);

	/**
	 * The published follow leader, or the player when nothing has published one yet or the published
	 * one has stopped being a usable anchor. Deliberately survives the follow task finishing: cover
	 * scoring runs on the combat branch where follow is not ticking, and handing back the player there
	 * would split a secondary's cover anchor from its follow anchor — the drift this exists to remove.
	 * May return null before the player pawn is cached; callers keep their own fallback.
	 */
	AActor* GetFollowLeader() const;

	/**
	 * True when Leader is still a usable follow anchor for Follower: valid, not the follower itself,
	 * possessed, not DBNO and not dead. Shared by the follow task's per-tick re-test and by
	 * GetFollowLeader's read-time staleness check, so a leader that goes down mid-fight stops
	 * anchoring cover scoring on the same tick it stops anchoring follow.
	 */
	static bool IsUsableFollowLeader(const ACompanionCharacter* Leader, const ACompanionCharacter* Follower);

	const UCompanionTuningDataAsset* GetTuning() const { return Tuning; }

	/** Live-fight signal, published by BTService_UpdateCompanionState each service tick while any
	 *  alerted enemy is known near the party (no companion eye-line required — the player-threat
	 *  channel sees through walls). Lets follow-path systems act fight-aware when the companion
	 *  itself can't see the fight (trailing extractee behind a wall). */
	void NoteAlertedThreat(const FVector& Location);

	/** True when the alerted-threat signal is fresher than MaxAge; outputs its location. */
	bool GetRecentAlertedThreat(float MaxAge, FVector& OutLocation) const;

	/** Hearing stimulus MaxAge (seconds). Stealth-break gates its heard-enemy signal past this so
	 *  stimuli from before a fresh Stealth order can't break it. */
	float GetHearingSenseMaxAge() const;

	/** Resets the BB_PlayerTraversal* keys. Public so BT tasks can call on early-finish paths. */
	void ClearTraversalBlackboard();

	/** Activates a pre-authored companion route. Sets BB keys so the BT route branch activates. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Route")
	void StartRoute(ACompanionRoute* Route);

	/** Clears route BB keys. Does NOT broadcast OnRouteCompleted — the BT task owns that. */
	void StopRoute(bool bAborted);

	/**
	 * Installs Route as the companion's facing reference: while following the player it faces the way
	 * that route runs. The route is never walked, and movement, formation and follow distance are all
	 * untouched. One reference is live at a time — installing another replaces it.
	 * Rejects a route that is null, not flagged bUseAsFacingReference, or shorter than 2 waypoints,
	 * and clears the existing reference when it does, so a mis-set trigger can't leave the previous
	 * section's direction quietly running.
	 */
	UFUNCTION(BlueprintCallable, Category = "Companion|Route")
	void SetFacingReferenceRoute(ACompanionRoute* Route);

	/** The live facing reference, or null when the companion has none. */
	UFUNCTION(BlueprintPure, Category = "Companion|Route")
	ACompanionRoute* GetFacingReferenceRoute() const;

	/** Uninstalls the facing reference — the companion reverts to its default facing behaviour. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Route")
	void ClearFacingReferenceRoute();

	/**
	 * AI-safe teleport. Always projects the destination onto the NavMesh first, then cancels any
	 * active traversal, stops the current move order and teleports -- so the pawn never lands
	 * off-navmesh and no stale AI order survives the move. Returns the real move result; false means
	 * the pawn is missing, NavMesh projection failed, or the destination was encroached/blocked.
	 *
	 * bForce == false (default): the three early-out refusal paths (missing pawn, nav-project fail,
	 * traversal busy) must disturb nothing, because callers retry. Probes with
	 * UWorld::FindTeleportSpot -- the only genuinely non-mutating query -- and bails before touching
	 * pawn state if the spot is blocked.
	 *
	 * bForce == true: don't pre-refuse (skip the traversal-busy gate and the FindTeleportSpot
	 * probe), cancel whatever the pawn is doing, and attempt the move anyway. The move can still
	 * legitimately fail (NavMesh projection or encroachment at the destination).
	 */
	bool TeleportToLocation(const FVector& Location, const FRotator& Rotation, bool bForce = false);

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|AI")
	TObjectPtr<UBehaviorTree> CompanionBehaviorTree;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Tuning")
	TObjectPtr<UCompanionTuningDataAsset> Tuning;

private:
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	TWeakObjectPtr<APawn> CachedPlayerCharacter;

	/** Last leader published by the follow task (see SetFollowLeader). Weak: it is either the player
	 *  or the primary companion, and neither is owned by this controller — the primary can die or be
	 *  destroyed while a secondary is still reading it. */
	TWeakObjectPtr<AActor> PublishedFollowLeader;

	/** Last leader actually written to BB_FollowLeader. Separate from the member above so a publish that
	 *  arrives while the blackboard is unavailable cannot be mistaken for one already mirrored. */
	TWeakObjectPtr<AActor> MirroredFollowLeader;

	/** One-shot latch for the missing-BB_FollowLeader warning. SetFollowLeader runs off a per-tick
	 *  caller, so an unwired key must cost one log line, not a stream. */
	bool bFollowLeaderKeyWarned = false;

	/** Live facing reference (see SetFacingReferenceRoute). Weak because the route actor belongs to the
	 *  level, not to us, and level streaming can pull it out from under an installed reference.
	 *  Deliberately survives player death, revive and ExecuteWarpBehindPlayer: it describes which way
	 *  the level section runs, not anything about the companion's own state. Only a later trigger, a
	 *  Clear Facing Reference trigger, or unpossession takes it away. */
	TWeakObjectPtr<ACompanionRoute> FacingReferenceRoute;

	// Live-fight signal (see NoteAlertedThreat). Time is world seconds; negative = never.
	FVector LastAlertedThreatLocation = FVector::ZeroVector;
	float LastAlertedThreatTime = -1e9f;

	// Traversal mirror coupling
	TWeakObjectPtr<UTraversalComponent> PlayerTraversalComp;
	FDelegateHandle TraversalStartedHandle;
	FDelegateHandle TraversalEndedHandle;

	// Warp safety net
	FTimerHandle WarpStuckTimer;
	FTimerHandle BindRetryTimer;
	float TimeSinceClosedToPlayer = 0.f;
	float TimeOnDifferentLevel = 0.f;
	int32 BindAttempts = 0;

	/** True once the current refusal streak has been warned about. Suppresses the per-tick repeat. */
	bool bWarpRefusalWarned = false;

	void TryBindToPlayerTraversal();
	void OnPlayerTraversalStarted(ETraversalType Type, float PlayRate, FVector ObstacleLocation, FVector LandingLocation);
	void OnPlayerTraversalEnded();
	void TickWarpFallback();
	bool IsCompanionRecentlyRendered() const;
	bool ShouldWarp() const;
	void ExecuteWarpBehindPlayer();

	/** Releases any pending NextCoverSlot claim and clears the BB key. Belt-and-braces for AI teardown paths. */
	void ReleaseNextCoverSlotIfClaimed();
};
