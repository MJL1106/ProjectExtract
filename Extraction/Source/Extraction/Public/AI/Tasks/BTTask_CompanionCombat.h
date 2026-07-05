// BT task — cover-aware companion combat. State machine drives EngageFromOpen, EngageFromCover, StandUpFire.
// P3 AICS migration: cover source changed from AAICoverSlot line-segment slots to FCoverHandle/FCoverData points.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Companion/CompanionTypes.h"
#include "AI/Cover/CoverPoseTypes.h"
#include "CoverSystemPublicData.h"
#include "BTTask_CompanionCombat.generated.h"

class UAnimMontage;
class ACompanionCharacter;
class UCompanionAnimInstance;
class UCharacterMovementComponent;
class AAIController;
class AController;
class ACoverSystem;
class UCoverReservationSubsystem;

UCLASS()
class EXTRACTION_API UBTTask_CompanionCombat : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionCombat();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasCoverPositionKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverLocationKey;

	/** BB key holding the Cover type (FCover via AICS plugin). Default name "CoverTarget". */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverTargetKey;

	// --- Burst timing ---

	/** Min seconds of fire per cover stand-up burst. */
	UPROPERTY(EditAnywhere, Category = "Combat|Burst", meta = (ClampMin = "0.1"))
	float MinFireBurst = 0.9f;

	/** Max seconds of fire per cover stand-up burst. */
	UPROPERTY(EditAnywhere, Category = "Combat|Burst", meta = (ClampMin = "0.1"))
	float MaxFireBurst = 2.0f;

	/** Min seconds of a quick-peek burst (less exposure). */
	UPROPERTY(EditAnywhere, Category = "Combat|Burst", meta = (ClampMin = "0.1"))
	float MinQuickPeekBurst = 0.25f;

	/** Max seconds of a quick-peek burst. */
	UPROPERTY(EditAnywhere, Category = "Combat|Burst", meta = (ClampMin = "0.1"))
	float MaxQuickPeekBurst = 0.6f;

	/** Seconds of pause between bursts in open-engage. */
	UPROPERTY(EditAnywhere, Category = "Combat|Burst", meta = (ClampMin = "0.1"))
	float FirePauseDuration = 0.5f;

	// --- Peek action weights (normal health) ---

	/** Relative weight for standing up and firing a full burst. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0.0"))
	float StandWeight = 60.f;

	/** Relative weight for a short quick-peek burst. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0.0"))
	float QuickWeight = 25.f;

	/** Relative weight for staying in cover one more cycle. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0.0"))
	float HoldWeight = 15.f;

	// --- Peek action weights (low health) ---

	/** Stand weight when health is below LowHealthFraction. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0"))
	float LowHpStandWeight = 15.f;

	/** Quick weight when health is below LowHealthFraction. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0"))
	float LowHpQuickWeight = 35.f;

	/** Hold weight when health is below LowHealthFraction. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0"))
	float LowHpHoldWeight = 50.f;

	/** Health fraction below which low-health weights apply. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthFraction = 0.35f;

	/** Multiplier applied to next peek cooldown when below LowHealthFraction. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "1.0"))
	float LowHealthCooldownMultiplier = 2.0f;

	/** How many consecutive Hold cycles before the companion is forced to Stand. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0", ClampMax = "5"))
	uint8 MaxConsecutiveHolds = 2;

	// --- Suppression ---

	/** Recent damage within this window flags the companion as suppressed. 0 disables. */
	UPROPERTY(EditAnywhere, Category = "Combat|Suppression", meta = (ClampMin = "0.0"))
	float SuppressionWindowSeconds = 1.8f;

	/** Multiplier applied to the next peek cooldown when suppressed. */
	UPROPERTY(EditAnywhere, Category = "Combat|Suppression", meta = (ClampMin = "1.0"))
	float SuppressionCooldownMultiplier = 1.5f;

	// --- Open-engage LoS abandon ---

	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float LosBlockedAbandonSeconds = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float AimDropOnLosBlockedSeconds = 0.3f;

	// --- Open-engage cover re-seek (restores cover priority while move-shooting in the open) ---

	/** While move-shooting with no cover, re-check for reachable cover this often (s). 0 disables. */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float OpenEngageCoverReseekInterval = 1.0f;

	// --- Open-area move-and-shoot ---

	/** Master toggle. When false, open-engage falls back to today's stand-still burst-fire. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot")
	bool bEnableOpenAreaMoveAndShoot = true;

	/** Movement speed (cm/s) while aiming + firing in the open — tactical pace, below WalkSpeed. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "1.0"))
	float CombatMoveSpeed = 300.f;

	/** Below this range (cm) the companion retreats from the target. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "0.0"))
	float MoveShootIdealRangeMin = 300.f;

	/** Above this range (cm) the companion advances on the target. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "0.0"))
	float MoveShootIdealRangeMax = 900.f;

	/** Max lateral step magnitude (cm) for the LoS-blocked regain fan (TickRegainLosReposition). Not used by the clear-LoS jiggle. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "1.0"))
	float MoveShootStrafeDistance = 130.f;

	/** Seconds between re-picks for the LoS-blocked regain fan (TickRegainLosReposition's MoveToLocation gate). */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "0.1"))
	float MoveShootRepositionInterval = 1.2f;

	/** Accept radius (cm) for the LoS-blocked regain fan's MoveToLocation. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "1.0"))
	float MoveShootAcceptRadius = 60.f;

	/** Half-extents (cm) for projecting a candidate point onto the navmesh (regain fan + jiggle drift). */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "1.0"))
	float MoveShootNavProjectExtent = 200.f;

	// --- Combat jiggle (clear-LoS restless micro-motion) ---

	/** Radius (cm) of the tight circle the companion jiggles within while it has LoS. THE main intensity dial — bigger = more movement. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "0.0"))
	float JiggleRadius = 220.f;

	/** Seconds between re-aiming within the jiggle circle. Lower = faster, jitterier jiggle. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "0.05"))
	float JiggleRetargetInterval = 1.0f;

	/** Within this horizontal distance (cm) of the jiggle micro-target, the offset re-rolls. Bigger = longer committed steps. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "1.0"))
	float JiggleReachThreshold = 70.f;

	/** Max turn rate (deg/s) of the move-shoot strafe heading. Lower = smoother, more deliberate steps; very high ≈ today's snap. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "1.0"))
	float CombatMoveTurnRate = 200.f;

	/** MaxAcceleration (cm/s^2) while move-shooting — below the engine default so the companion eases into strafe steps instead of darting. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "1.0"))
	float CombatMoveAcceleration = 1000.f;

	/** Seconds between considering a closer/farther drift of the jiggle home anchor. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "0.1"))
	float JiggleDriftInterval = 1.5f;

	/** Distance (cm) the jiggle home anchor nudges per closer/farther drift. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "0.0"))
	float JiggleDriftStep = 120.f;

	/** Relative weight of drifting the home anchor toward the target. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "0.0"))
	float JiggleDriftCloserWeight = 20.f;

	/** Relative weight of drifting the home anchor away from the target. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "0.0"))
	float JiggleDriftFartherWeight = 20.f;

	/** Relative weight of holding the home anchor (no drift) — default = mostly jiggle in place. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "0.0"))
	float JiggleDriftHoldWeight = 60.f;

	/** Re-roll attempts to find a jiggle micro-target with clear LoS to the target before falling back to sitting on the anchor. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveShoot", meta = (ClampMin = "1", ClampMax = "16"))
	int32 JiggleLosRetryCount = 4;

	// --- Cover timing ---

	/** Minimum dwell in cover-idle before the first peek can fire. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float MinCoverIdleDwell = 0.4f;

	/** Z offset above slot location for the stand-up-fire LoS trace. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float StandFireEyeHeight = 150.f;

	/** Min peek cooldown (randomised per cycle). */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float MinPeekCooldown = 0.8f;

	/** Max peek cooldown (randomised per cycle). */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float MaxPeekCooldown = 2.2f;

	/** Interval (seconds) between cover-validity LOS rechecks. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.25", ClampMax = "5.0"))
	float CoverValidityCheckInterval = 1.0f;

	/** Interval (seconds) between LoS re-picks while in cover idle. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.1"))
	float SubSlotLosRecheckInterval = 0.6f;

	/** Minimum time at current cover before a failed re-eval can abandon the slot. */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MinCoverDwellBeforeReEval = 2.0f;

	/** Toggle verbose exit-gate logs + debug draw under LogCompanionAI. */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDebugLogging = false;

	// --- Cover entry ---

	/** Companion is considered "at sub-slot" when within this distance (cm). */
	UPROPERTY(EditAnywhere, Category = "Cover|Entry", meta = (ClampMin = "5.0"))
	float FinalApproachAcceptRadius = 30.f;

	/** Max time to wait for the final-approach walk before forcing the snap (failsafe). */
	UPROPERTY(EditAnywhere, Category = "Cover|Entry", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float FinalApproachTimeout = 3.0f;

	/** Seconds the path-follower must be Idle (without arriving) before snapping early. */
	UPROPERTY(EditAnywhere, Category = "Cover|Entry", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float FinalApproachStalledGracePeriod = 0.25f;

	/** Duration of the smooth-snap tween that replaces cover entry / return teleport pops (seconds). */
	UPROPERTY(EditAnywhere, Category = "Combat|Movement", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float SmoothSnapDuration = 0.2f;

	/** Minimum distance (cm) to a shuffle destination cover point (replaces the old line-Alpha delta). */
	UPROPERTY(EditAnywhere, Category = "Combat|Movement", meta = (ClampMin = "1.0"))
	float ShuffleDistanceMin = 50.f;

	/** Maximum distance (cm) to a shuffle destination cover point (replaces the old line-Alpha delta). */
	UPROPERTY(EditAnywhere, Category = "Combat|Movement", meta = (ClampMin = "1.0"))
	float ShuffleDistanceMax = 125.f;

	// --- New action weights (crouch cover) ---

	/** Relative weight for standing, firing, and walking to an adjacent sub-slot. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0.0"))
	float StandUpAndRepositionWeight = 20.f;

	/** Relative weight for silent lateral repositioning within Crouch cover. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0.0"))
	float RepositionWeight = 15.f;

	/** Relative weight for silent lateral repositioning within Stand cover. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0.0"))
	float RepositionWeightStand = 0.f;

	/** Low-health variant for Reposition in Crouch cover. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0"))
	float LowHpRepositionWeight = 25.f;

	/** Low-health variant for Reposition in Stand cover. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0"))
	float LowHpRepositionWeightStand = 0.f;

	/** Low-health variant for StandUpAndReposition. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0"))
	float LowHpStandUpAndRepositionWeight = 5.f;

	// --- Stand-cover endpoint weights ---

	/** Relative weight for stepping out past a corner, firing, then retreating (Stand cover endpoints only). */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights", meta = (ClampMin = "0.0"))
	float CornerPeekWeight = 60.f;

	/** Low-health variant for CornerPeek. */
	UPROPERTY(EditAnywhere, Category = "Combat|PeekWeights|LowHealth", meta = (ClampMin = "0.0"))
	float LowHpCornerPeekWeight = 25.f;

	// --- Reposition / CornerPeek tunables ---

	/** Lateral interp speed for Reposition and StandUpAndReposition walk phase (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Combat|Movement", meta = (ClampMin = "1.0"))
	float RepositionWalkSpeed = 150.f;

	/** Lateral interp speed for CornerPeek outbound strafe (apex side) (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Combat|Movement", meta = (ClampMin = "1.0"))
	float CornerPeekOutSpeed = 150.f;

	/** Lateral interp speed for CornerPeek return strafe (back to home) (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Combat|Movement", meta = (ClampMin = "1.0"))
	float CornerPeekReturnSpeed = 150.f;

	/** How far past the wall corner the CornerPeek apex clears, beyond the capsule radius (cm).
	 *  Apex = corner + capsule radius + this. */
	UPROPERTY(EditAnywhere, Category = "Combat|Movement", meta = (ClampMin = "0.0"))
	float CornerPeekApexClearance = 20.f;

	/** Distance threshold to consider a reposition/corner-peek movement arrived (cm). */
	UPROPERTY(EditAnywhere, Category = "Combat|Movement", meta = (ClampMin = "1.0"))
	float RepositionArrivalTolerance = 8.f;

	/** Seconds the path-follower must be stalled (no dist progress) before snapping to destination. */
	UPROPERTY(EditAnywhere, Category = "Combat|Movement", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float RepositionStalledGracePeriod = 0.5f;

	/** Max total time allowed for a reposition walk before snap-warp fallback. */
	UPROPERTY(EditAnywhere, Category = "Combat|Movement", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float RepositionTimeoutSeconds = 3.0f;

private:
	enum class EPeekAction : uint8 { Stand, Quick, Hold, Reposition, StandUpAndReposition, CornerPeek };

	/** Jiggle home drift direction relative to the target each drift cycle. */
	enum class EJiggleDrift : uint8 { Closer, Farther, Hold };

	static EPeekAction RollPeekActionMulti(TArrayView<const TPair<EPeekAction, float>> Weighted);

	// Per-burst helpers
	void ReturnToCover(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
		const FCoverData& CoverData, bool bSuppressed, bool bLowHealth);

	void TickRepositionAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim, UBlackboardComponent* BB,
		const FCoverData& CoverData, bool bSuppressed, bool bLowHp, float DeltaSeconds);
	void TickStandUpAndRepositionAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim, UBlackboardComponent* BB,
		const FCoverData& CoverData, bool bSuppressed, bool bLowHp, float DeltaSeconds);
	void TickCornerPeekAction(ACompanionCharacter* Companion, UCompanionAnimInstance* Anim,
		const FCoverData& CoverData, AActor* Target, bool bSuppressed, bool bLowHp,
		TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds);

	/** Shared move-shoot entry: caches + lowers walk speed and focuses the target (single-source for the MaxWalkSpeed override). No-op if already active. */
	void EnterMoveShootIfNeeded(ACompanionCharacter* Companion, AAIController* AIC, AActor* Target, UCharacterMovementComponent* CMC, const TCHAR* Reason);

	/** Shared move-shoot re-pick gate: decrements the reposition timer and returns true when a new destination should be chosen this tick. */
	bool ShouldRepickMoveShoot(ACompanionCharacter* Companion, AAIController* AIC, float DeltaSeconds);

	/** Open-area combat jiggle: continuous restless micro-motion (AddMovementInput) within JiggleRadius of JiggleHome while LoS is clear. SetFocus already faces the enemy; firing runs after this call. */
	void TickCombatJiggle(ACompanionCharacter* Companion, AAIController* AIC, AActor* Target, TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds);

	/** Re-rolls JiggleOffset up to JiggleLosRetryCount times to find a micro-target with LoS; falls back to ZeroVector (sit on anchor) if none pass. Resets the retarget timer. */
	void RerollJiggleOffset(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached);

	/** Drift cycle: weighted-rolls Closer/Farther/Hold and nudges JiggleHome by JiggleDriftStep along the horizontal companion->target axis, clamped to [MoveShootIdealRangeMin, MoveShootIdealRangeMax] and nav-projected. LoS-gates the Closer drift. No-op on Hold or failed projection. */
	void TickJiggleDrift(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds);

	/** Player-pull move-and-shoot: when the player is too far, close the gap while keeping the enemy focused. Prefers player proximity over jiggle position. */
	void TickMoveShootTowardPlayer(ACompanionCharacter* Companion, AAIController* AIC, AActor* Target, float DeltaSeconds);

	/** Weighted-random jiggle drift direction from the Closer/Farther/Hold weights. Returns Hold if all weights are zero. */
	EJiggleDrift RollJiggleDrift() const;

	/** LoS-blocked reposition: keep facing the target, sidestep laterally to a nav point that has LoS. Holds (no thrash) if no side works. */
	void TickRegainLosReposition(ACompanionCharacter* Companion, AAIController* AIC, AActor* Target, TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds);

	/** Shared LoS test: does Point (raised to eye height) have a clear ECC_Visibility line to the target? Ignores self + weapon + attached. */
	bool PointHasLosToTarget(ACompanionCharacter* Companion, const FVector& Point, AActor* Target, TArrayView<AActor* const> IgnoredAttached) const;

	/** Move-shoot facing: live focus on actor when LoS is clear; frozen focal point at LastKnownTargetLocation when blocked. No-op if blocked and no last-known location yet. */
	void UpdateMoveShootFacing(AAIController* AIC, AActor* Target, bool bLosClear);

	/** Projects MyLoc+LateralOffset onto the navmesh and returns true only if the projected point has LoS to the target. */
	bool TryLateralLosDestination(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached, const FVector& LateralOffset, FVector& OutDest) const;

	/** Regain-LoS fan: tests right/left/back/back-right/back-left offsets at StepDistance, nav-projects + LoS-verifies each, returns the NEAREST valid point (smallest displacement). Biases toward a small back-step over a big swing. Returns false if none clear. */
	bool PickFanLosDestination(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached, float StepDistance, FVector& OutDest) const;

	/** Symmetric teardown for move-and-shoot: stop movement, clear focus, restore walk speed, reset state. */
	void EndOpenAreaMoveShoot(ACompanionCharacter* Companion);

	float BurstTimer = 0.0f;
	bool bIsFiringBurst = false;

	float TimeInCoverIdle = 0.f;
	float PeekCooldown = 0.f;
	EPeekSide ResolvedPeekSide = EPeekSide::Right;

	/** Trace-verified side resolve (shared UCoverGeometryStatics::ChooseGapPeekSide) — writes BOTH
	 *  CurrentLean and ResolvedPeekSide. Crouch cover with no verified side gap maps to Front
	 *  (over-top analogue, enemy parity); stand cover keeps None so the roll can reject the point. */
	void ResolvePeekSideForCover(ACompanionCharacter* Companion, AActor* Target,
		const FCoverData& CoverData, const FVector& ThreatLoc);

	/** Completed peek cycles at the current cover point (incremented per ReturnToCover). Gates
	 *  reposition/shuffle/compromise-relocate on MinPeekCyclesBeforeRelocate — commit to a point
	 *  before wandering. */
	int32 PeekCyclesAtCover = 0;

	/** Cycle carry across task restarts: cycles are per-physical-cover, but target churn (death /
	 *  switch) exits and re-enters this task several times per fight at the same point. Stamped at
	 *  exit (OnTaskFinished/AbortTask, before ResetTaskState wipes the live counter); ExecuteTask
	 *  restores the count when it re-claims the same handle. Deliberately NOT touched by
	 *  ResetTaskState/ResetPeekCycleCounters. */
	FCoverHandle PeekCycleCarryHandle;
	int32 PeekCycleCarryCount = 0;

	/** Consecutive failed no-peek-los validity evals (1 Hz) — invalidate only at the
	 *  CoverCompromiseDebounce threshold instead of instantly. */
	int32 NoPeekLosStrikes = 0;

	/** Zero both cycle counters AND the character mirror in the same call — the monitor's G5 gate
	 *  reads the mirror, and a one-frame stale value at a task-internal swap lets a pre-swap
	 *  pending candidate commit against the point we just arrived at. */
	void ResetPeekCycleCounters(ACompanionCharacter* Companion);

	/** One-shot log latch: crouch-cover stand-up committed with no over-top montage wired. */
	bool bWarnedOverTopUnwired = false;

	/** Shared silent-Reposition commit: stamps intent, low-readies, hands control to
	 *  TickRepositionAction (walk + stall/timeout snap + arrival CommitCoverSwitch). Used by the
	 *  action roll and the blocked-LOS shuffle reroute (replaces the old mid-combat TeleportTo). */
	void CommitSilentReposition(ACompanionCharacter* Companion, const FCover& ShuffleTarget, float Now);

	float CoverValidityCheckTimer = 0.f;
	float TimeAtCurrentCover = 0.f;

	FVector LastPeekResolveCoverLoc = FVector::ZeroVector;
	FVector LastPeekResolveTargetLoc = FVector::ZeroVector;
	static constexpr float PeekResolveDistThresholdSq = 50.f * 50.f;

	float LosBlockedAccum = 0.f;
	float TimeInOpenEngageNoCover = 0.f;
	int8 LastTickBranch = -1;
	bool bLastLosBlocked = false;
	TWeakObjectPtr<AActor> LastLosBlocker;

	// Open-area move-and-shoot state (shared entry/speed/focus + the LoS-blocked regain fan)

	/** Last target location where open-area LoS was confirmed clear; frozen at the moment LoS is lost. */
	FVector LastKnownTargetLocation = FVector::ZeroVector;
	/** True once LastKnownTargetLocation has been written at least once during the current engagement. */
	bool bHasLastKnownTargetLocation = false;

	FVector MoveShootDestination = FVector::ZeroVector;
	bool bMoveShootMoveActive = false;
	float MoveShootRepositionTimer = 0.f;
	/** True while holding after a failed nav projection — gate must wait the full interval (ignore bIdle) before re-picking. */
	bool bMoveShootHolding = false;
	/** CMC->MaxWalkSpeed captured on entry so it can be restored on exit. 0 = not captured. */
	float CachedDefaultWalkSpeed = 0.f;

	/** Latched leash bool with hysteresis — trips at LeashDist, releases only once back inside the dead-band. */
	bool bPlayerPullLatched = false;
	/** True while TickMoveShootTowardPlayer owns movement; cleared by jiggle/regain paths to edge-detect re-entry and reset the shared pick gate. */
	bool bPlayerPullActive = false;

	// Combat jiggle state (clear-LoS restless micro-motion; per-instance, server-only)
	/** True while the jiggle micro-motion owns movement. Cleared by EndOpenAreaMoveShoot and on a LoS-blocked stretch so JiggleHome re-anchors on resume. */
	bool bJiggleActive = false;
	/** World anchor the jiggle circles around — re-anchored to current location on each (re)activation, nudged by drift. */
	FVector JiggleHome = FVector::ZeroVector;
	/** Current ground-plane offset from JiggleHome defining the micro-target; magnitude in [0, JiggleRadius]. */
	FVector JiggleOffset = FVector::ZeroVector;
	/** Counts down to the next JiggleOffset re-roll. */
	float JiggleRetargetTimer = 0.f;
	/** Counts down to the next closer/farther drift consideration. */
	float JiggleDriftTimer = 0.f;
	/** Smoothed movement direction for the jiggle strafe walk — interpolated toward the desired heading each tick. Reset in EndOpenAreaMoveShoot. */
	FVector SmoothedMoveDir = FVector::ZeroVector;

	/** CMC->MaxAcceleration captured on move-shoot entry so it can be restored on exit. 0 = not captured. */
	float CachedDefaultAcceleration = 0.f;

	/** Seconds the companion has continuously been inside the player's ADS cone during jiggle steering.
	 *  When this exceeds the escape threshold, jiggle re-anchors to break the wall-grind stall. */
	float InConeContinuousTime = 0.f;

	// Peek action state
	EPeekAction CurrentBurstAction = EPeekAction::Stand;
	uint8 ConsecutiveHolds = 0;

	/** Set true when a Reposition (silent or StandUpAndReposition) action just completed; blocks Reposition from rolling on the next action roll. Cleared once any non-Reposition action commits. */
	bool bJustRepositioned = false;

	// Reposition / StandUpAndReposition state
	/** Shuffle destination cover point, set on commit; invalid Handle = no reposition target picked. */
	FCover RepositionTargetCover;
	/** World-space target for the in-progress reposition walk, snapshotted at commit so mid-walk arrow drag does not retarget. */
	FVector RepositionTargetWorldLoc = FVector::ZeroVector;
	bool bRepositionStandPhase = false;
	bool bStandUpRepositionWalking = false;
	float LastRepositionDist = 0.f;
	float RepositionElapsed = 0.f;
	float RepositionStalledTime = 0.f;
	bool bRepositionStartLogged = false;

	// CornerPeek state
	FVector CornerPeekHomeLocation = FVector::ZeroVector;
	FVector CornerPeekApexLocation = FVector::ZeroVector;
	bool bCornerPeekReturning = false;
	bool bCornerPeekFiring = false;

	// Debug-only rate limiter for stand-burst LoS trace
	float DebugBurstLosCheckTimer = 0.f;

	// Functional muzzle-LoS withhold during the in-place stand/quick burst: pauses the trigger (no shots)
	// while the muzzle→target line is blocked, resuming when clear — burst timing is unaffected so FSM flow
	// is unchanged. Throttled by StandBurstMuzzleCheckTimer (10 Hz). bStandBurstFireHeld = fire withheld.
	float StandBurstMuzzleCheckTimer = 0.f;
	bool bStandBurstFireHeld = false;

	/** World time the muzzle-withhold last resumed fire. StartFiring() fires an instant shot on every call
	 *  with no internal refire guard, so a 10 Hz blocked/clear flicker could resume-fire faster than the
	 *  weapon's cadence. The resume path gates on this + the weapon fire interval. 0 = never resumed. */
	float LastStandBurstResumeFireTime = 0.f;

	// Rate limiter for CornerPeek LoS checks (fire-start and fire-stop), 10 Hz.
	float CornerPeekLosCheckTimer = 0.f;

	// Active peek montage (weak — anim owns it)
	TWeakObjectPtr<UAnimMontage> ActivePeekMontage;

	/** Resolved lean side for the current cover point (replaces the old line-Alpha position).
	 *  None = hunkered / no peek available from this point. */
	ECoverLean CurrentLean = ECoverLean::None;
	/** Cached yaw of GetFireArcForward(CoverData) at reposition commit; avoids per-tick recompute in Phase B. */
	float CachedSlotForwardYaw = 0.f;

	/** Consecutive cover-validity ticks where the point has been detected compromised (flanked / body-exposed).
	 *  Trips the break-cover path once it reaches the tuning CoverCompromiseDebounce threshold.
	 *  Reset to 0 on fresh point arrival or when the compromise condition is NOT met. */
	int32 CoverCompromiseConsecutiveCount = 0;

	/** Combat target the compromise debounce counter is currently accruing against. When the combat
	 *  target changes between validity evals, the counter is reset so a fresh target does not inherit
	 *  violation counts accrued against the old one (prevents retarget-driven instant cover dumps). */
	TWeakObjectPtr<AActor> LastValidatedTarget;

	/** Target-agnostic starvation counter: accrues whenever the current target (any identity) is
	 *  outside the widened arc at a validity eval. Reset on any in-arc eval. Trips the force-
	 *  invalidate path when it reaches CoverArcStarvationEvals, preventing fast-flip deadlocks. */
	int32 ArcStarvationCount = 0;
	float SubSlotLosRecheckTimer = 0.f;
	/** Handle of the cover point occupied on the previous tick — detects a BB-driven cover swap without ExecuteTask re-running. */
	FCoverHandle LastTickCoverHandle;
	uint8 BlockedRecheckHits = 0;

	/** Per-cover cache for the idle branch's hunker position — the edge-aligned variant runs a
	 *  lateral trace march that is static per cover point; recompute only when the handle changes. */
	FCoverHandle CachedIdleHunkerHandle;
	FVector CachedIdleHunkerLoc = FVector::ZeroVector;

	bool bWaitingForFinalApproach = false;
	FVector FinalApproachTarget = FVector::ZeroVector;
	float FinalApproachElapsed = 0.f;
	float LastFinalApproachDist = 0.f;
	float FinalApproachStalledTime = 0.f;

	// Smooth-snap tween state
	bool bSmoothSnapping = false;
	/** Applied by TickSmoothSnap on completion to avoid mid-snap crouch pop. */
	bool bPendingCrouchAfterSnap = false;
	/** Set by gate=945 so reload fires after the return-to-cover snap completes and Crouch fires. */
	bool bPendingReloadAfterSnap = false;
	FVector SmoothSnapStartLoc = FVector::ZeroVector;
	FRotator SmoothSnapStartRot = FRotator::ZeroRotator;
	FVector SmoothSnapTargetLoc = FVector::ZeroVector;
	FRotator SmoothSnapTargetRot = FRotator::ZeroRotator;
	float SmoothSnapElapsed = 0.f;
	/** Static string identifying which call site initiated the active snap (diagnostic only). */
	const TCHAR* SmoothSnapReason = TEXT("");
	/** Euclidean distance at snap start — drives linear vs eased selection and duration scaling. */
	float SmoothSnapInitialDist = 0.f;
	/** Effective tween duration after distance scaling; clamped to [SmoothSnapDuration, 1.0s]. */
	float SmoothSnapEffectiveDuration = 0.f;

	void BeginSmoothSnap(ACompanionCharacter* Companion, const FVector& TargetLoc, const FRotator& TargetRot, bool bShouldCrouchAfter, const TCHAR* Reason = TEXT(""));
	/** Returns true when the snap completes this tick (or is not active). */
	bool TickSmoothSnap(ACompanionCharacter* Companion, float DeltaSeconds);

	/** Finds a neighbouring cover point to shuffle to: same wall as CurrentCover, free, within
	 *  [ShuffleDistanceMin, ShuffleDistanceMax] of the companion. Synchronous C++ pick (mirrors the
	 *  enemy's flank-relocate pattern) — the EQS_Cover_Shuffle asset variant is a P4 A/B.
	 *  Returns an invalid FCover when no candidate qualifies. */
	FCover FindShuffleCover(ACompanionCharacter* Companion, const FCover& CurrentCover, const FVector& ThreatLocation) const;

	/** Commits a cover switch: writes NewCover to the CoverTarget BB key (the plugin's Keep Cover
	 *  Occupied service auto-occupies) and stamps the previously-held point (LastTickCoverHandle,
	 *  if valid and different) as vacated on the reservation subsystem. Updates LastTickCoverHandle. */
	void CommitCoverSwitch(UBlackboardComponent* BB, const FCover& NewCover, AController* Controller);

	/** Returns true and triggers reload if ammo is too low to start a peek action. Caller must return immediately when true. */
	bool TryPrePeekReloadGate(ACompanionCharacter* Companion, const FCoverData& CoverData);

	/** Muzzle-LoS withhold backstop for in-place stand-up fire: pauses the trigger while the muzzle→target
	 *  line is blocked (no shots into our own wall), resuming when clear. Throttled to 10 Hz via
	 *  StandBurstMuzzleCheckTimer; bStandBurstFireHeld latches the withheld state. Used by the plain
	 *  Stand/Quick burst and by StandUpAndReposition Phase A (stand-fire-in-place). No-op if no weapon. */
	void TickStandBurstMuzzleWithhold(ACompanionCharacter* Companion, AActor* Target, TArrayView<AActor* const> IgnoredAttached, float DeltaSeconds, const FCoverData* PeekConeCover = nullptr);

	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	void ResetTaskState(ACompanionCharacter* Companion, UBlackboardComponent* BB, const FCoverHandle& CoverHandle, bool bReleaseSlot, bool bResetPosture = true);

	// --- Reload gate ---

	/** True while a reload triggered by TryPrePeekReloadGate is in flight. */
	bool bReloadGateActive = false;

	/** World time when the reload gate was activated. */
	float ReloadGateStartTime = 0.f;

	/** Multiplier on weapon reload time before the gate is force-cleared (anti-latch). */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float ReloadGateTimeoutMultiplier = 1.5f;

	/** Grace seconds added on top of (ReloadTime * Multiplier) before force-clear. */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ReloadGateTimeoutGrace = 0.5f;

	// --- Cover idle watchdog ---

	/** World time of the last committed peek action in BRANCH 0. */
	float LastDecisionTime = 0.f;

	/** Seconds without a peek decision before the watchdog force-re-rolls. */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float CoverIdleWatchdogSeconds = 8.0f;
};
