// AI companion character — follows player, engages enemies, revives downed teammates.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "AI/SearchRoomExposure.h"
#include "Movement/TraversalTypes.h"
#include "Companion/CompanionTypes.h"
#include "Companion/CompanionCommandTypes.h"
#include "AIShooterInterface.h"
#include "Character/ExtractionPlayerInterface.h"
#include "CoverSystemPublicData.h"
#include "CompanionCharacter.generated.h"

enum class ECompanionBarkType : uint8;
class UCompanionBarkSetData;
class USoundBase;
class UHealthComponent;
class UFootstepNoiseComponent;
class USuppressionComponent;
class UCoverPoseComponent;
class AWeaponBase;
class UCompanionAnimInstance;
class UTraversalComponent;
class UWidgetComponent;
class UUserWidget;
class AExtractionPlayer;
class UCompanionTuningDataAsset;
class UEnemyGrenadierComponent;
class AEnemyCharacter;

DECLARE_LOG_CATEGORY_EXTERN(LogCompanion, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCompanionPostureChanged, ECompanionPosture, NewPosture);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCompanionModeChanged, ECompanionMode, NewMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLowReadyAimChanged, bool, bIsLowReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCommandedTakedownFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCommandedTakedownStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCompanionDownedStateChanged, bool, bDowned, float, BleedoutDuration);

UCLASS(Blueprintable)
class EXTRACTION_API ACompanionCharacter : public ACharacter, public IExtractionPlayerInterface, public IGameplayTagAssetInterface, public IAIShooterInterface, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ACompanionCharacter();

	virtual void Tick(float DeltaTime) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	// --- IGameplayTagAssetInterface ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	// --- IAIShooterInterface ---
	virtual AActor* GetAIAimTarget() const override { return CurrentAimTarget.Get(); }
	virtual float GetAIAimSpreadDegrees() const override { return GetCurrentInaccuracy(); }
	virtual FVector GetAimPointForTarget(const AActor* Target) const override;

	// --- IGenericTeamAgentInterface ---
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(0); }

	// --- Barks ---

	/** Routes a companion voice line through the world's shared bark channel. Safe to call from
	 *  anywhere, any frequency — the subsystem owns cooldowns and one-voice arbitration. Context
	 *  filters tagged variants (e.g. an archetype or direction the line names). */
	void Bark(ECompanionBarkType Type, FName Context = NAME_None) const;

	/** Scripted one-off line (dialogue trigger volumes, the VIP rescue exchange) — plays through
	 *  the bark channel with this companion's voice attenuation/volume, interrupting live chatter.
	 *  No type cooldowns. Returns the line's duration so callers can chain the next beat off it
	 *  ending; 0 when nothing played. */
	float SpeakScriptedLine(USoundBase* Sound) const;

	// --- Weapon Interface ---

	/** Virtual so the armed extractee can refuse to fire while its pistol is still hidden
	 *  (the unarmed window between being freed and the handoff landing). */
	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	virtual void StartWeaponFire();

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void StopWeaponFire();

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void ReloadWeapon();

	/** Virtual for the same reason as StartWeaponFire — BT decorators gate on this. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	virtual bool CanFire() const;

	/** Able to hold a firing line at all: has a weapon it is allowed to use. Deliberately NOT
	 *  CanFire(), which also goes false on an empty magazine — a reloading ally must keep its wave
	 *  hold rather than stroll back to formation mid-reload. The extractee narrows this to armed
	 *  only, so the wave hold cannot latch onto it during the unarmed rescue handoff window. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	virtual bool IsCombatReady() const;

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool NeedsReload() const;

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool IsReloading() const;

	/** True if the equipped weapon can currently be reloaded. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool CanReload() const;

	/** Current ammo in the equipped weapon's magazine. Returns 0 if no weapon. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	int32 GetCurrentAmmo() const;

	/** True if the equipped weapon's data marks it suppressed. False with no weapon/data. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool IsCurrentWeaponSuppressed() const;

	/** Returns the reload time of the equipped weapon. Returns 0 if no weapon or no data. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	float GetWeaponReloadTime() const;

	// --- Aim Inaccuracy ---

	void SetAimTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	float GetCurrentInaccuracy() const;

	// --- Getters ---

	UFUNCTION(BlueprintPure, Category = "Companion")
	virtual UHealthComponent* GetHealthComponent() const override { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	USuppressionComponent* GetSuppressionComponent() const { return SuppressionComponent; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	UCoverPoseComponent* GetCoverPoseComponent() const { return CoverPoseComponent; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	TSubclassOf<AWeaponBase> GetWeaponClass() const { return WeaponClass; }

	/** Grenadier-pattern lob component (enemy reuse). Created lazily on first call when the tuning
	 *  DA enables grenades and sets a projectile class; null otherwise. Combat task trigger point. */
	UEnemyGrenadierComponent* GetOrCreateGrenadierComponent();

	UFUNCTION(BlueprintPure, Category = "Companion|Grenade")
	UEnemyGrenadierComponent* GetGrenadierComponent() const { return GrenadierComponent; }

	/** Target the companion is currently aiming at. Used by WeaponBase to aim along muzzle->target. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	AActor* GetAimTarget() const { return CurrentAimTarget.Get(); }

	// --- Target LOS mirror (enemy bHasTargetLOS parity) ---
	// Written by BTService_UpdateCompanionState's LOS filter; consumed by UCompanionAnimInstance
	// to fade the cover aim tracking when the target is behind geometry (kills the through-wall
	// stare — the aim layer itself has no LOS gate by design).

	void SetHasTargetLOS(bool bNewHasLOS) { bHasTargetLOS = bNewHasLOS; }
	bool HasTargetLOS() const { return bHasTargetLOS; }

	// --- Peek-cycle mirror (combat-task-written; CoverSwitchMonitor reads it as the commit gate
	// so score-based switches can't move a companion that never got a shot off at this point) ---

	void SetPeekCyclesAtCurrentCover(int32 Cycles) { PeekCyclesAtCurrentCover = Cycles; }
	int32 GetPeekCyclesAtCurrentCover() const { return PeekCyclesAtCurrentCover; }

	// --- Player-focus mirror (BTService_UpdateCompanionState-written) ---
	// Enemies currently in Combat state with their aim/combat target on the PLAYER. Consumed by
	// the combat task's angle-seek brain — no BB plumbing, same pattern as the LOS mirror above.

	void SetPlayerFocusedEnemyCount(int32 Count) { PlayerFocusedEnemyCount = Count; }
	int32 GetPlayerFocusedEnemyCount() const { return PlayerFocusedEnemyCount; }

	// --- Pressure01 mirror (BTTask_CompanionCombat-written) ---
	// Live pressure signal [0,1] including both distance and incoming-fire terms. Written by the
	// combat task each pressure sample; no in-tree consumer — phase-2 overlay feed.

	void SetPressure01(float Value, float WorldTime) { CachedPressure01 = Value; CachedPressure01Time = WorldTime; }
	float GetPressure01() const { return CachedPressure01; }
	float GetPressure01Time() const { return CachedPressure01Time; }

	// --- Natural cover release (committed-time cycling back to mobile fighting) ---
	// Stamped by CoverSwitchMonitor's natural-release vacate; both cover commit sites read it to
	// block an immediate re-commit (unless fresh strong pressure) so cycling can't become cover-hop.

	void StampNaturalCoverRelease();
	float GetLastNaturalReleaseTime() const { return LastNaturalReleaseTime; }

	// --- Cover commit stamp (combat-task ExecuteTask cover entry) ---
	// The switch monitor's triggers-cleared exit gates on time-since-COMMIT, not time-since-arrival:
	// the monitor's arrival memory persists across task restarts at a retained slot, so a fresh
	// re-commit would otherwise inherit a pre-satisfied dwell and vacate on the first check.

	void StampCoverCommit();
	float GetLastCoverCommitTime() const { return LastCoverCommitTime; }

	// --- Confirmed-kill stamp (written by AEnemyCharacter::HandleDeath's companion-killer branch) ---
	// Combat mode's advance hop reads this to let ONE bound skip its cooldown after a kill. The hop
	// consumes the stamp rather than testing a window every frame, so a single kill can never chain
	// bounds and the hop timer keeps working as the back-off for failed candidate scans.

	void StampConfirmedKill();
	float GetLastConfirmedKillTime() const { return LastConfirmedKillTime; }

	// --- Task speed override (single writer for MaxWalkSpeed / MaxWalkSpeedCrouched) ---
	// Any BT task that needs to author its own walk speeds (combat move-shoot, route stances, etc.)
	// sets the override while active; ApplyMovementSpeeds skips channels that have a positive
	// override, so the Tick focus-edge re-resolve cannot stomp the authored pace. Exactly one task
	// may own it at a time. Cleared unconditionally at DBNO entry, revive, UnPossessed and EndPlay
	// -- no live task can survive any of those transitions.

	/** Sets a task speed override. Positive values override that channel; zero or negative leaves
	 *  the channel under normal ApplyMovementSpeeds control. */
	void SetTaskSpeedOverride(float InWalkSpeed, float InCrouchedSpeed);

	/** Clears both channels unconditionally. */
	void ClearTaskSpeedOverride();

	/** True while any channel is overridden. */
	bool HasTaskSpeedOverride() const { return TaskSpeedOverrideWalk > 0.f || TaskSpeedOverrideCrouched > 0.f; }

	/** Force a re-resolve of walk / crouch speeds from the tuning asset right now. Public wrapper
	 *  so tasks can re-derive after clearing the override without caching the old value. */
	void RefreshMovementSpeeds() { ApplyMovementSpeeds(); }

	// --- Follow catch-up pace (reduced sprint tier for formation catch-up only) ---

	void SetFollowCatchupPace(bool bPace);

	/** True while the follow task is closing a formation gap at the reduced catch-up sprint tier.
	 *  Read by IsStrafingForFocus and the BT service's non-combat facing tiers so the strafe clamp
	 *  and gameplay focals yield during catch-up, same as they do during a full sprint. */
	bool IsFollowCatchupPace() const { return bFollowCatchupPace; }

	// --- Purposeful cover-commit grant (combat-task-written, MoveToCoverPoint-consumed one-shot) ---
	// Set while the pending CoverTarget was deliberately chosen by the combat task (angle-seek pick
	// or Combat-mode advance hop) — bypasses the EvaluateTriggers decline at commit (reservation /
	// occupancy checks still apply). Time-stamped: the grant expires after a few seconds and
	// Consume clears it on EVERY read, so a decline path (claim race, EQS empty, target death) can
	// never leave a stale trigger-free commit behind.

	void SetCoverCommitGrant(bool bPending);
	bool ConsumeCoverCommitGrant();

	/** Stamps a one-shot flag that tells BTTask_MoveToCoverPoint to skip the multi-threat re-rank
	 *  swap (ValidateAndRerankCover) on the next commit. The bNoEyesOn decline still fires -- this
	 *  only prevents replacing the commanded handle with a different cover. Cleared on every read.
	 *  Enemy pawns never set this, so their path is byte-identical. */
	void SetCommandedCoverSkipRerank(bool bSkip) { bCommandedCoverSkipRerank = bSkip; }
	bool ConsumeCommandedCoverSkipRerank() { const bool V = bCommandedCoverSkipRerank; bCommandedCoverSkipRerank = false; return V; }

	/** One-shot: bypass BTTask_MoveToCoverPoint's autonomy gates (CommitDist > OuterCap and
	 *  bOffLevel) for a player-issued cover order. bPointBlankThreat stays enforced. Same
	 *  lifetime discipline as the re-rank skip. */
	void SetCommandedCoverBypass(bool bBypass) { bCommandedCoverBypass = bBypass; }
	bool ConsumeCommandedCoverBypass() { const bool V = bCommandedCoverBypass; bCommandedCoverBypass = false; return V; }

	// --- Commanded cover target (CompanionCommandComponent -> BTTask_CompanionTakeCover one-shot) ---
	// Stores the resolved cover point from the TakeCover command so the BT task can consume it
	// without changing IssueCommand's signature. Time-stamped, cleared on every read (same
	// anti-stale contract as the cover commit grant above).

	void SetCommandedCoverTarget(const FCover& Cover);
	bool ConsumeCommandedCoverTarget(FCover& OutCover);

	// --- Commanded cover hold (companion stays at the commanded cover until released) ---
	// Modelled on SetWaveHoldActive/IsWaveHoldActive: Follow task holds position, posture decay
	// suspends, cover seat retained. Released when the player moves beyond the leash, companion
	// goes DBNO, or a different command is issued.

	void SetCommandedCoverHold(const FVector& AnchorLocation, float LeashRadius, const FCover& HoldCover);
	void ClearCommandedCoverHold();
	bool IsCommandedCoverHoldActive() const { return bCommandedCoverHoldActive; }
	FVector GetCommandedCoverHoldAnchor() const { return CommandedCoverHoldAnchor; }
	float GetCommandedCoverHoldLeash() const { return CommandedCoverHoldLeashRadius; }
	/** The exact cover the player pointed at, retained for the hold's lifetime (unlike the one-shot
	 *  CommandedCoverTarget, which BTTask_CompanionTakeCover consumes). Combat inherits the hold and
	 *  runs its own EQS pick; without this the pick walks the companion off the player's wall. */
	const FCover& GetCommandedCoverHoldCover() const { return CommandedCoverHoldCover; }

	/** Stamp the combat-start time for the hold release. Called once on first BB_CombatTarget while
	 *  the hold is active. Does not re-stamp on target flicker. */
	void StampCommandedCoverCombatStart();
	/** World time combat was first seen during this hold. -1e9 = no combat yet. */
	float GetCommandedCoverCombatStartTime() const { return CommandedCoverCombatStartTime; }
	/** True once combat was seen during this hold. When combat ends with this true, the hold
	 *  releases so the companion does not freeze at stale cover post-fight. */
	bool IsCommandedCoverCombatGraceArmed() const { return bCommandedCoverCombatGraceArmed; }

	// --- Covering Fire (commanded sustained-fire window, see Slice 2 plan) ---
	// Arm -> Start -> Tick -> Clear. Arm and Start are a same-frame handshake driven by the player's
	// press; pending never outlives that frame. Active = clock running. Hard ceiling force-clears
	// regardless of pause state.

	/** Arm a covering-fire window. Does NOT start the clock — call StartCoveringFire straight after. */
	void ArmCoveringFire(float Duration);
	/** Start the countdown. No-op unless armed. */
	void StartCoveringFire();
	/** Tick the countdown. Clock pauses while bReloadHeld is true. */
	void TickCoveringFire(float DeltaSeconds, bool bReloadHeld);
	/** True while the countdown is running (armed AND started). */
	bool IsCoveringFireActive() const { return bCoveringFireActive; }
	/** True while armed but not yet started — only ever within the arming frame. */
	bool IsCoveringFirePending() const { return bCoveringFirePending; }
	/** Remaining window seconds. 0 when inactive. */
	float GetCoveringFireRemaining() const { return CoveringFireRemaining; }
	/** Tear down everything. Safe to call when inactive. */
	void ClearCoveringFire();

	/** Seconds remaining on the covering-fire cooldown. 0 when ready. */
	float GetCoveringFireCooldownRemaining() const;

	/** True while the cooldown is active (covers just ended, not yet ready again). */
	bool IsCoveringFireOnCooldown() const;

	/** Configured length of the post-use covering-fire cooldown, so the HUD can normalise a bar. */
	float GetCoveringFireCooldownDuration() const { return CoveringFireCooldown; }

	/** Record that the companion currently holds a live combat target. Called per service tick
	 *  while BB_CombatTarget is valid; the stamp stays fresh for as long as a target is held.
	 *  Not reset anywhere — liveness is gated separately by callers. */
	void StampCombatTargetSeen();

	/** Seconds since the last StampCombatTargetSeen call. Returns a large value if never stamped
	 *  or if no world is available, so callers can compare against a recency window directly. */
	float GetTimeSinceCombatTarget() const;

	/** Record that the companion had a CLEAR eye-line to a live combat target this tick. Stamped only
	 *  from the state service's LoS-clear branch — deliberately NOT the same signal as
	 *  StampCombatTargetSeen, which stamps on mere target PRESENCE and so stays fresh forever while a
	 *  target is retained through a wall by the cover / player-pressure keeps. Callers that need
	 *  "is this companion actually in a fight right now" must use this one; the revive gate turns on
	 *  exactly that distinction. */
	void StampCombatContact();

	/** Seconds since the last StampCombatContact call. Returns a large value if never stamped or if
	 *  no world is available, so callers can compare against a recency window directly. */
	float GetTimeSinceCombatContact() const;

	// --- Aim location override (covering-fire suppressive fire at last-seen position) ---
	// Mirrors AEnemyCharacter::SetAimLocationOverride. When set, GetAimPointForTarget returns
	// this location instead of the target's sight point. AimTarget stays valid (focus, inaccuracy
	// ramp, anim aim pose all untouched). WeaponBase is untouched.

	void SetAimLocationOverride(const FVector& Location) { AimLocationOverride = Location; bHasAimLocationOverride = true; }
	void ClearAimLocationOverride() { bHasAimLocationOverride = false; AimLocationOverride = FVector::ZeroVector; }
	bool HasAimLocationOverride() const { return bHasAimLocationOverride; }

	/** Mirror written once per combat-task tick with the pre-peek reload gate's latched state.
	 *  Same no-BB-plumbing pattern as SetHasTargetLOS / SetPeekCyclesAtCurrentCover. */
	void SetCoveringFireReloadHeld(bool bHeld) { bCoveringFireReloadHeld = bHeld; }

	/** Mirror written once per combat-task tick: true while the companion is sitting in cover
	 *  NOT peeking. During a covering-fire window the companion is only meant to break off into
	 *  cover to reload, so idle-hunker time must not burn the window's clock. */
	void SetCoveringFireCoverIdle(bool bIdle) { bCoveringFireCoverIdle = bIdle; }

	/** Broadcast while the covering-fire window is active. Carries remaining seconds and whether
	 *  the clock is paused (companion reloading). Fires on the pause edges even when the number
	 *  has not changed, so the HUD can show a distinct paused state. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCoveringFireTick, float, Remaining, bool, bPaused);
	UPROPERTY(BlueprintAssignable, Category = "Companion|CoveringFire")
	FOnCoveringFireTick OnCoveringFireTick;

	// --- Revive Flag (set by BTTask_RevivePlayer while actively reviving) ---

	void SetIsRevivingPlayer(bool bReviving) { bIsRevivingPlayer = bReviving; }
	bool IsRevivingPlayer() const { return bIsRevivingPlayer; }

	// --- Rescue Commit (set by the state service while the revive window is latched open) ---

	void SetRescueCommitted(bool bCommitted) { bRescueCommitted = bCommitted; }
	bool IsRescueCommitted() const { return bRescueCommitted; }

	// --- Wave Hold (Director wave is live and this ally has been in the fight) ---

	void SetWaveHoldActive(bool bActive) { bWaveHoldActive = bActive; }

	/** True while a finite Director wave is running and this ally has already engaged in it.
	 *  A wave stays active across the gaps BETWEEN squad spawns, so without this the last kill of a
	 *  squad clears the combat target, the BB observer aborts the combat branch, the cover slot is
	 *  released and the tree falls through to Follow — allies stroll back to formation mid-defence.
	 *  While set: the combat teardown keeps its cover seat and pose, Follow holds position instead
	 *  of pathing home, and the posture decay to Exploration is suspended. */
	bool IsWaveHoldActive() const { return bWaveHoldActive; }

	// --- Low Ready Aim ---

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void SetLowReadyAim(bool bNewLowReady);

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool IsLowReadyAim() const { return bLowReadyAim; }

	UPROPERTY(BlueprintAssignable, Category = "Companion|Combat")
	FOnLowReadyAimChanged OnLowReadyAimChanged;

	// --- Scripted Aim (route Alert/Crouch legs — weapon up along control rotation, no actor target) ---

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void SetScriptedAim(bool bNewScriptedAim);

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool IsScriptedAiming() const { return bScriptedAim; }

	// --- Route hold (BB_RouteActive stays true through a HoldAtFinal park; this distinguishes it) ---

	void SetRouteHoldingAtFinal(bool bHolding) { bRouteHoldingAtFinal = bHolding; }

	/** True while the route task is parked at the final waypoint (HoldAtFinal). The walk is done —
	 *  player commands (breach) are allowed again even though the route branch is still latent. */
	UFUNCTION(BlueprintPure, Category = "Companion|Route")
	bool IsRouteHoldingAtFinal() const { return bRouteHoldingAtFinal; }

	// --- Sprint API ---

	/** Gate checked by SetSprinting before latching bIsSprinting. Every sprint-tier speed
	 *  (catch-up 650, rescue/stealth 850, traversal mirror) resolves through
	 *  ApplyMovementSpeeds off bIsSprinting, and bIsSprinting only moves through SetSprinting,
	 *  so this single gate is exhaustive. */
	virtual bool CanSprint() const { return true; }

	UFUNCTION(BlueprintCallable, Category = "Companion|Movement")
	void SetSprinting(bool bSprint);

	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	bool IsSprinting() const { return bIsSprinting; }

	/** Speed cap (cm/s) applied while strafing — see UCompanionTuningDataAsset::StrafeMaxSpeed for
	 *  why the number is the locomotion blendspace's top directional row and not a feel lever. */
	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	float GetStrafeMaxSpeed() const;

	/** True while the companion is moving with the body pointed somewhere other than its direction of
	 *  travel: not sprinting, not closing a follow gap, AND a Gameplay-priority focus is live on the AI
	 *  controller. That focus is exactly what drives yaw (the companion runs
	 *  bUseControllerDesiredRotation), so it is the honest test for "the legs are playing the
	 *  directional locomotion rows". Mirrors how UCompanionAnimInstance derives bFocusLive.
	 *
	 *  bFollowCatchupPace is included alongside the sprint check because catch-up pace travels at a
	 *  speed (550-650) well above the strafe cap (275). Without it the companion faces its gameplay
	 *  focal while closing a follow gap and the strafe clamp starves it to 275, which is below the
	 *  player's walk speed (410) -- the companion physically cannot close and the sprint gate flip-flops
	 *  at the boundary. The generalised rule: TRAVELLING (sprinting OR closing a follow gap) means
	 *  facing travel at full speed; holding a gameplay focus while NOT travelling means strafing at the
	 *  capped speed. Never both. */
	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	bool IsStrafingForFocus() const;

	/** True while another system owns this companion's stance (crouch/stand) right now: DBNO, a
	 *  commanded takedown (armed / executing / montage playing), traversal (the capsule resizes
	 *  mid-vault), or an active route leg (its Alert/Crouch legs set their own stances). Shared by the
	 *  stealth clamp teardown and the BT service's stance backstop so the two can never drift — every
	 *  one of these owners restores stance in its own teardown, and popping a crouch out from under
	 *  an authored takedown pose or a mid-vault capsule resize is a proven visual break. */
	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	bool IsStanceOwnedElsewhere() const;

	/** True while the current crouch was applied by the stealth crouch-mirror (MirrorCrouch). Read by
	 *  the BT service's stance backstop so it never pops a crouch the mirror owns. */
	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	bool IsCrouchOwnedByStealth() const { return bCrouchOwnedByStealth; }

	// --- Revive urgency mirror (BTService_UpdateCompanionState-written, BTTask_FollowPlayer-read) ---
	// The service's revive threat sweep already overlaps the downed player every tick; publishing its
	// nearest-hostile result here is what lets the rescue approach decide sprint-vs-jog without a
	// second enemy scan. Same no-BB-plumbing pattern as the LOS / player-focus mirrors above.

	void SetNearestThreatToDownedPlayer(float Distance) { NearestThreatToDownedPlayerDist = Distance; }

	/** Distance (cm) from the DOWNED player to the nearest living hostile inside the service's sweep
	 *  radius. Negative when the player isn't down, the companion is already in the revive hold, or
	 *  nothing was found — callers must treat negative as "no threat", never as "distance 0". */
	float GetNearestThreatToDownedPlayer() const { return NearestThreatToDownedPlayerDist; }

	// --- Stealth catch-up (set by the follow task; shapes ApplyStealthMovementClamps) ---

	void SetStealthCatchup(EStealthCatchup NewStage);

	/** Crouch/UnCrouch on behalf of the stealth crouch-mirror. Tracks ownership so the stealth
	 *  teardown (ApplyStealthMovementClamps) only releases a crouch this system applied — never
	 *  a combat cover crouch. */
	void MirrorCrouch(bool bCrouch);

	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	EStealthCatchup GetStealthCatchup() const { return StealthCatchupStage; }

	// --- Traversal ---

	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	virtual UTraversalComponent* GetTraversalComponent() const override { return TraversalComponent; }

	// --- Suppression / Health ---

	/** True if damage was received within Window seconds. Window <= 0 always returns false. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool IsSuppressed(float Window) const;

	/** Number of real damage hits received within Window seconds — graded under-fire signal for the
	 *  cover triggers (the single-timestamp IsSuppressed check trips on any one graze). */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	int32 GetRecentDamageCount(float Window) const;

	/** Continuous suppression value [0,1] — 0 with no component. The "silent pressure" signal. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	float GetSuppression01() const;

	/** Actor behind the most recent real damage hit, if that hit landed within Window seconds.
	 *  May be ANY actor (player friendly-fire included) — consumers filter for hostiles. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	AActor* GetRecentAttacker(float Window) const;

	/** World time the most recent attacker stamp landed; -1e9 when never damaged. */
	float GetLastAttackerStampTime() const { return LastAttackerStampTime; }

	/** World-time stamp of the last compromise break (relocate or vacate). Gates the debounced
	 *  geometric re-break cooldown and blocks one stale hit from breaking two points in a row. */
	void StampCompromiseBreak();
	float GetLastCompromiseBreakTime() const { return LastCompromiseBreakTime; }

	/** Health fraction [0,1]. Returns 1 if HealthComponent missing. */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	float GetHealthFraction() const;

	/** Magazine fraction [0,1] of the equipped weapon. Returns 1 with no weapon/data (never
	 *  triggers low-ammo behaviour on a companion that can't shoot anyway). */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	float GetAmmoFraction() const;

	// --- Posture ---

	UFUNCTION(BlueprintPure, Category = "Companion")
	ECompanionPosture GetPosture() const { return Posture; }

	UFUNCTION(BlueprintCallable, Category = "Companion")
	void SetPosture(ECompanionPosture NewPosture);

	UPROPERTY(BlueprintAssignable, Category = "Companion")
	FOnCompanionPostureChanged OnPostureChanged;

	// --- Mode (player-commanded directive; see ECompanionMode) ---

	UFUNCTION(BlueprintPure, Category = "Companion|Mode")
	ECompanionMode GetMode() const { return Mode; }

	UFUNCTION(BlueprintCallable, Category = "Companion|Mode")
	virtual void SetMode(ECompanionMode NewMode);

	// --- Second-companion support (armed extractee) ---

	/** False on non-commandable allies (the armed extractee). Player command/story systems —
	 *  pings, mode picker, routes, scripted trigger dialogue, kill-approval barks — must resolve
	 *  the primary only; enemy perception and revive logic treat every companion alike. */
	UFUNCTION(BlueprintPure, Category = "Companion")
	bool IsPrimaryCompanion() const { return bIsPrimaryCompanion; }

	/** Which side of the player this companion forms up on: +1 primary, -1 anyone else. Both
	 *  companions run the same follow task against the same tuning asset, so without the mirror
	 *  they compute a bit-identical anchor behind the player and physically collide. */
	UFUNCTION(BlueprintPure, Category = "Companion")
	float GetFormationSideSign() const { return bIsPrimaryCompanion ? 1.f : -1.f; }

	/** Extra back-offset for a non-primary companion — staggers the pair instead of forming a
	 *  symmetric wall. Takes the tuned value (UCompanionTuningDataAsset::SecondaryFormationBackBias)
	 *  rather than the asset so this header stays free of the tuning include. */
	UFUNCTION(BlueprintPure, Category = "Companion")
	float GetFormationBackBias(float SecondaryBias) const { return bIsPrimaryCompanion ? 0.f : SecondaryBias; }

	/** The player's commandable companion, or null. */
	static ACompanionCharacter* GetPrimaryCompanion(UWorld* World);

	/** True when any companion other than Exclude could still pick the squad up: possessed
	 *  (a captive extractee has no controller yet), not DBNO, not dead. Both squad-wipe fail
	 *  checks key off this instead of "the other one is down". */
	static bool IsAnyCompanionReviveCapable(UWorld* World, const ACompanionCharacter* Exclude);

	/** Same per-actor test as IsAnyCompanionReviveCapable's loop body, exposed for the downed
	 *  player's revive claim: a hold whose owner has died, gone DBNO or lost its controller is
	 *  stale and must be steal-able, or the surviving ally could never take the revive over. */
	static bool IsReviveClaimantCapable(const AActor* Claimant);

	/** Stealth-broken = the fight is on (player spotted); stealth rules are suspended until the
	 *  BT service re-pins. Server-only transient state, set by BTService_UpdateCompanionState. */
	UFUNCTION(BlueprintPure, Category = "Companion|Mode")
	bool IsStealthBroken() const { return bStealthBroken; }

	void SetStealthBroken(bool bBroken);

	/** True while stealth rules apply: Mode == Stealth and not broken. */
	UFUNCTION(BlueprintPure, Category = "Companion|Mode")
	bool IsStealthActive() const { return Mode == ECompanionMode::Stealth && !bStealthBroken; }

	/** Unconditionally unperceivable to enemies (not a mode-driven cloak). Base companion is never this. */
	UFUNCTION(BlueprintPure, Category = "Companion|Mode")
	virtual bool IsAlwaysSightCloaked() const { return false; }

	/** World seconds of the moment unbroken stealth last became active (fresh Stealth order or
	 *  re-pin). The BT service breaks stealth on any combat EVENT stamped after this — a stale
	 *  Combat-awareness/alert tail from before the order can never re-break it. */
	float GetStealthPinTime() const { return StealthPinTime; }

	UPROPERTY(BlueprintAssignable, Category = "Companion|Mode")
	FOnCompanionModeChanged OnModeChanged;

	// --- Post-breach engagement grant (server-only, transient) ---
	// Widens Normal-mode target acquisition to UNAWARE enemies near a spot the player ordered the
	// companion into, for a bounded window. Stamped by BTTask_CompanionBreach on completion and by
	// BTTask_CompanionExplore on arrival; consumed by BTService_UpdateCompanionState's
	// acquisition gates; cleared on DBNO/death.

	void SetPostBreachEngagement(const FVector& Anchor, float Radius, float Duration);
	void ClearPostBreachEngagement();
	bool IsWithinPostBreachEngagement(const FVector& Location) const;

	// --- Search/Breach room exposure (server-only, command-scoped) ---

	void BeginSearchRoomExposure(const FVector& RoomAnchor, float Radius, bool bSilentStartle);
	void EndSearchRoomExposure();
	bool IsSearchRoomExposureActive() const { return SearchRoomExposure.IsActive(); }
	uint32 GetActiveSearchRoomExposureGeneration() const { return SearchRoomExposure.GetActiveGeneration(); }
	bool IsSearchRoomExposureObserverInScope(const FVector& ObserverLocation) const
	{
		return SearchRoomExposure.IsObserverInScope(ObserverLocation);
	}
	bool HasSearchRoomExposureSilentStartle(uint32 ExposureGeneration) const
	{
		return SearchRoomExposure.HasSilentStartle(ExposureGeneration);
	}

	// --- Commanded Takedown (synced to player commit) ---

	/** Arms the companion for a coordinated takedown. Binds to the player's OnPlayerTakedownCommitted.
	 *  Knife: companion faces victim and waits at its current position.
	 *  Shoot: companion aims at the victim immediately.
	 *  Execution fires when the player commits (or deferred until in-position for knife). */
	UFUNCTION(BlueprintCallable, Category = "Companion|Takedown")
	void ArmCommandedTakedown(AActor* Victim, ETakedownMethod Method);

	/** Disarms without executing. Unbinds delegate, clears state. Safe to call when not armed. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Takedown")
	void DisarmCommandedTakedown();

	/** True while armed and waiting for (or executing) a coordinated takedown. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsCommandedTakedownArmed() const { return bTakedownArmed; }

	/** True when a shoot-method takedown is armed (stealth discipline exemption snapshot). */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsShootTakedownArmed() const { return bTakedownArmed && TakedownActiveMethod == ETakedownMethod::Shoot; }

	/** True while the companion is in a crouched knife approach. Readable by the AnimInstance. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsInTakedownApproach() const { return bTakedownCrouchApproach; }

	/** True while a takedown montage is actively playing. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsTakedownMontagePlaying() const { return bTakedownMontagePlaying; }

	/** Latch a victim the companion must finish before it may pick any other combat target.
	 *  Set when a commanded takedown tears down with the victim still alive — the companion switches
	 *  to normal gunfire on it instead of abandoning it for whatever the combat selector prefers.
	 *  HoldSeconds is a safety valve so an unreachable victim can never freeze it out of the fight. */
	void LatchForcedCombatTarget(AActor* Target, float HoldSeconds);

	/** The latched must-finish victim, or nullptr when there is none, it died, or the hold expired.
	 *  Consumed by BTService_UpdateCompanionState as a top-priority target override. */
	AActor* GetForcedCombatTarget() const;

	void ClearForcedCombatTarget();

	/** True from ExecuteCommandedTakedown entry until FinishCommandedTakedown/Disarm.
	 *  BT task uses this to transition Armed -> Executing and stop the hold timeout. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsCommandedTakedownExecuting() const { return bTakedownExecuting; }

	/** True once the player has committed to a synced takedown (fired their weapon or issued the
	 *  explicit commit signal — see OnPlayerFiredWeaponHandler / OnPlayerTakedownCommittedHandler).
	 *  BT task uses this to decide whether it still owes the player a firing position (patience is
	 *  conditional on the player, not on a clock) or whether the short abort budgets should apply. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsCommandedTakedownPlayerCommitted() const { return bTakedownPlayerCommitted; }

	/** Autonomous (no player sync) execution trigger — used when the companion is
	 *  commanded to solo a lone target (no paired takedown partner). */
	UFUNCTION(BlueprintCallable, Category = "Companion|Takedown")
	void CommitTakedownNow();

	void SetTakedownCrouchApproach(bool bApproach) { bTakedownCrouchApproach = bApproach; }
	void SetTakedownInPosition(bool bInPos);


	// --- Commanded Loot ---

	/** Plays the loot/search montage on the body mesh. Early-returns when no montage is assigned
	 *  (the loot still happens — the anim is cosmetic). Called by BTTask_CompanionLoot on arrival. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Loot")
	void PlayLootMontage();

	// --- Commanded Breach ---

	/** Plays the per-type breach montage (kick / tactical open / quiet open). Returns the montage
	 *  play length in seconds, or 0 when no montage is mapped for Type — the door still opens
	 *  (no-montage behaviour). */
	UFUNCTION(BlueprintCallable, Category = "Companion|Breach")
	float PlayBreachMontage(EBreachType Type);

	// --- Revive ---

	/** Plays the kneeling reviver montage while the revive hold runs. Early-returns when no montage
	 *  is assigned (the revive still happens — the anim is cosmetic). Called by BTTask_RevivePlayer. */
	void PlayReviveMontage();

	/** Blends the reviver montage out. Safe to call when nothing is playing. */
	void StopReviveMontage();

	// --- DBNO / Downed State ---

	UFUNCTION(BlueprintPure, Category = "Companion|DBNO")
	bool GetIsCompanionDBNO() const { return bIsDBNO; }

	// --- Angle-seek overlay readout (AI overlay card only — no gameplay reads these) ---

	/** Set by BTTask_CompanionCombat while the crossfire drift is live; cleared on EndAngleSeek. */
	void SetAngleSeekOverlayActive(bool bActive);

	/** Timed variant for the flank-cover commit, which finishes the task immediately — holds the
	 *  FLANKING readout while the companion travels to the committed cover. */
	void MarkAngleSeekOverlayFor(float Seconds);

	bool IsAngleSeekingForOverlay() const;

	/** True while the player's revive hold is active on this companion. The downed-retreat BT task
	 *  stops issuing crawl movement so only the paired montage's root motion moves the body. */
	bool IsBeingRevived() const { return bBeingRevived; }

	/** Crawl-pace MaxWalkSpeed applied while DBNO — the downed-retreat task re-asserts this clamp. */
	float GetDownedCrawlSpeed() const { return DownedCrawlSpeed; }

	UPROPERTY(BlueprintAssignable, Category = "Companion|DBNO")
	FOnCompanionDownedStateChanged OnCompanionDownedStateChanged;

	// --- IExtractionPlayerInterface ---

	virtual UWeaponComponent* GetWeaponComponent() const override { return nullptr; }
	virtual UExtractionAnimInstance* GetExtractionAnimInstance() const override { return nullptr; }
	virtual bool GetIsDBNO() const override { return bIsDBNO; }
	virtual float GetBleedoutTimeRemaining() const override;
	virtual void ExitDBNO() override;
	virtual ETraversalType GetActiveTraversalType() const override;
	virtual bool IsInTraversal() const override;
	virtual bool GetIsVaulting() const override;
	virtual FVector GetVaultTargetLocation() const override { return FVector::ZeroVector; }
	virtual float GetVaultSurfaceHeight() const override { return 0.f; }
	virtual void DoAim(float Yaw, float Pitch) override {}
	virtual USceneComponent* GetWeaponSpawn() const override { return nullptr; }
	virtual void SetBeingRevived(bool bBeingRevived, float ExpectedDuration = 0.f) override;
	virtual void AlignForRevive(const FVector& ReviverLocation) override;
	virtual bool IsBeingRevivedMontagePlaying() const override;
	virtual const UAnimMontage* GetBeingRevivedMontage() const override { return BeingRevivedMontage; }

	/** See bPlayPlayerReviveMontages. */
	bool ShouldPlayPlayerReviveMontages() const { return bPlayPlayerReviveMontages; }

	/** Broadcast when a KNIFE commanded takedown begins executing — BP shows the knife mesh here. */
	UPROPERTY(BlueprintAssignable, Category = "Companion|Takedown")
	FOnCommandedTakedownStarted OnCommandedTakedownStarted;

	/** Broadcast when the companion's takedown execution completes (kill applied or shot fired). */
	UPROPERTY(BlueprintAssignable, Category = "Companion|Takedown")
	FOnCommandedTakedownFinished OnCommandedTakedownFinished;

protected:

	UPROPERTY(ReplicatedUsing = OnRep_Posture)
	ECompanionPosture Posture = ECompanionPosture::Exploration;

	UFUNCTION()
	void OnRep_Posture();

	UPROPERTY(ReplicatedUsing = OnRep_Mode)
	ECompanionMode Mode = ECompanionMode::Normal;

	UFUNCTION()
	void OnRep_Mode();

	/** See IsPrimaryCompanion. Cleared in the armed-extractee subclass constructor. */
	bool bIsPrimaryCompanion = true;

	/** Fail-screen reason when this companion bleeds out — the extractee overrides with its own text. */
	virtual FText GetBleedoutFailReason() const;

	/** Not replicated — server-side behaviour gate; clients only need Mode for UI. */
	bool bStealthBroken = false;

	/** World seconds when unbroken stealth last became active. See GetStealthPinTime. */
	float StealthPinTime = -1e9f;

	/** True while the current crouch was applied by the stealth crouch-mirror (MirrorCrouch).
	 *  Gates the teardown UnCrouch in ApplyStealthMovementClamps so a combat cover crouch —
	 *  which this system does not own — is never popped. */
	bool bCrouchOwnedByStealth = false;

	// Post-breach engagement grant backing state (server-only, transient).
	FVector PostBreachAnchor = FVector::ZeroVector;
	float PostBreachRadiusSq = 0.f;
	float PostBreachExpiryTime = -1.f;

	FSearchRoomExposureState SearchRoomExposure;

	/** Sprint-lock + stealth speed tiers while stealth rules apply; releases them when they don't.
	 *  Stance (crouch/stand) is owned by the BT service's player-crouch mirror (F4a), not this. */
	void ApplyStealthMovementClamps();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostInitializeComponents() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Components")
	TObjectPtr<UHealthComponent> HealthComponent;

	/** Overlay-only angle-seek state — see SetAngleSeekOverlayActive/MarkAngleSeekOverlayFor. */
	bool bAngleSeekOverlayActive = false;
	double AngleSeekOverlayHoldUntil = 0.0;

	/** Audible surface-aware footsteps only — AI-noise emission is disabled at construction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Components")
	TObjectPtr<UFootstepNoiseComponent> FootstepAudioComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Components")
	TObjectPtr<USuppressionComponent> SuppressionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Components")
	TObjectPtr<UCoverPoseComponent> CoverPoseComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Movement")
	TObjectPtr<UTraversalComponent> TraversalComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|UI")
	TObjectPtr<UWidgetComponent> HealthWidgetComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|UI")
	TSubclassOf<UUserWidget> HealthWidgetClass;

	/** Overhead mode indicator (icon that pops on mode change, then fades). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|UI")
	TObjectPtr<UWidgetComponent> ModeWidgetComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|UI")
	TSubclassOf<UUserWidget> ModeWidgetClass;

	// --- Config ---

	/** Companion voice lines + attenuation — designer assigns in BP. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Barks")
	TObjectPtr<UCompanionBarkSetData> BarkSet;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat")
	TSubclassOf<AWeaponBase> WeaponClass;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Weapon")
	FName WeaponAttachSocket = TEXT("WeaponSocket");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float MaxInaccuracyDegrees = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float MinInaccuracyDegrees = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat", meta = (ClampMin = "0.1"))
	float InaccuracySettleTime = 1.5f;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float MaxEngageRange = 3500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Combat", meta = (ClampMin = "1.0"))
	float RotationInterpSpeed = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.5"))
	float ReviveDuration = 4.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "50.0"))
	float ReviveProximityRadius = 200.0f;

	/** Radius (cm) around the downed player within which a Combat-state enemy WITH an eye-line to the
	 *  body counts as a revive threat. Searching enemies in this band never hold the window shut —
	 *  post-fight survivors wandering the area must not block the revive indefinitely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "100.0"))
	float ReviveThreatRadius = 1500.f;

	/** Inner ring (cm) around the downed player where ANY alerted (Searching or Combat) enemy counts
	 *  as a revive threat unconditionally — that close, it would see the revive start regardless of
	 *  current eye-line. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveHardThreatRadius = 600.f;

	/** Cap (cm, from the downed player) for the LoS-based revive-threat check. Beyond ReviveThreatRadius,
	 *  an enemy only holds the window shut when it is actively IN COMBAT, within this range, AND has an
	 *  eye-line to the body — a searcher parked on the standoff ring must not block the revive forever. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "100.0"))
	float ReviveLoSThreatRadius = 2500.f;

	/** Seconds of continuous no-threat before the revive window opens. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveSafeGraceSeconds = 3.0f;

	/** Distance (cm) from the companion to its own combat target that qualifies as "pinned in a fight"
	 *  for the revive entry gate. Below max engage range on purpose. 0 disables. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveSelfEngageRadius = 2000.f;

	/** Recent-attacker window (seconds) for the revive entry gate. Separate from the bail window so
	 *  entry hysteresis is not coupled to bail hysteresis. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveContactWindow = 3.f;

	/** How recently (seconds) the companion must have had an ACTUAL eye-line on a live enemy for the
	 *  revive blockers that carry no line-of-sight test of their own to still count — the self-engage
	 *  fight-live term and the ring's DBNO-handoff shortcut. Both read state that survives through
	 *  walls indefinitely once the player drops, so without this they never release and desperation
	 *  becomes the only opener. 0 disables those two blockers entirely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveFightContactWindow = 4.f;

	/** Suppression level (0-1) at which the companion counts itself as under pressure for the revive
	 *  entry gate. 0 disables. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReviveSuppressionThreshold = 0.25f;

	/** Fight-live term only blocks revive above this many bleedout seconds remaining. Must exceed
	 *  ReviveSprintBleedoutThreshold (DA, 25) to avoid a jogging mid-band rescue. 0 removes the
	 *  release band -- fight-live then blocks at every bleedout value. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveFightLiveBleedoutFloor = 35.f;

	/** Sustained seconds of body heat that closes a committed revive window. The bounded latch:
	 *  unwinds in real time rather than snapping to zero. 0 restores the old unbounded latch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveAbortHotSeconds = 2.5f;

	/** Bleedout seconds remaining at which the companion commits to revive regardless of threats. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "1.0"))
	float DesperationBleedoutThreshold = 12.f;

	/** Incoming damage multiplier while the companion is actively reviving the player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float ReviveDamageMultiplier = 0.35f;

	/** Incoming damage multiplier during the committed rescue approach (window latched, not yet in
	 *  the hold) — the sprint to the body has to be survivable under ring fire. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float RescueApproachDamageMultiplier = 0.5f;

	/** Incoming damage multiplier during a commanded covering-fire window. Sits between revive
	 *  (0.35) and rescue approach (0.5) in strength. Does not stack with either. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|CoveringFire", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float CoveringFireDamageMultiplier = 0.4f;

	/** Health fraction below which a committed rescue bails back to combat while threats are hot.
	 *  Desperation bleedout overrides the bail (last-ditch attempt beats a guaranteed bleedout). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RescueBailHealthFraction = 0.25f;

	/** The low-HP rescue bail additionally requires the companion to have been hit within this many
	 *  seconds — low health with nobody actually shooting must not abort a committed rescue. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float RescueBailUnderFireWindow = 2.f;

	/** Reviver actor offset in the patient actor frame: X is forward and Y is right, in centimetres. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive",
		meta = (ToolTip = "Authored reviver offset from the patient actor. X is forward and Y is right, in centimetres."))
	FVector2D RevivePairOffset = FVector2D(60.f, -64.5f);

	/** Reviver actor yaw relative to the patient actor yaw, in degrees. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive",
		meta = (ToolTip = "Authored reviver yaw relative to the patient actor yaw, in degrees."))
	float RevivePairYawOffset = -46.f;

	/** PLAYER-reviver seat position in the patient actor frame (X fwd, Y right, cm). Defaults
	 *  to the working direction's tuned constants (both directions play the same authored
	 *  clip pair). Kept separate from RevivePairOffset so the player side can be trimmed live
	 *  without touching the working direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Revive")
	FVector2D PlayerRevivePairOffset = FVector2D(60.f, -64.5f);

	/** PLAYER-reviver visual yaw relative to the patient actor yaw (matches RevivePairYawOffset;
	 *  ABP_Manny's rotate-root-bone is compensated separately on the player). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Revive")
	float PlayerRevivePairYawOffset = -46.f;

	/** Extra yaw on the PATIENT body at align, WITHOUT moving the reviver's seat (the seat
	 *  anchors to the untrimmed frame). Positive = the companion turns to its right, which
	 *  reads as rotating left from the kneeling player's view. Live-tunable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Revive",
		meta = (ClampMin = "-180.0", ClampMax = "180.0", UIMin = "-180.0", UIMax = "180.0"))
	float PlayerRevivePatientYawTrimDeg = 0.f;

protected:

	// --- DBNO Config ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|DBNO", meta = (ClampMin = "1.0"))
	float BleedoutDuration = 105.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|DBNO", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float ReviveHealthPercent = 0.3f;

	/** MaxWalkSpeed while DBNO — the downed-retreat crawl toward cover. Mirrors the player's
	 *  DBNOCrawlSpeed so the shared BS_Downed_Crawl blendspace reads the same motion range. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|DBNO", meta = (ClampMin = "0.0"))
	float DownedCrawlSpeed = 100.f;

	// --- Movement (fallbacks — the tuning asset's Companion|Movement block is authoritative
	// once the AI controller possesses; see TunedWalkSpeed/TunedSprintSpeed/TunedCrouchedWalkSpeed.
	// Defaults match the tuning defaults so clients without a controller don't diverge) ---

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float WalkSpeed = 550.f;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float SprintSpeed = 850.f;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float CrouchedWalkSpeed = 250.f;

	// Strafe speed cap fallback — mirror of UCompanionTuningDataAsset::StrafeMaxSpeed. 275 is the
	// TOP DIRECTIONAL ROW of BS_Companion_Rifle02_Locomotion (Y axis = raw cm/s: 0 / 100 / 275 / 850;
	// the 850 row holds a single forward-only sample). Above it the legs blend into a forward sprint
	// clip while the body faces its focus, which is the "runs forwards while side-stepping" report.
	// Re-author that blendspace's top row before raising this.
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float StrafeMaxSpeed = 275.f;

	// Standing-channel stealth fallbacks (used when no tuning asset is assigned) — mirror of the
	// tuning asset's UCompanionTuningDataAsset::StealthWalkSpeed / StealthCatchupSpeed. Stealth no
	// longer force-crouches (F4a), so TunedWalkSpeed must return a stealth-tuned value while
	// standing, same convention as WalkSpeed/CrouchedWalkSpeed above.
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float StealthWalkSpeed = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float StealthCatchupSpeed = 450.f;

	// --- Soft Collision (companion-side self-push — F2 asymmetric blocking) ---

	/** AddMovementInput scale applied when the companion overlaps the player capsule OR another
	 *  companion's. The player's own push (which lets it pass through) lives on
	 *  AExtractionPlayer::CompanionPushStrength. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|SoftCollision", meta = (ClampMin = "0.0"))
	float CompanionSelfPushStrength = 1.0f;

	/** Extra personal-space padding (cm) added on top of the combined capsule radii before the
	 *  self-push kicks in. Capped so total reach stays under the two distances that would turn the
	 *  push into an oscillation: the follow task's 200cm move re-issue deadband (a push that carries
	 *  past it re-paths every frame) and the ~412cm mirrored-wedge separation at live formation
	 *  offsets (FormationOffsetRight 200 mirrored + SecondaryFormationBackBias 100) — beyond that the
	 *  pair would push apart, walk back into formation, and push again forever. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|SoftCollision", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float CompanionSelfPushPadding = 0.f;

	// --- Takedown ---

	/** Knife mesh shown only during a knife takedown. Designer assigns SKM_Knife to it on BP_Companion.
	 *  Attached to KnifeAttachSocket on the body mesh, hidden + no-collision by default. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Takedown")
	TObjectPtr<USkeletalMeshComponent> TakedownKnifeMesh;

	/** Socket on the companion body mesh the takedown knife attaches to. */
	UPROPERTY(EditDefaultsOnly, Category = "Takedown")
	FName KnifeAttachSocket = TEXT("KnifeSocket");

	/** Knife takedown montage — designer assigns in BP. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown")
	TObjectPtr<UAnimMontage> KnifeTakedownMontage;

	/** Loot/search montage played at each container during a commanded loot sweep — designer assigns in BP. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Loot")
	TObjectPtr<UAnimMontage> LootMontage;

	/** Per-type breach montages (Loud kick / Tactical open / Quiet open) — designer assigns in BP.
	 *  A missing entry means that type breaches without a montage. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Breach")
	TMap<EBreachType, TObjectPtr<UAnimMontage>> BreachMontages;

	/** Single-shot reviver montage, rate-scaled to span the hold once. Designer assigns in BP. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Revive")
	TObjectPtr<UAnimMontage> ReviveMontage;

	/** Montage played on this companion while the player revives it, rate-scaled to the hold. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Revive")
	TObjectPtr<UAnimMontage> BeingRevivedMontage;

	/** Master switch for the player-revives-companion animation pair (reviver kneel + patient
	 *  montage + the seat/align math that lines them up). Off = the hold works identically but
	 *  plays no montages on either body. Companion-revives-player is unaffected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Revive")
	bool bPlayPlayerReviveMontages = false;

	/** Victim-relative-to-attacker placement for the knife takedown, in the shared facing frame:
	 *  X = forward gap (the companion stands this far BEHIND the victim), Y = lateral, Z = height.
	 *  Default 90 = the contact spacing the player takedown uses for this same ClavicleStabDown pair
	 *  (root motion is off, so the bodies are placed at the already-closed distance, not the demo's
	 *  wider at-rest spacing). Tunable per finisher on BP_Companion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Takedown")
	FVector CommandedTakedownOffset = FVector(90.f, 0.f, 0.f);

	/** Extra straight-back distance (cm) added along the authored offset's OWN direction — a pure
	 *  radial push away from the victim. Tune THIS to move the companion back: scaling
	 *  CommandedTakedownOffset's components rotates the placement when X and Y are both set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float CommandedTakedownBackpad = 0.f;

	/** Seconds into the knife montage when the victim dies and ragdolls. Mid-collapse (the Vic
	 *  clip's pelvis dive runs ~2.0-2.5s) so physics inherits the fall — engaging the ragdoll on
	 *  the already-flat end pose pops on floor contact. Clamped to the montage length at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Takedown", meta = (ClampMin = "0.1"))
	float KnifeTakedownKillAtSeconds = 2.15f;

	/** Shoot takedown: how many frames to hold aim before firing the lethal shot (legacy, unused). */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float ShootAimSettleDelay = 0.15f;

	/** Hold duration after aiming-in before the first cosmetic shot. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float ShootAimInDuration = 1.0f;

	/** Number of cosmetic (visual-only) shots fired before the kill. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "1"))
	int32 ShootShotCount = 2;

	/** Gap between cosmetic shots. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float ShootShotInterval = 0.12f;

	/** Delay after the kill before lowering the weapon and releasing facing. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float ShootLowerDelay = 1.0f;

	/** Player-synced commit: gap between the instant double-tap shots (replaces ShootShotInterval). */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float ShootCommandedShotInterval = 0.06f;

	/** Player-synced commit: delay from the last shot to the kill. 0 = kill on the last shot's frame. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float ShootCommandedKillDelay = 0.1f;

	/** How far ahead each Tick stamps the victim's/partners' takedown hush (AEnemyCharacter::
	 *  BeginTakedownWindow). Doubles as the teardown latency: once the heartbeat stops, the pocket
	 *  wakes this many seconds later at worst. Also the grace tail that swallows the player's synced
	 *  shot — OnPlayerFiredWeapon broadcasts before the hitscan and its noise report, so the shot's
	 *  stimulus can land a few frames AFTER the companion has already finished and stopped stamping.
	 *  0.75 s is long enough to cover that, short enough that a missed shot still wakes the pocket
	 *  inside a second (the punishment for whiffing). */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Takedown", meta = (ClampMin = "0.0"))
	float TakedownWindowRefreshSeconds = 0.75f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_LowReadyAim)
	bool bLowReadyAim = false;

	/** Scripted weapon-up: aim along control rotation with no actor target (e.g. route Alert/Crouch legs). Not replicated — single-player feature. */
	bool bScriptedAim = false;

	/** True while the route task is parked at the final waypoint (HoldAtFinal). Transient, not replicated. */
	bool bRouteHoldingAtFinal = false;

	/** True while the companion is in the BTTask_RevivePlayer hold. Drives tanky damage reduction
	 *  and the service-side revive-window latch. Transient, not replicated. */
	bool bIsRevivingPlayer = false;

	/** True while the revive window is latched open (committed rescue: approach + hold). Drives the
	 *  approach damage reduction. Written by BTService_UpdateCompanionState. Transient. */
	bool bRescueCommitted = false;

	/** See IsWaveHoldActive. Written by BTService_UpdateCompanionState. Transient, not replicated. */
	bool bWaveHoldActive = false;

	/** Commanded cover hold backing state. Written by BTTask_CompanionTakeCover, released by
	 *  BTService_UpdateCompanionState. Transient, not replicated. */
	bool bCommandedCoverHoldActive = false;
	FVector CommandedCoverHoldAnchor = FVector::ZeroVector;
	float CommandedCoverHoldLeashRadius = 0.f;
	/** The cover the hold is anchored to. See GetCommandedCoverHoldCover. */
	FCover CommandedCoverHoldCover;
	/** World time the first BB_CombatTarget was seen during this hold. -1e9 = no combat yet.
	 *  Stamped once, never re-stamped on target flicker. */
	float CommandedCoverCombatStartTime = -1e9f;
	/** True once combat was seen during this hold. When combat ends with this flag true, the
	 *  hold releases -- a post-fight companion must not freeze at stale cover. */
	bool bCommandedCoverCombatGraceArmed = false;

	/** One-shot commanded cover target. Written by CompanionCommandComponent::ConfirmTakeCover,
	 *  consumed by BTTask_CompanionTakeCover. Cleared on every read. */
	FCover CommandedCoverTarget;
	float CommandedCoverTargetStamp = -1e9f;

	/** Covering-fire backing state. Transient, not replicated. */
	bool bCoveringFirePending = false;
	bool bCoveringFireActive = false;
	float CoveringFireRemaining = 0.f;
	float CoveringFireDuration = 0.f;
	/** World time start was called. Hard ceiling = start + duration + CoveringFireCeilingSlack. */
	float CoveringFireStartTime = -1e9f;

	/** Seconds of slack beyond the nominal duration before the hard ceiling force-clears. Without
	 *  it a reload that can never complete leaves the companion permanently damage-resistant. */
	static constexpr float CoveringFireCeilingSlack = 8.f;

	/** Lifetime (seconds) of a cover-commit grant before it is treated as stale. */
	static constexpr float CoveringFireArmTimeout = 6.f;

	/** Cooldown (seconds) after a covering-fire window completes before another can be triggered. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|CoveringFire", meta = (ClampMin = "0.0"))
	float CoveringFireCooldown = 10.f;

	/** Aim location override for suppressive fire. See SetAimLocationOverride. */
	FVector AimLocationOverride = FVector::ZeroVector;
	bool bHasAimLocationOverride = false;

	/** World time the last window ended. -1e9 = never. Stamped only when a window actually ran. */
	float CoveringFireEndTime = -1e9f;

	/** Mirror of the combat task's pre-peek reload gate. Written per combat-task tick. */
	bool bCoveringFireReloadHeld = false;

	/** Mirror of the combat task's in-cover-idle (not peeking) state. Written per combat-task tick. */
	bool bCoveringFireCoverIdle = false;

	/** Cached remaining for the broadcast throttle (avoids per-frame multicast). */
	float CoveringFireLastBroadcast = -1.f;
	/** Cached paused state for the broadcast edge detect. */
	bool bCoveringFireLastBroadcastPaused = false;

	/** World time the companion last held a valid BB_CombatTarget. Stamped per service tick
	 *  while a target is set; never cleared (callers compare against a recency window). */
	float LastCombatTargetSeenTime = -1e9f;

	/** World time the companion last had a CLEAR eye-line to a live combat target. Stamped only from
	 *  the service's LoS-clear branch; never cleared (callers compare against a recency window). */
	float LastCombatContactTime = -1e9f;

	/** Mirror of the combat service's eye→target LOS trace (enemy bHasTargetLOS parity). Transient, not replicated. */
	bool bHasTargetLOS = false;

	/** Mirror of the combat task's PeekCyclesAtCover (per-tick copy). Transient, not replicated. */
	int32 PeekCyclesAtCurrentCover = 0;

	UFUNCTION()
	void OnRep_LowReadyAim();

	UPROPERTY(ReplicatedUsing = OnRep_IsSprinting)
	bool bIsSprinting = false;

	UFUNCTION()
	void OnRep_IsSprinting();

	UFUNCTION()
	void HandleDeath();

	UFUNCTION()
	void HandleRevive();

	/** Pushes shield changes into the overhead health widget's OnShieldUpdated custom event —
	 *  the widget graph can't bind OnShieldChanged itself (see BeginPlay). */
	UFUNCTION()
	void HandleShieldChangedForWidget(float CurrentShield, float MaxShield);

	UFUNCTION()
	void OnWeaponFiredCallback();

	UFUNCTION()
	void HandleTraversalStarted(ETraversalType Type, float PlayRate, FVector ObstacleLocation, FVector LandingLocation);

	UFUNCTION()
	void HandleTraversalEnded();

	UFUNCTION()
	void OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void EnterDBNO();
	void OnBleedoutExpired();

	UPROPERTY(ReplicatedUsing = OnRep_IsDBNO)
	bool bIsDBNO = false;

	UFUNCTION()
	void OnRep_IsDBNO();

	/** True while the player is reviving this companion. */
	bool bBeingRevived = false;

	FTimerHandle BleedoutTimerHandle;

	/** Repeats the DBNO call-for-help bark while awaiting revive. Cleared on revive/death/EndPlay. */
	FTimerHandle CallForHelpTimerHandle;

	UPROPERTY(VisibleInstanceOnly, Category = "Companion|Tags")
	FGameplayTagContainer OwnedTags;

	UPROPERTY()
	TObjectPtr<AWeaponBase> CurrentWeapon;

	/** See GetOrCreateGrenadierComponent. */
	UPROPERTY()
	TObjectPtr<UEnemyGrenadierComponent> GrenadierComponent;

	/** Montage started by HandleGrenadeTelegraph — stopped by HandleGrenadeCancelled. */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveGrenadeThrowMontage;

	/** Plays the throw wind-up montage (crouch variant when crouched, else a random stand montage). */
	UFUNCTION()
	void HandleGrenadeTelegraph(FVector PredictedLanding, float TimeToImpact);

	/** Barks "frag out" at release — bound to OnGrenadeThrown so a cancelled wind-up stays silent. */
	UFUNCTION()
	void HandleGrenadeThrown();

	/** Stops the in-flight throw montage on a cancelled wind-up. */
	UFUNCTION()
	void HandleGrenadeCancelled();

	UPROPERTY()
	TWeakObjectPtr<AActor> CurrentAimTarget;

	float TimeAimingAtCurrentTarget = 0.0f;
	float LastDamageWorldTime = -1e9f;

	/** Recent damage-hit timestamps (world seconds) — pruned on stamp, bounded. Backs GetRecentDamageCount. */
	TArray<float> RecentDamageTimes;

	/** Actor behind the most recent damage hit + its stamp time. Backs GetRecentAttacker. */
	TWeakObjectPtr<AActor> LastDamageAttacker;
	float LastAttackerStampTime = -1e9f;

	/** World time of the last compromise break. Backs StampCompromiseBreak. */
	float LastCompromiseBreakTime = -1e9f;

	/** Mirror of the BT service's "enemies focused on the player" tally. Transient, not replicated. */
	int32 PlayerFocusedEnemyCount = 0;

	/** Mirror of BTTask_CompanionCombat's computed Pressure01 (distance + fire terms). Transient. */
	float CachedPressure01 = 0.f;
	/** World time of the last SetPressure01 write. Used to detect stale values in the overlay. */
	float CachedPressure01Time = -1e9f;

	/** See GetNearestThreatToDownedPlayer. Negative = no threat / not applicable. Transient. */
	float NearestThreatToDownedPlayerDist = -1.f;

	/** IsStrafingForFocus() as of the last Tick. ApplyMovementSpeeds only runs on sprint/stance/
	 *  stealth edges, so a focus appearing or vanishing under it would leave the strafe clamp
	 *  resolved against a stale answer; Tick re-resolves on this edge and only on this edge. */
	bool bLastStrafingForFocus = false;

	/** World time of the last committed-time natural cover release. */
	float LastNaturalReleaseTime = -1e9f;

	/** World time of the last combat-task cover commit (ExecuteTask cover entry). */
	float LastCoverCommitTime = -1e9f;

	/** World time of this companion's last confirmed kill. Backs the Combat-mode post-kill advance. */
	float LastConfirmedKillTime = -1e9f;

	/** Task speed override channels. Positive = overridden; zero/negative = under normal control.
	 *  See SetTaskSpeedOverride / ClearTaskSpeedOverride. */
	float TaskSpeedOverrideWalk = 0.f;
	float TaskSpeedOverrideCrouched = 0.f;

	/** True while the follow task's catch-up sprint should use the reduced FollowCatchupSprintSpeed
	 *  tier instead of full SprintSpeed. Never set by rescue sprint-to-target or stealth catch-up. */
	bool bFollowCatchupPace = false;

	/** Delays the FallingBehind bark until catch-up pace has held continuously this long — pace
	 *  toggles constantly during normal follow sprints and must not bark on every flip. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Barks", meta = (ClampMin = "0.0"))
	float FallingBehindBarkDelay = 3.f;

	/** Pending FallingBehind bark; armed on catch-up start, cleared when pace ends. */
	FTimerHandle CatchupBarkTimerHandle;

	/** World time the purposeful cover-commit grant was stamped; -1e9 = none. See SetCoverCommitGrant. */
	float CoverCommitGrantStamp = -1e9f;

	/** One-shot skip-rerank flag. See SetCommandedCoverSkipRerank. */
	bool bCommandedCoverSkipRerank = false;

	/** One-shot commanded-cover bypass flag. See SetCommandedCoverBypass. */
	bool bCommandedCoverBypass = false;

	EStealthCatchup StealthCatchupStage = EStealthCatchup::None;

	/** Tuning asset from the possessing companion AI controller; null before possession / on clients. */
	const UCompanionTuningDataAsset* GetTuning() const;
	float TunedWalkSpeed() const;
	float TunedSprintSpeed() const;
	float TunedFollowCatchupSprintSpeed() const;
	float TunedCrouchedWalkSpeed() const;

	/** Pushes the current sprint/stealth-catchup state into CMC MaxWalkSpeed / MaxWalkSpeedCrouched. */
	void ApplyMovementSpeeds();

	/** Server-authority: gently pushes the companion out of an overlap with the player instead of
	 *  popping — the companion side of the F2 asymmetric blocking (the player's own push is
	 *  AExtractionPlayer::UpdateCompanionSoftCollision). Mirrors that push math with roles swapped. */
	void TickPlayerSoftSeparation();

	/** The same treatment against every OTHER companion. Both companions run the same follow task,
	 *  so in a corridor narrower than the formation their capsules meet and each hard-blocks the
	 *  other's movement sweep — neither can slide past and the pair jams. The ignore wiring runs
	 *  everywhere (local movement filter, no authority semantics); only the push is server-side. */
	void TickAllySoftSeparation();

	/** Every other companion in the world (primary <-> armed VIP). Rebuilt on a slow rescan —
	 *  TActorIterator walks the whole level and can't run on a per-frame pass. */
	TArray<TWeakObjectPtr<ACompanionCharacter>> AllyCompanions;

	/** World seconds of the last ally rescan. Sentinel-low so the first tick always scans — the
	 *  interval alone then gates the empty-list case too. */
	float LastAllyScanTime = -1e9f;

	void RefreshAllyCompanions();

	/** Shared gate for both separation passes: false while we are downed, mid-revive, mid-takedown,
	 *  traversing, or unpossessed (the captive VIP — CMC never consumes an input vector without a
	 *  controller, and a push must never walk him off his placed kneeling spot). */
	bool CanApplySoftSeparation() const;

	/** Idempotent MUTUAL ignore assert against one ally. Mutual, unlike the asymmetric player
	 *  wiring: both bodies steer, so a one-sided ignore still leaves the other sweep blocking. */
	void EnsureAllySoftCollisionIgnores(ACompanionCharacter& Ally);

	/** Converts capsule overlap depth with Other into an AddMovementInput push away from it. */
	void SoftPushAwayFrom(const ACharacter& Other);

	FTimerHandle ModeWidgetLinkTimerHandle;

	/** Casts the mode widget component's user widget and hands it this companion.
	 *  Re-arms ModeWidgetLinkTimerHandle if the widget isn't constructed yet. */
	void TryLinkModeWidget();

	// --- Commanded takedown state ---

	UFUNCTION()
	void OnPlayerTakedownCommittedHandler();

	UFUNCTION()
	void OnPlayerFiredWeaponHandler();

	void ExecuteCommandedTakedown();
	void FinishCommandedTakedown();

	/** Re-stamps the takedown hush on every enemy in TakedownWindowEnemies. Called every Tick while a
	 *  commanded takedown is armed/executing — see TakedownWindowRefreshSeconds. */
	void RefreshTakedownWindow();

	/** Cosmetic fire: plays the fire montage + weapon muzzle FX with no hitscan/damage/alert. */
	void FireCosmeticShotAt(const FVector& AimEndPoint);

	// Shoot takedown phased helpers (each phase re-arms ShootDelayTimerHandle for the next)
	void HandleTakedownAimedIn();
	void HandleTakedownKill();
	void HandleTakedownLower();

	UFUNCTION()
	void OnTakedownMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	TWeakObjectPtr<AActor> TakedownVictim;
	/** Unfinished takedown victim the combat selector must keep targeting until it dies. */
	TWeakObjectPtr<AActor> ForcedCombatTarget;
	/** World time the forced-target commitment lapses. */
	float ForcedCombatTargetExpiry = 0.f;
	TWeakObjectPtr<AExtractionPlayer> TakedownPlayerRef;

	/** The victim plus every takedown-eligible enemy sharing a volume with it — the pocket the armed
	 *  takedown hushes. Weak: any of them can die (that is the point) while the window is open, and a
	 *  dead entry simply stops being stamped. Emptied on disarm/finish, which IS the teardown. */
	TArray<TWeakObjectPtr<AEnemyCharacter>> TakedownWindowEnemies;

	ETakedownMethod TakedownActiveMethod = ETakedownMethod::Knife;
	FTimerHandle ShootDelayTimerHandle;

	/** Mid-montage knife-kill timer (see KnifeTakedownKillAtSeconds). */
	FTimerHandle KnifeKillTimerHandle;

	/** True when the pending shoot execution was triggered by the player's own shot — selects the
	 *  instant double-tap chain instead of the phased autonomous cadence. */
	bool bTakedownCommandedInstant = false;

	/** Instant chain registers the kill up front (before the cosmetic shots) — guards
	 *  HandleTakedownKill against a second ExecuteTakedown on the corpse. */
	bool bTakedownKillRegistered = false;
	bool bTakedownArmed = false;
	bool bTakedownPlayerCommitted = false;
	bool bTakedownInPosition = false;
	bool bTakedownExecuting = false;
	bool bTakedownCrouchApproach = false;
	bool bTakedownMontagePlaying = false;

	/** Remaining cosmetic shots in the shoot takedown sequence. Transient runtime counter. */
	int32 TakedownShotsRemaining = 0;
};
