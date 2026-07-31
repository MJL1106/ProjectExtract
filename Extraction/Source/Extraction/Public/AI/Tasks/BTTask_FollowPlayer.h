// BT task — companion follows its LEADER in formation or sprints to reach the player.
// The leader is the player for the primary companion (so the primary's path is unchanged by the
// chain) and the PRIMARY COMPANION for a non-primary one (the armed extractee VIP): player <-
// companion <- VIP. Both companions previously anchored on the same player-keyed maths and moved in
// lockstep into each other. See ResolveFollowLeader for the fallbacks back to the player.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Companion/CompanionTypes.h"
#include "BTTask_FollowPlayer.generated.h"

class UCompanionTuningDataAsset;
class ACompanionAIController;
class ACompanionCharacter;
class ACharacter;
class UEnvQuery;
struct FEnvQueryResult;

UCLASS()
class EXTRACTION_API UBTTask_FollowPlayer : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_FollowPlayer();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;

	/** Re-acquire band (cm) below UCompanionTuningDataAsset::SecondaryLeaderLeashDistance. The leash
	 *  is latched, so without a release band a leader hovering on the boundary would swap the whole
	 *  follow frame (anchor, pursuit target, sprint mirror) between leader and player every tick.
	 *  Public because ACompanionAIController::GetFollowLeader adds it ON TOP of the break distance for
	 *  its own combat-branch backstop — sharing the one constant is what keeps that backstop provably
	 *  outside this task's latched band instead of contending with it. */
	static constexpr float LeaderLeashReleaseHysteresis = 300.f;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

	/** Always sprint (used for revive branch) */
	UPROPERTY(EditAnywhere, Category = "Follow")
	bool bSprintToTarget = false;

	/** Optional EQS query for picking a follow slot near the companion's leader — anchor it on
	 *  UEnvQueryContext_FollowLeader, not the player, or a secondary gets the primary's slots. If
	 *  unset, falls back to the formation-offset target. */
	UPROPERTY(EditAnywhere, Category = "EQS")
	TObjectPtr<UEnvQuery> FollowSlotQuery;

	/** Minimum seconds between EQS follow-slot requests. */
	UPROPERTY(EditAnywhere, Category = "EQS", meta = (ClampMin = "0.1"))
	float EqsQueryInterval = 0.5f;

private:
	FVector LastMoveTarget = FVector::ZeroVector;
	bool bIsIdling = false;

	/** Edge guard for the wave hold's one-shot StopMovement — see ACompanionCharacter::IsWaveHoldActive.
	 *  Without it the hold would re-issue StopMovement every tick for the length of the wave. */
	bool bHoldingForWave = false;

	/** Edge guard for the commanded cover hold's one-shot StopMovement — mirrors bHoldingForWave
	 *  for ACompanionCharacter::IsCommandedCoverHoldActive. Separate because the wave hold clears
	 *  bHoldingForWave every tick it is inactive, which would re-trigger the commanded hold's
	 *  StopMovement if they shared the flag. */
	bool bHoldingForCoverCommand = false;

	/** ~1Hz throttle for the sprint-mode rescue-approach diagnostic log. */
	float SprintLogAccumulator = 0.f;

	// --- Rescue-approach pace (bSprintToTarget branch; see UpdateRescueSprint) ---

	/** Latched sprint decision for the current rescue approach. Backs the hysteresis: without it the
	 *  three thresholds are single edges and a companion running one of them flickers the sprint flag,
	 *  which drives BOTH MaxWalkSpeed and the anim instance's sprint state. */
	bool bRescueSprinting = false;

	/** Release bands for the rescue-sprint hysteresis, same idiom as the stealth catch-up ladder's
	 *  split enter/exit thresholds above. Once sprinting, EVERY trigger has to clear by its band
	 *  before the approach drops back to a jog. */
	static constexpr float RescueSprintBleedoutReleaseMargin = 5.f;   // seconds ABOVE the bleedout threshold
	static constexpr float RescueSprintDistanceReleaseScale = 0.75f;  // fraction OF the distance threshold
	static constexpr float RescueSprintThreatReleaseScale = 1.25f;    // multiple OF the threat radius

	/** Re-decides the rescue approach's pace this tick and returns the sprint flag to apply. Sprint
	 *  when the rescue is genuinely urgent — bleedout nearly out, a long way still to run, or a
	 *  hostile standing over the body — and jog otherwise. The old branch called SetSprinting(true)
	 *  unconditionally every tick, so the companion panic-sprinted a rescue with 80 seconds of
	 *  bleedout left and an empty room. The threat term is READ, never scanned: the state service
	 *  already sweeps the body every tick and publishes its nearest-hostile distance on the companion. */
	bool UpdateRescueSprint(ACompanionCharacter& Companion, const UCompanionTuningDataAsset& Tuning,
		float DistToPlayer, AActor& Player);

	/** Latched sprint decision for the formation catch-up. Without a release band, bWantSprint
	 *  flickers per-frame at SprintDistanceThreshold when any non-combat gameplay focal is live:
	 *  the sprint clears the focal (facing yields to travel) and raises MaxWalkSpeed, closing the
	 *  gap fast; the companion drops below the threshold within a frame or two and un-sprints; the
	 *  focal re-asserts, the strafe clamp re-engages at 275, the companion falls behind again, and
	 *  the cycle repeats. Enter at SprintDistanceThreshold, release at 0.6x. */
	bool bFormationSprinting = false;

	/** Release band fraction for the formation sprint latch. Sprint enters at
	 *  SprintDistanceThreshold and releases at this fraction of it. Matches the stealth catch-up
	 *  ladder's EffSprintBreak floor idiom already in this file (~lines 143-162). */
	static constexpr float FormationSprintReleaseFraction = 0.6f;

	/** Accumulates DeltaSeconds in the formation branch; gates the blocked-move re-issue below so a
	 *  normal brief Idle blip (arrival, replan) doesn't spam repath. */
	float BlockedMoveReissueAccumulator = 0.f;

	/** Minimum seconds between forced LastMoveTarget resets when the path-following component
	 *  reports Idle while the companion is still outside the formation's acceptance radius (e.g.
	 *  the player is standing exactly on the anchor point, so MoveToLocation can't path there). */
	static constexpr float BlockedMoveReissueCooldown = 0.5f;

	/** Mode seen last tick — a change drops the idle latch so the new formation applies immediately
	 *  (e.g. switching to Combat while stationary sends the companion to the lead point). */
	ECompanionMode LastSeenMode = ECompanionMode::Normal;

	// EQS slot state
	bool bEqsQueryInProgress = false;
	bool bHasEqsTarget = false;
	float TimeSinceLastEqs = 0.f;
	FVector EqsTarget = FVector::ZeroVector;

	// Fight-aware follow (see ACompanionAIController::NoteAlertedThreat): stamped at query
	// dispatch when the live-fight signal is fresh; the callback then prefers the best-scored
	// slot with eye-line toward the threat, so a no-LoS companion drifts to where it can see
	// the fight instead of cycling oblivious formation slots.
	bool bFightBiasQuery = false;
	FVector FightBiasThreatLocation = FVector::ZeroVector;

	/** Cap on per-callback slot eye-line traces (best-scored slots first). Also sizes the accepted
	 *  candidate window. Every slot the query returns is already anchored on this companion's own
	 *  leader, so the window is a plain best-first fill — a non-empty result set always yields at
	 *  least one candidate. */
	static constexpr int32 MaxFightBiasSlotTraces = 8;

	// Leader floor-transit detector (see UpdateLeaderZTransit). Envelope follower over the
	// leader's grounded vertical rate; while hot, follow pursues the leader directly instead
	// of trusting the EQS slot / formation anchor.
	float LastLeaderZ = 0.f;
	bool bHasLeaderZSample = false;
	float LeaderZTransitEnvelope = 0.f;
	bool bWasPursuingLeader = false;

	/** Envelope drain rate (cm/s per second) — sets how long pursuit holds after the leader settles. */
	static constexpr float ZTransitEnvelopeDecay = 200.f;

	/** Envelope rise rate (cm/s per second) — only a SUSTAINED vertical rate (~0.1s+) can cross the
	 *  pursuit threshold. Without it a single-tick Z blip (curb step-up, ground-height adjustment)
	 *  reads as hundreds of cm/s and trips seconds of pursuit. Real floor drops that happen in one
	 *  frame are covered by the off-level pursuit arm instead. */
	static constexpr float ZTransitEnvelopeAttack = 400.f;

	/** Single-tick rate cap — bounds one-frame dZ/dt spikes (ledge-drop landings) so the envelope
	 *  math stays in sane range regardless of frame time. */
	static constexpr float ZTransitRateClamp = 600.f;

	// --- Leader chain (non-primary companion trails the primary; see ResolveFollowLeader) ---

	// LeaderLeashReleaseHysteresis is declared public above — shared with the controller's backstop.

	/** Min seconds between primary-companion lookups. GetPrimaryCompanion is a TActorIterator over
	 *  the level — it may only run on a cache MISS, and then only this often. */
	static constexpr float LeaderRescanInterval = 2.f;

	/** Leader 2D speed (cm/s) that latches it "travelling" — the anchor then trails its travel
	 *  direction instead of bearing-holding. Enter/exit are split because the leader is a companion
	 *  that ally soft-separation shoves around: a single threshold flip-flops the two anchor formulas
	 *  against each other. Only the leader-anchored path uses these; the player-anchored path keeps
	 *  its original raw speed comparison. */
	static constexpr float LeaderMovingSpeedEnter = 150.f;
	static constexpr float LeaderMovingSpeedExit = 60.f;

	/** Cached primary companion for a secondary's leader chain. Weak — the primary can die, be
	 *  destroyed or never exist; capability is re-tested every tick, not just at scan time. */
	TWeakObjectPtr<ACompanionCharacter> CachedLeader;

	/** Leader used last tick (player OR primary companion). A change clears the move dedup, the idle
	 *  latch, the travel latch and the Z sample — a cross-floor leader swap must not inject a fake
	 *  transit spike into the envelope. */
	TWeakObjectPtr<AActor> LastFollowLeader;

	/** Accumulates only while CachedLeader is empty; pinned at LeaderRescanInterval while it is hot
	 *  so the first miss rescans on the same tick (same idiom as TimeSinceLastEqs). */
	float TimeSinceLeaderScan = 0.f;

	/** Latched: the leader has been commanded too far from the player to anchor on. See
	 *  LeaderLeashReleaseHysteresis. */
	bool bLeaderLeashBroken = false;

	/** Latched leader-is-travelling state. See LeaderMovingSpeedEnter/Exit. */
	bool bLeaderMoving = false;

	void OnFollowQueryFinished(TSharedPtr<FEnvQueryResult> Result);
	void UpdateLeaderZTransit(const ACharacter* LeaderChar, float LeaderZ, float DeltaSeconds);

	/** The actor this companion forms up on this tick — never null. The player for a primary
	 *  companion (which is what keeps every leader-keyed line in TickTask identical to the pre-chain
	 *  behaviour), and the primary companion for a secondary — falling back to the player whenever
	 *  the primary is invalid, unpossessed, DBNO, dead, leashed away, or the tuning switch is off. */
	AActor* ResolveFollowLeader(ACompanionCharacter& Companion, AActor& Player,
		const UCompanionTuningDataAsset& Tuning, float DeltaSeconds);

	UPROPERTY()
	TObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;

	// Cached at ExecuteTask, cleared at OnTaskFinished. bCreateNodeInstance = true keeps
	// per-instance state safe; the casts in TickTask were ~120 Hz of churn otherwise.
	TWeakObjectPtr<ACompanionAIController> CachedController;
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;
};
