// BT task — companion follows player in formation or sprints to reach them.

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

	// Ally spacing: stamped at query dispatch for a non-primary companion (the armed extractee).
	// The follow query is player-anchored and identical for both companions, so the callback must
	// drop slots sitting on the primary's — AND slots on the primary's side of the player — before
	// either pass picks one. Without the side test a surviving slot is merely far from the primary,
	// not mirrored, so the wedge only existed on the reject-everything fallback path.
	bool bAllySpacingQuery = false;

	/** Half-plane the secondary's slots must sit in — stamped at dispatch as sign(mirrored OffsetRight)
	 *  rather than assumed +1. FormationOffsetRight is a live serialised override in the tuning asset;
	 *  flipping it negative flips the anchor's side, and a hardcoded assumption would leave the filter
	 *  rejecting exactly the side the anchor now wants. Zero offset means no side, so no test. */
	float AllySlotSideSign = 0.f;

	/** Side basis latched from the player's last TRAVEL direction, reused by the side test while they
	 *  stand still. Dropping the test at rest dropped the wedge at rest: spacing alone only proves a
	 *  slot is AllyFollowSlotMinSpacing from the primary, never that it is mirrored, so the secondary
	 *  took the slot beside the primary and physically walked across the player's back (the formation
	 *  branch keeps running at rest, and a cross-body slot clears the 200cm re-issue dedup). Latched
	 *  travel is not actor-forward — it doesn't rotate when the player turns on the spot, so the
	 *  accepted side can't swing underneath the stationary bearing-hold anchor. */
	FVector LastMovingSideRight = FVector::ZeroVector;

	/** Latches while the ally filter is rejecting every slot, so OnFollowQueryFinished logs that state
	 *  on entry only — geometry that stays unusable would otherwise log twice a second all level. */
	bool bAllySpacingRejectedAll = false;

	/** Wrong-side tolerance (cm) for the ally side test. Slots within this band of the player's
	 *  centreline pass either way: a corridor narrower than the formation offset has no clearly-sided
	 *  slots at all, and rejecting them there would pin the secondary to the formation anchor. */
	static constexpr float AllySlotSideDeadZone = 100.f;

	/** Cap on per-callback slot eye-line traces (best-scored slots first). Also sizes the accepted
	 *  candidate window — the ally filter runs during the fill, so this bounds slots KEPT, never
	 *  slots examined. A trace budget must not double as a distance-check budget: pre-truncating to
	 *  it made "all 8 rejected" a steady state, since EQS sorts best-first and the top slots cluster
	 *  exactly where the primary is standing. */
	static constexpr int32 MaxFightBiasSlotTraces = 8;

	// Player floor-transit detector (see UpdatePlayerZTransit). Envelope follower over the
	// player's grounded vertical rate; while hot, follow pursues the player directly instead
	// of trusting the EQS slot / formation anchor.
	float LastPlayerZ = 0.f;
	bool bHasPlayerZSample = false;
	float PlayerZTransitEnvelope = 0.f;
	bool bWasPursuingPlayer = false;

	/** Envelope drain rate (cm/s per second) — sets how long pursuit holds after the player settles. */
	static constexpr float ZTransitEnvelopeDecay = 200.f;

	/** Envelope rise rate (cm/s per second) — only a SUSTAINED vertical rate (~0.1s+) can cross the
	 *  pursuit threshold. Without it a single-tick Z blip (curb step-up, ground-height adjustment)
	 *  reads as hundreds of cm/s and trips seconds of pursuit. Real floor drops that happen in one
	 *  frame are covered by the off-level pursuit arm instead. */
	static constexpr float ZTransitEnvelopeAttack = 400.f;

	/** Single-tick rate cap — bounds one-frame dZ/dt spikes (ledge-drop landings) so the envelope
	 *  math stays in sane range regardless of frame time. */
	static constexpr float ZTransitRateClamp = 600.f;

	void OnFollowQueryFinished(TSharedPtr<FEnvQueryResult> Result);
	void UpdatePlayerZTransit(const ACharacter* PlayerChar, float PlayerZ, float DeltaSeconds);

	UPROPERTY()
	TObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;

	// Cached at ExecuteTask, cleared at OnTaskFinished. bCreateNodeInstance = true keeps
	// per-instance state safe; the casts in TickTask were ~120 Hz of churn otherwise.
	TWeakObjectPtr<ACompanionAIController> CachedController;
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;
};
