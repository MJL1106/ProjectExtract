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

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

	/** Always sprint (used for revive branch) */
	UPROPERTY(EditAnywhere, Category = "Follow")
	bool bSprintToTarget = false;

	/** Optional EQS query for picking a follow slot near the player. If unset, falls back to formation-offset target. */
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

	/** ~1Hz throttle for the sprint-mode rescue-approach diagnostic log. */
	float SprintLogAccumulator = 0.f;

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
	 *  candidate window. Only a PRIMARY companion ever dispatches the query, so the window is a plain
	 *  best-first fill — a non-empty result set always yields at least one candidate. */
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

	/** Re-acquire band (cm) below UCompanionTuningDataAsset::SecondaryLeaderLeashDistance. The leash
	 *  is latched, so without a release band a leader hovering on the boundary would swap the whole
	 *  follow frame (anchor, pursuit target, sprint mirror) between leader and player every tick. */
	static constexpr float LeaderLeashReleaseHysteresis = 300.f;

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
