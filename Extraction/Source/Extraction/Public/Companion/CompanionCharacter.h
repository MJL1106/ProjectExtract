// AI companion character — follows player, engages enemies, revives downed teammates.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Movement/TraversalTypes.h"
#include "Companion/CompanionTypes.h"
#include "Companion/CompanionCommandTypes.h"
#include "AIShooterInterface.h"
#include "CompanionCharacter.generated.h"

class UHealthComponent;
class USuppressionComponent;
class UCoverPoseComponent;
class AWeaponBase;
class UCompanionAnimInstance;
class UTraversalComponent;
class UWidgetComponent;
class UUserWidget;
class AExtractionPlayer;
class UCompanionTuningDataAsset;

DECLARE_LOG_CATEGORY_EXTERN(LogCompanion, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCompanionPostureChanged, ECompanionPosture, NewPosture);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCompanionModeChanged, ECompanionMode, NewMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLowReadyAimChanged, bool, bIsLowReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCommandedTakedownFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCommandedTakedownStarted);

UCLASS(Blueprintable)
class EXTRACTION_API ACompanionCharacter : public ACharacter, public IGameplayTagAssetInterface, public IAIShooterInterface, public IGenericTeamAgentInterface
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

	// --- Weapon Interface ---

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void StartWeaponFire();

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void StopWeaponFire();

	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void ReloadWeapon();

	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	bool CanFire() const;

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
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	USuppressionComponent* GetSuppressionComponent() const { return SuppressionComponent; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	UCoverPoseComponent* GetCoverPoseComponent() const { return CoverPoseComponent; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Companion")
	TSubclassOf<AWeaponBase> GetWeaponClass() const { return WeaponClass; }

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

	// --- Natural cover release (committed-time cycling back to mobile fighting) ---
	// Stamped by CoverSwitchMonitor's natural-release vacate; both cover commit sites read it to
	// block an immediate re-commit (unless fresh strong pressure) so cycling can't become cover-hop.

	void StampNaturalCoverRelease();
	float GetLastNaturalReleaseTime() const { return LastNaturalReleaseTime; }

	// --- Purposeful cover-commit grant (combat-task-written, MoveToCoverPoint-consumed one-shot) ---
	// Set while the pending CoverTarget was deliberately chosen by the combat task (angle-seek pick
	// or Combat-mode advance hop) — bypasses the EvaluateTriggers decline at commit (reservation /
	// occupancy checks still apply). Time-stamped: the grant expires after a few seconds and
	// Consume clears it on EVERY read, so a decline path (claim race, EQS empty, target death) can
	// never leave a stale trigger-free commit behind.

	void SetCoverCommitGrant(bool bPending);
	bool ConsumeCoverCommitGrant();

	// --- Revive Flag (set by BTTask_RevivePlayer while actively reviving) ---

	void SetIsRevivingPlayer(bool bReviving) { bIsRevivingPlayer = bReviving; }
	bool IsRevivingPlayer() const { return bIsRevivingPlayer; }

	// --- Rescue Commit (set by the state service while the revive window is latched open) ---

	void SetRescueCommitted(bool bCommitted) { bRescueCommitted = bCommitted; }
	bool IsRescueCommitted() const { return bRescueCommitted; }

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

	// --- Sprint API ---

	UFUNCTION(BlueprintCallable, Category = "Companion|Movement")
	void SetSprinting(bool bSprint);

	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	bool IsSprinting() const { return bIsSprinting; }

	// --- Stealth catch-up (set by the follow task; shapes ApplyStealthMovementClamps) ---

	void SetStealthCatchup(EStealthCatchup NewStage);

	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	EStealthCatchup GetStealthCatchup() const { return StealthCatchupStage; }

	// --- Traversal ---

	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	UTraversalComponent* GetTraversalComponent() const { return TraversalComponent; }

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
	void SetMode(ECompanionMode NewMode);

	/** Stealth-broken = the fight is on (player spotted); stealth rules are suspended until the
	 *  BT service re-pins. Server-only transient state, set by BTService_UpdateCompanionState. */
	UFUNCTION(BlueprintPure, Category = "Companion|Mode")
	bool IsStealthBroken() const { return bStealthBroken; }

	void SetStealthBroken(bool bBroken);

	/** True while stealth rules apply: Mode == Stealth and not broken. */
	UFUNCTION(BlueprintPure, Category = "Companion|Mode")
	bool IsStealthActive() const { return Mode == ECompanionMode::Stealth && !bStealthBroken; }

	UPROPERTY(BlueprintAssignable, Category = "Companion|Mode")
	FOnCompanionModeChanged OnModeChanged;

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

	/** True while the companion is in a crouched knife approach. Readable by the AnimInstance. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsInTakedownApproach() const { return bTakedownCrouchApproach; }

	/** True while a takedown montage is actively playing. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsTakedownMontagePlaying() const { return bTakedownMontagePlaying; }

	/** True from ExecuteCommandedTakedown entry until FinishCommandedTakedown/Disarm.
	 *  BT task uses this to transition Armed -> Executing and stop the hold timeout. */
	UFUNCTION(BlueprintPure, Category = "Companion|Takedown")
	bool IsCommandedTakedownExecuting() const { return bTakedownExecuting; }

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

	/** True while the reviver montage is actively playing on the body mesh. */
	bool IsReviveMontagePlaying() const;

	/** ReviveDuration with the `revive.Duration` console override applied (tuning: tweak live in PIE,
	 *  then bake the winner into the BP). Montage rate-scaling follows this everywhere. */
	float GetEffectiveReviveDuration() const;

	/** ReviveAlignDistance with the `revive.AlignDistance` console override applied. */
	float GetEffectiveReviveAlignDistance() const;

	/** Live-tuning yaw (deg) added to the companion's facing at the revive snap — `revive.CompanionYawOffset`. */
	static float GetReviveCompanionYawOffset();

	/** The reviver montage asset — the revive task reads its clip's authored root start transform. */
	const UAnimMontage* GetReviveMontage() const { return ReviveMontage; }

	/** True only when the reviver montage is NOT playing AND a *different* montage is active on the
	 *  body — i.e. a same-group takeover stomped it mid-hold and it needs re-asserting. Returns false
	 *  when nothing is playing (the montage naturally reached its end), so the per-tick re-assert can't
	 *  restart it at the tail of the hold — that tail restart was the visible "double play". */
	bool ShouldReassertReviveMontage() const;

	/** Name of the montage currently active on the body mesh's anim instance ("None" if none) —
	 *  hold-loop diagnostic for identifying a same-group montage stomping the reviver anim. */
	FString GetActiveBodyMontageName() const;

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

	/** Not replicated — server-side behaviour gate; clients only need Mode for UI. */
	bool bStealthBroken = false;

	/** Crouch + sprint-lock while stealth rules apply; releases them when they don't. */
	void ApplyStealthMovementClamps();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostInitializeComponents() override;
	virtual void PossessedBy(AController* NewController) override;

	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Components")
	TObjectPtr<UHealthComponent> HealthComponent;

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

	/** Radius (cm) around the downed player within which an alerted enemy counts as a revive threat. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "100.0"))
	float ReviveThreatRadius = 1500.f;

	/** Cap (cm, from the downed player) for the LoS-based revive-threat check. Beyond ReviveThreatRadius,
	 *  an enemy only holds the window shut when it is actively IN COMBAT, within this range, AND has an
	 *  eye-line to the body — a searcher parked on the standoff ring must not block the revive forever. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "100.0"))
	float ReviveLoSThreatRadius = 2500.f;

	/** Seconds of continuous no-threat before the revive window opens. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveSafeGraceSeconds = 1.0f;

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

	/** Health fraction below which a committed rescue bails back to combat while threats are hot.
	 *  Desperation bleedout overrides the bail (last-ditch attempt beats a guaranteed bleedout). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RescueBailHealthFraction = 0.25f;

	/** Center-to-center distance (cm) the companion snaps to at revive-hold start so the paired
	 *  kneel/get-up anims line up face-to-face. Tune to the anim pair's authored gap. Values below
	 *  the summed capsule radii (~68) are safe: the pair mutually move-ignores during the hold. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Revive", meta = (ClampMin = "40.0"))
	float ReviveAlignDistance = 70.f;

protected:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Companion|Config", meta = (ClampMin = "0.0"))
	float DestroyDelay = 3.0f;

	// --- Movement (fallbacks — the tuning asset's Companion|Movement block is authoritative
	// once the AI controller possesses; see TunedWalkSpeed/TunedSprintSpeed/TunedCrouchedWalkSpeed.
	// Defaults match the tuning defaults so clients without a controller don't diverge) ---

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float WalkSpeed = 550.f;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float SprintSpeed = 850.f;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float CrouchedWalkSpeed = 250.f;

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

	/** Single-shot reviver montage (rate-scaled to span the hold once) played while reviving the
	 *  downed player — designer assigns in BP. Must be non-looping: the double-play fix relies on the
	 *  montage having a natural blend-out tail so the per-tick re-assert stops once it ends. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Revive")
	TObjectPtr<UAnimMontage> ReviveMontage;

	/** Victim-relative-to-attacker placement for the knife takedown, in the shared facing frame:
	 *  X = forward gap (the companion stands this far BEHIND the victim), Y = lateral, Z = height.
	 *  Default 90 = the contact spacing the player takedown uses for this same ClavicleStabDown pair
	 *  (root motion is off, so the bodies are placed at the already-closed distance, not the demo's
	 *  wider at-rest spacing). Tunable per finisher on BP_Companion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Takedown")
	FVector CommandedTakedownOffset = FVector(90.f, 0.f, 0.f);

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

private:
	UPROPERTY(ReplicatedUsing = OnRep_LowReadyAim)
	bool bLowReadyAim = false;

	/** Scripted weapon-up: aim along control rotation with no actor target (e.g. route Alert/Crouch legs). Not replicated — single-player feature. */
	bool bScriptedAim = false;

	/** True while the companion is in the BTTask_RevivePlayer hold. Drives tanky damage reduction
	 *  and the service-side revive-window latch. Transient, not replicated. */
	bool bIsRevivingPlayer = false;

	/** True while the revive window is latched open (committed rescue: approach + hold). Drives the
	 *  approach damage reduction. Written by BTService_UpdateCompanionState. Transient. */
	bool bRescueCommitted = false;

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

	UFUNCTION()
	void OnWeaponFiredCallback();

	UFUNCTION()
	void HandleTraversalStarted(ETraversalType Type, float PlayRate, FVector ObstacleLocation, FVector LandingLocation);

	UFUNCTION()
	void HandleTraversalEnded();

	UFUNCTION()
	void OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void DestroyAfterDeath();

	UPROPERTY(VisibleInstanceOnly, Category = "Companion|Tags")
	FGameplayTagContainer OwnedTags;

	UPROPERTY()
	TObjectPtr<AWeaponBase> CurrentWeapon;

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

	/** World time of the last committed-time natural cover release. */
	float LastNaturalReleaseTime = -1e9f;

	/** World time the purposeful cover-commit grant was stamped; -1e9 = none. See SetCoverCommitGrant. */
	float CoverCommitGrantStamp = -1e9f;

	EStealthCatchup StealthCatchupStage = EStealthCatchup::None;

	/** Tuning asset from the possessing companion AI controller; null before possession / on clients. */
	const UCompanionTuningDataAsset* GetTuning() const;
	float TunedWalkSpeed() const;
	float TunedSprintSpeed() const;
	float TunedCrouchedWalkSpeed() const;

	/** Pushes the current sprint/stealth-catchup state into CMC MaxWalkSpeed / MaxWalkSpeedCrouched. */
	void ApplyMovementSpeeds();

	FTimerHandle DestroyTimerHandle;
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

	/** Cosmetic fire: plays the fire montage + weapon muzzle FX with no hitscan/damage/alert. */
	void FireCosmeticShotAt(const FVector& AimEndPoint);

	// Shoot takedown phased helpers (each phase re-arms ShootDelayTimerHandle for the next)
	void HandleTakedownAimedIn();
	void HandleTakedownKill();
	void HandleTakedownLower();

	UFUNCTION()
	void OnTakedownMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	TWeakObjectPtr<AActor> TakedownVictim;
	TWeakObjectPtr<AExtractionPlayer> TakedownPlayerRef;
	ETakedownMethod TakedownActiveMethod = ETakedownMethod::Knife;
	FTimerHandle ShootDelayTimerHandle;

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
