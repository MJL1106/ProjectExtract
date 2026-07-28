// BT service — ticks to update all companion blackboard keys (DBNO, combat target, etc).

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Companion/CompanionTypes.h"
#include "BTService_UpdateCompanionState.generated.h"

class ACompanionAIController;
class ACompanionCharacter;
class AEnemyCharacter;
class APawn;
class UBlackboardComponent;
class UCompanionTuningDataAsset;
class UEnemyDirectorSubsystem;
struct FCollisionQueryParams;

UCLASS()
class EXTRACTION_API UBTService_UpdateCompanionState : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateCompanionState();

	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerNeedsReviveKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasCoverPositionKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector ReviveWindowOpenKey;

	/**
	 * New-system cover point (FCover). Resolved by name "CoverTarget" in InitializeFromAsset so no BT
	 * asset edit is required. Read (not written) here — used to detect "my own wall is blocking my
	 * eye-line" so an LoS block while genuinely in cover keeps the target instead of clearing it.
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CoverTargetKey;

	/** Toggle verbose perception/target logs under LogCompanionAI. Enable per-instance in BT. */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDebugLogging = false;

	UPROPERTY(EditAnywhere, Category = "Posture", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float ExploreReturnDelay = 3.f;

	/** Seconds between downtime chatter attempts (Following/IdleAmbient). The bark subsystem's lull
	 *  gate and per-type cooldowns still apply on top, so most attempts near activity are dropped. */
	UPROPERTY(EditAnywhere, Category = "Barks", meta = (ClampMin = "5.0"))
	float AmbientAttemptIntervalSeconds = 20.f;

	/** Grace before clearing a no-cover combat target on sustained LoS block — lets the companion reposition to regain a shot instead of thrashing on brief occlusion. */
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float CombatTargetLosGraceSeconds = 3.0f;

private:
	float OutOfCombatTimer = 0.f;
	/** True once the weapon has been lowered since the last target/threat was lost — edge guard for
	 *  the immediate lower, robust across branch switches (stealth re-pin -> combat decay). */
	bool bLoweredOnTargetLoss = false;
	bool bWasLosBlocked = false;
	/** Accumulated time the no-cover combat target has been LoS-blocked; cleared on LoS-clear or active cover slot. */
	float OpenLosBlockedTime = 0.f;
	TWeakObjectPtr<AActor> PrevCombatTarget;
	/** One-time guard so the CoverTarget-key-unresolved warning fires at most once per instance. */
	bool bLoggedCoverKeyResolveFail = false;
	/** One-time guard so the ReviveWindowOpen-key-unresolved warning fires at most once per instance. */
	bool bLoggedReviveKeyResolveFail = false;

	/** Accumulated safe-seconds (no nearby threats) while the player is DBNO. Resets on threat detection. */
	float ReviveSafeAccumulator = 0.f;

	/** Edge-detect for the window open/close diagnostic log. */
	bool bLastReviveWindowOpen = false;

	// --- Bark edge/pacing state (VO routes through ACompanionCharacter::Bark; the subsystem owns
	// cooldowns/arbitration — these members only stop re-fires of one-shot reactions and pace the
	// optional attempts) ---

	/** PlayerDownReaction fired for the current player DBNO stint. */
	bool bPlayerDownBarked = false;

	/** PlayerHurtWarning fired; re-arms once the player recovers above the re-arm threshold. */
	bool bPlayerHurtBarked = false;

	/** World time of the next ambient Following/IdleAmbient attempt; <0 = not yet initialised. */
	float NextAmbientBarkTime = -1.f;

	/** World time of the next under-pressure Reassurance attempt. */
	float NextReassuranceBarkTime = 0.f;

	/** World time of the last body-charger target steal — gates the steal to BodyChargerStealCooldown. */
	float LastBodyChargerStealTime = -1.f;

	// --- F4a stealth crouch-mirror state (mirrors the player's own crouch/uncrouch with a
	// randomized human-reaction delay; see UpdateStealthCrouchMirror) ---

	/** Last observed player bIsCrouched — an edge here rolls a fresh MirrorDelayRemaining. */
	bool bMirrorLastPlayerCrouched = false;

	/** Countdown (seconds) until the mirrored crouch/uncrouch actually applies. Rolled fresh on
	 *  every player crouch-state edge; quantizes to this service's ~0.25s tick (acceptable). */
	float MirrorDelayRemaining = 0.f;

	void UpdateStealthCrouchMirror(ACompanionCharacter& Companion, const APawn* PlayerPawn,
		const UCompanionTuningDataAsset* Tuning, float DeltaSeconds);

	// --- Post-combat overwatch state (weapon held ready on the chokepoint/bearing the threat came
	// from after the last kill; see UpdatePostCombatOverwatch/ComputeOverwatchAimPoint) ---

	/** Where the FIRST target of the current fight stood when acquired. Set once per fight,
	 *  cleared with the rest of the fight memory on the Exploration flip / stealth re-pin. */
	FVector FightFirstContactLocation = FVector::ZeroVector;
	bool bHasFightFirstContact = false;

	/** Rolling location of the current combat target — the freshest "threat was here" point. */
	FVector LastThreatLocation = FVector::ZeroVector;
	bool bHasLastThreatLocation = false;

	/** True while the post-fight hold owns aim/focus. Suspends the immediate weapon-lower and the
	 *  Exploration decay accrual in the combat-decay branch, and Tier-0-yields UpdateNonCombatFacing. */
	bool bOverwatchActive = false;
	FVector OverwatchAimPoint = FVector::ZeroVector;
	float OverwatchElapsed = 0.f;
	/** Accumulated seconds of sustained self-movement — a real move (formation catch-up, command)
	 *  breaks the hold rather than back-walking the companion while it aims. */
	float OverwatchMovingTime = 0.f;

	/** Seconds since the aim point was last re-resolved. The point used to be computed once on entry
	 *  and re-asserted verbatim for the whole window, so it went stale the moment the ally or the
	 *  fight moved — and under a wave hold (max-time cap waived) it never expired to correct itself. */
	float OverwatchAimRefreshTimer = 0.f;

	/** Consecutive refreshes that failed to resolve an aim point. Debounce: a refusal is a single
	 *  trace sample on a ~1s cadence, so ending the hold on the first one let anything transient in
	 *  the firing line cycle overwatch END/START. Reset by any successful refresh and by the end. */
	int32 OverwatchRefreshFailures = 0;

	/** Runs the post-fight hold inside the combat-decay branch: enters on the fresh target-loss
	 *  edge, faces the computed aim point weapon-up each tick, breaks on player-left / new owner /
	 *  self-movement / timer cap. Returns true while it owns aim/focus this tick. */
	bool UpdatePostCombatOverwatch(ACompanionAIController& Controller, ACompanionCharacter& Companion,
		const APawn* PlayerPawn, const UCompanionTuningDataAsset* Tuning,
		bool bTakedownOwnsAim, bool bPlayerDBNO, float DeltaSeconds);

	/** Aim-point pick: nearest same-floor door on the portal path toward the threat memory (LoS
	 *  checked), else the last-threat/first-contact point, else a point along the threat bearing.
	 *  Returns false when no usable memory exists (overwatch is skipped). */
	bool ComputeOverwatchAimPoint(const ACompanionCharacter& Companion,
		const UCompanionTuningDataAsset* Tuning, FVector& OutAimPoint) const;

	/** Threat point the aim pick is measured against: freshest threat memory, else first contact,
	 *  else a bearing push-out, else the director's wave spawn reference. False = no memory at all. */
	bool ResolveOverwatchThreatRef(const ACompanionCharacter& Companion,
		const UCompanionTuningDataAsset* Tuning, FVector& OutThreatRef) const;

	/** Chokepoint pick: nearest same-storey door on the portal path toward ThreatRef with an
	 *  unobstructed eye-line. False = no usable door, caller falls through to the direct aim. */
	bool PickOverwatchDoorAim(const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning,
		const FVector& ThreatRef, const FCollisionQueryParams& LosParams, FVector& OutAim) const;

	/** Re-resolves OverwatchAimPoint on the tuning's refresh cadence while the hold is active.
	 *  Returns false only after OverwatchRefreshFailuresToEnd CONSECUTIVE failures — the caller then
	 *  ends the hold rather than keep asserting a world point the ally can no longer justify. */
	bool RefreshOverwatchAimPoint(const ACompanionCharacter& Companion,
		const UCompanionTuningDataAsset* Tuning, float DeltaSeconds);

	/** Drops the scripted-aim raise, releases the Gameplay focal if it is still the one we wrote, and
	 *  resets the hold state. The focal clear is ours to do: under a wave hold nothing downstream does
	 *  it (the decay branch only restores low-ready and UpdateNonCombatFacing Tier-0-yields on the
	 *  hold), so the anim's focal-driven aim kept pointing the ally at the dead overwatch point.
	 *  bAnotherOwnerHasFocus: when true the compare-clear is skipped — a SetFocus(Actor) resolves to
	 *  that actor's world location, so a near-stationary enemy can Equals-match the overwatch point
	 *  and wrongly clear the new owner's focus on the safety-end path. */
	void EndPostCombatOverwatch(ACompanionAIController& Controller, ACompanionCharacter& Companion,
		bool bAnotherOwnerHasFocus);

	/** Clears first-contact/last-threat memory — the fight is fully over (Exploration flip or
	 *  stealth re-pin), the next acquisition starts a new fight. */
	void ResetFightThreatMemory();

	// --- Wave hold (stay combat-ready at cover for the length of a finite Director wave) ---

	/** True once this ally has engaged during the CURRENT wave. Latched for the wave's duration
	 *  rather than read straight off bHasFightFirstContact, because the Exploration flip wipes fight
	 *  memory and that flip is exactly what runs while the ally is outside the leash — keying off it
	 *  directly would stop the ally ever re-holding after it had caught back up. */
	bool bEngagedThisWave = false;

	/** Edge-log guard for the wave-hold transition, and the edge that triggers the stand-up. */
	bool bLastWaveHold = false;

	/** Seconds with no combat target AND no LoS-verified alerted threat while a wave is live. Past
	 *  the tuning's WaveHoldQuietReleaseSeconds the hold drops so the ally stops standing in an
	 *  emptied room. Verified knowledge only: fed the raw radius-only flag it never accrued at all
	 *  for an ally behind a wall, and the release could never fire.
	 *  Unwinds in real time rather than snapping to zero, and saturates at release + the tuning's
	 *  WaveHoldQuietRearmSeconds — see UpdateWaveHoldQuietTimer for why the hysteresis is load-bearing. */
	float WaveHoldQuietTimer = 0.f;

	/** Seconds the hold has been active AND blind (no target, no verified threat) during the current
	 *  wave. Feeds the hard ceiling. Gated on blindness, not on hold time: a healthy defence is held
	 *  for nearly its whole 40-90s run, so a plain total tripped the ceiling mid-wave and disabled the
	 *  hold for the rest of it. Zeroed the moment knowledge goes live, like the quiet timer. */
	float WaveHoldBlindHeldTime = 0.f;

	/** Latched once the ceiling trips: the hold stays released for the REST of the wave. Without the
	 *  latch the first knowing tick zeroes the blind timer and the hold re-arms immediately, flickering
	 *  the ally between hold and follow instead of letting it walk back to the player. An ally that has
	 *  spent that long blind-holding has demonstrably lost the fight. Cleared with the per-wave block. */
	bool bWaveHoldCeilingReleased = false;

	/** Director wave id the per-wave block above belongs to. The reset cannot key on "wave inactive"
	 *  alone: TickNode returns early for a DBNO companion, so an ally downed during the defence and
	 *  revived after the next wave started never observes an inactive tick, and the ceiling latch
	 *  survived into the new wave — which then ran hold-less from its first tick, silently. */
	FName WaveHoldWaveId = NAME_None;

	/** Edge-log guard for the revive-claim refusal. The refused state persists for as long as the
	 *  other ally holds the claim, so an un-edged log would spam at service cadence for the whole
	 *  revive. */
	bool bLastReviveClaimRefused = false;

	/** True when the wave-hold overwatch extension owned aim last tick. Used to detect the release
	 *  edge so we can restore low-ready and clear focus (the decay-branch lower can't run in
	 *  Exploration posture). */
	bool bWaveOverwatchOwnedAimLastTick = false;

	/** Recomputes the wave hold and mirrors it onto the companion for the combat teardown and the
	 *  follow task to read. Deliberately a live state, not a latch: a wave ending or being cancelled
	 *  releases it on the next tick with no extra plumbing. Returns true while held.
	 *  bThreatKnown MUST be line-of-sight verified (bReadyOnlyThreat, not the raw radius-only
	 *  bHasAlertedThreat) — a through-wall signal resets the quiet timer every tick and pins the hold
	 *  on forever for any ally without an eye-line to the fight. */
	bool UpdateWaveHold(ACompanionCharacter& Companion, const APawn* PlayerPawn,
		const UCompanionTuningDataAsset* Tuning, bool bHasTarget, bool bThreatKnown, float DeltaSeconds);

	/** Advances WaveHoldQuietTimer for this tick and returns true while the quiet release is tripped.
	 *  Decay-with-saturation rather than a snap to zero, so re-arming the hold costs a sustained
	 *  knowledge window: re-arming calls StopMovement() on the follow task, and a single knowing tick
	 *  re-arming it turned a flickering eye-line into a stop-go stutter across the whole defence.
	 *  bWaveEngaged gates accrual: the pre-engagement window (spawn delay + travel) is blind by
	 *  definition, and accruing there banks enough time to trip the release before first contact. */
	bool UpdateWaveHoldQuietTimer(bool bKnowledgeLive, bool bWaveEngaged,
		const UCompanionTuningDataAsset* Tuning, float DeltaSeconds);

	/** Stale-combat backstop: true when no fight has been reported to the director for the tuning's
	 *  window AND no live detected enemy sits inside the leash. Covers stuck enemies and blocked-spawn
	 *  soft-locks; the proximity gate stops a distant holdout pinning the signal forever. */
	bool IsWaveCombatStale(const ACompanionCharacter& Companion, const UCompanionTuningDataAsset* Tuning,
		const UEnemyDirectorSubsystem* Director) const;

	// --- F3 watch-threat state (nearest visible-or-lingering enemy the companion watches without
	// engaging; see ComputeWatchThreat/ApplyWatchFacing) ---

	FVector WatchThreatLocation = FVector::ZeroVector;
	float WatchThreatLingerRemaining = 0.f;

	/** The enemy the linger window remembers. A corpse must drop the watch immediately (a silent
	 *  player takedown of the watched enemy otherwise pins a raised weapon on the takedown spot
	 *  for the whole linger); only a live enemy earns retreat memory. */
	TWeakObjectPtr<const AEnemyCharacter> WatchedEnemy;

	/** True while the watch-only aim/low-ready stance is applied — edge-guards the SetLowReadyAim/
	 *  SetScriptedAim calls so they fire once on entry and once on drop, not every tick. */
	bool bWatchStanceApplied = false;

	/** IsStealthActive() at the moment the watch stance was applied — re-checked every tick so a
	 *  mode flip mid-watch (Normal->Stealth or back) re-applies the correct stance instead of
	 *  leaking the old one for the rest of the watch. */
	bool bWatchStanceStealth = false;

	// --- F1 ambient facing state (idle scan-glance roll + focal dedup; see
	// ComputeAttentionYaw/ApplyAmbientFacing/SetAmbientFocalDeduped) ---

	float AmbientScanTimer = 0.f;
	float AmbientScanNextInterval = 0.f;
	float AmbientScanOffsetDeg = 0.f;
	FVector LastAmbientFocalPoint = FVector::ZeroVector;

	/**
	 * Arbitrates non-combat facing/stance every tick, after the posture chain settles. Priority
	 * ladder: Tier 0 yields to any system that already owns aim/focus/stance this tick (combat
	 * target, ready-threat, revive, takedown, traversal, route, moving/non-Loot command, DBNO); Tier 1
	 * (F3) watches the nearest visible/lingering threat; Tier 2 (F1) is ambient path-look-ahead or
	 * idle attention-yaw facing. Never writes the combat-target key, never fires, never flips
	 * posture to Combat.
	 */
	void UpdateNonCombatFacing(ACompanionAIController& Controller, ACompanionCharacter& Companion,
		UBlackboardComponent& BB, APawn* PlayerPawn, const UCompanionTuningDataAsset* Tuning,
		bool bHasTarget, bool bReadyOnlyThreat, const AEnemyCharacter* WatchCandidateEnemy,
		float DeltaSeconds);

	/** Updates WatchThreatLocation/linger from Candidate's LoS; returns true while the watch is
	 *  live (visible this tick, still within its linger window, or held by the controller's
	 *  fresh alerted-threat signal — the fight-aware-follow no-eye-line fallback). */
	bool ComputeWatchThreat(const ACompanionAIController& Controller, const ACompanionCharacter& Companion,
		const AEnemyCharacter* Candidate, const UCompanionTuningDataAsset* Tuning, float DeltaSeconds);

	/** Faces WatchThreatLocation every tick; applies the Normal (weapon-raise) or Stealth
	 *  (low-profile rotate-only) stance once on the rising edge (bWatchStanceApplied). */
	void ApplyWatchFacing(ACompanionAIController& Controller, ACompanionCharacter& Companion);

	/** Base idle attention yaw (player 2D velocity heading, else view yaw) plus the rolled idle
	 *  scan-glance offset — re-rolls the offset/interval when the scan timer expires. */
	float ComputeAttentionYaw(const APawn& PlayerPawn, const UCompanionTuningDataAsset* Tuning, float DeltaSeconds);

	/** Path look-ahead while moving; idle attention-yaw (with don't-turn-away guard + dead zone)
	 *  otherwise. No-ops when bAmbientFacingEnabled is off. */
	void ApplyAmbientFacing(ACompanionAIController& Controller, const ACompanionCharacter& Companion,
		const APawn* PlayerPawn, const UCompanionTuningDataAsset* Tuning, float DeltaSeconds);

	/** Skips a redundant SetFocalPoint when Point hasn't meaningfully moved since the last call. */
	void SetAmbientFocalDeduped(ACompanionAIController& Controller, const FVector& Point);
};
