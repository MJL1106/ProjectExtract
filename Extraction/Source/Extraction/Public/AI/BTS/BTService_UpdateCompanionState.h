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
	 * target, ready-threat, revive, takedown, traversal, route, companion command, DBNO); Tier 1
	 * (F3) watches the nearest visible/lingering threat; Tier 2 (F1) is ambient path-look-ahead or
	 * idle attention-yaw facing. Never writes the combat-target key, never fires, never flips
	 * posture to Combat.
	 */
	void UpdateNonCombatFacing(ACompanionAIController& Controller, ACompanionCharacter& Companion,
		UBlackboardComponent& BB, APawn* PlayerPawn, const UCompanionTuningDataAsset* Tuning,
		bool bHasTarget, bool bReadyOnlyThreat, const AEnemyCharacter* WatchCandidateEnemy,
		float DeltaSeconds);

	/** Updates WatchThreatLocation/linger from Candidate's LoS; returns true while the watch is
	 *  live (visible this tick, or still within its linger window). */
	bool ComputeWatchThreat(const ACompanionCharacter& Companion, const AEnemyCharacter* Candidate,
		const UCompanionTuningDataAsset* Tuning, float DeltaSeconds);

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
