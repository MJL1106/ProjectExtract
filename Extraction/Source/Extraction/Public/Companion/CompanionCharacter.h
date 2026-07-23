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
#include "CompanionCharacter.generated.h"

enum class ECompanionBarkType : uint8;
class UCompanionBarkSetData;
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

	// --- Follow catch-up pace (reduced sprint tier for formation catch-up only) ---

	void SetFollowCatchupPace(bool bPace);

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

	// --- Route hold (BB_RouteActive stays true through a HoldAtFinal park; this distinguishes it) ---

	void SetRouteHoldingAtFinal(bool bHolding) { bRouteHoldingAtFinal = bHolding; }

	/** True while the route task is parked at the final waypoint (HoldAtFinal). The walk is done —
	 *  player commands (breach) are allowed again even though the route branch is still latent. */
	UFUNCTION(BlueprintPure, Category = "Companion|Route")
	bool IsRouteHoldingAtFinal() const { return bRouteHoldingAtFinal; }

	// --- Sprint API ---

	UFUNCTION(BlueprintCallable, Category = "Companion|Movement")
	void SetSprinting(bool bSprint);

	UFUNCTION(BlueprintPure, Category = "Companion|Movement")
	bool IsSprinting() const { return bIsSprinting; }

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
	void SetMode(ECompanionMode NewMode);

	/** Stealth-broken = the fight is on (player spotted); stealth rules are suspended until the
	 *  BT service re-pins. Server-only transient state, set by BTService_UpdateCompanionState. */
	UFUNCTION(BlueprintPure, Category = "Companion|Mode")
	bool IsStealthBroken() const { return bStealthBroken; }

	void SetStealthBroken(bool bBroken);

	/** True while stealth rules apply: Mode == Stealth and not broken. */
	UFUNCTION(BlueprintPure, Category = "Companion|Mode")
	bool IsStealthActive() const { return Mode == ECompanionMode::Stealth && !bStealthBroken; }

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

	// --- DBNO / Downed State ---

	UFUNCTION(BlueprintPure, Category = "Companion|DBNO")
	bool GetIsCompanionDBNO() const { return bIsDBNO; }

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

	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Companion|Components")
	TObjectPtr<UHealthComponent> HealthComponent;

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

	// Standing-channel stealth fallbacks (used when no tuning asset is assigned) — mirror of the
	// tuning asset's UCompanionTuningDataAsset::StealthWalkSpeed / StealthCatchupSpeed. Stealth no
	// longer force-crouches (F4a), so TunedWalkSpeed must return a stealth-tuned value while
	// standing, same convention as WalkSpeed/CrouchedWalkSpeed above.
	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float StealthWalkSpeed = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Movement")
	float StealthCatchupSpeed = 450.f;

	// --- Soft Collision (companion-side self-push — F2 asymmetric blocking) ---

	/** AddMovementInput scale applied when the companion overlaps the player capsule. The player's
	 *  own push (which lets it pass through) lives on AExtractionPlayer::CompanionPushStrength. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|SoftCollision", meta = (ClampMin = "0.0"))
	float CompanionSelfPushStrength = 1.0f;

	/** Extra personal-space padding (cm) added on top of the combined capsule radii before the
	 *  self-push kicks in. */
	UPROPERTY(EditDefaultsOnly, Category = "Companion|SoftCollision", meta = (ClampMin = "0.0"))
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

	/** World time of the last committed-time natural cover release. */
	float LastNaturalReleaseTime = -1e9f;

	/** World time of the last combat-task cover commit (ExecuteTask cover entry). */
	float LastCoverCommitTime = -1e9f;

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
