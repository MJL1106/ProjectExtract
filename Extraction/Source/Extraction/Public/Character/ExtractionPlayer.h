// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Camera/CameraTypes.h"
#include "ExtractionTypes.h"
#include "Logging/LogMacros.h"
#include "Character/ExtractionPlayerInterface.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Perception/AISightTargetInterface.h"
#include "ExtractionPlayer.generated.h"

class AWeaponBase;
class AEnemyCharacter;
class ACompanionCharacter;
class UInputComponent;
class UInputAction;
class UInputMappingContext;
class UExtractionAnimInstance;
class UHealthComponent;
class UFootstepNoiseComponent;
class UWeaponComponent;
class UTraversalComponent;
class UCompanionCommandComponent;
class UAnimMontage;
class USpringArmComponent;
struct FInputActionValue;

// Distinct name from the legacy AExtractionCharacter declaration to avoid linker conflicts during the migration period.
// Rename to FOnDBNOStateChanged once AExtractionCharacter is retired (Phase 5).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlayerDBNOStateChanged, bool, bNewIsDBNO, float, BleedoutDuration);

/** Broadcast the moment the player commits their own takedown (both montage and instant paths). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerTakedownCommitted);

/** Broadcast the moment the player pulls their fire trigger. Companion shoot-takedowns listen here for sync. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerFiredWeapon);

/**
 * Minimal C++ base for the kit-reparented player Blueprint.
 * The BP (duplicate of BP_FPCharacter) owns mesh, camera, spring arm, slide,
 * sprint, crouch, jump, and all kit procedural components. This class owns
 * gameplay components (health, weapon, traversal), DBNO/revive state, hit-region
 * damage routing, and the input handlers the kit doesn't provide.
 */
UCLASS()
class EXTRACTION_API AExtractionPlayer : public ACharacter, public IExtractionPlayerInterface, public IGameplayTagAssetInterface, public IGenericTeamAgentInterface, public IAISightTargetInterface
{
	GENERATED_BODY()

public:

	AExtractionPlayer();

	virtual void PostInitializeComponents() override;

	// --- IGameplayTagAssetInterface ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	// --- IGenericTeamAgentInterface ---
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(0); }

	// --- IAISightTargetInterface ---
	// Head bone excluded: only pelvis/chest/neck clear LOS for detection, matching the aim and fire-gate resolvers.
	virtual UAISense_Sight::EVisibilityResult CanBeSeenFrom(const FCanBeSeenFromContext& Context, FVector& OutSeenLocation, int32& OutNumberOfLoSChecksPerformed, int32& OutNumberOfAsyncLosCheckRequested, float& OutSightStrength, int32* UserData = nullptr, const FOnPendingVisibilityQueryProcessedDelegate* Delegate = nullptr) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

	// ---- Delegates ----

	UPROPERTY(BlueprintAssignable, Category = "Health|Events")
	FOnPlayerDBNOStateChanged OnDBNOStateChanged;

	/** Fires the moment the player commits their own takedown. Companion BT tasks listen here for sync. */
	UPROPERTY(BlueprintAssignable, Category = "Takedown|Events")
	FOnPlayerTakedownCommitted OnPlayerTakedownCommitted;

	/** Fires the moment the player pulls their fire trigger. Companion shoot-takedowns listen here for sync. */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnPlayerFiredWeapon OnPlayerFiredWeapon;

	// ---- BlueprintImplementableEvents ----

	/** Fired locally after the owning client receives the equipped weapon (or on server after equip).
	 *  BP implements this to call AC_ProceduralAnimation->NewHandPose. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Events")
	void OnWeaponEquipped(AWeaponBase* EquippedWeapon);

	/** Fired locally when ADS state changes (input down = true, input up = false).
	 *  BP implements this to call AC_ProceduralAnimation->NewHandPose with Aim/Base pose. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon|Events")
	void OnADSChanged(bool bIsADS);

	/** Fired once after Montage_Play succeeds and the end delegate is bound.
	 *  BP uses this to lock the camera, hide the gun, and show the knife. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Takedown|Events")
	void OnTakedownStarted(AEnemyCharacter* Victim);

	/** Fired once when the finisher montage ends (natural end or interrupt).
	 *  BP uses this to restore the camera, show the gun, and hide the knife. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Takedown|Events")
	void OnTakedownFinished();

	// ---- Input handlers (BlueprintCallable so kit BP can delegate if needed) ----

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoAim(float Yaw, float Pitch) override;

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoMove(float Right, float Forward);

	// ---- Revive API ----

	/** Exit DBNO state and restore health/movement. Called server-side. */
	virtual void ExitDBNO() override;

	/** Plays/stops the being-revived montage on the body mesh while a reviver holds the revive.
	 *  ExpectedDuration > 0 rate-scales the montage to span the hold exactly. Also locks look/move
	 *  input and suspends controller-yaw follow so AlignForRevive's facing sticks. */
	virtual void SetBeingRevived(bool bBeingRevived, float ExpectedDuration = 0.f) override;

	/** Rotates the downed body to face the reviver for the paired revive anims. */
	virtual void AlignForRevive(const FVector& ReviverLocation) override;

	/** Diagnostic: true while BeingRevivedMontage is playing on the body mesh. */
	virtual bool IsBeingRevivedMontagePlaying() const override;

	virtual const UAnimMontage* GetBeingRevivedMontage() const override { return BeingRevivedMontage; }

	/** Montage-driven revive completion: fires when the being-revived get-up montage starts blending
	 *  out. A natural end (not interrupted) completes the revive at the exact frame the character is
	 *  upright — ExitDBNO here instead of on the reviver's timer, killing the "blends back down to
	 *  crawl, then pops up" seam. Interrupted = stomped/aborted; logged, no exit. */
	void OnBeingRevivedMontageBlendOut(UAnimMontage* Montage, bool bInterrupted);

	// ---- IExtractionPlayerInterface ----

	UFUNCTION(BlueprintPure, Category = "Components")
	virtual UHealthComponent* GetHealthComponent() const override { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Components")
	virtual UWeaponComponent* GetWeaponComponent() const override { return WeaponComponent; }

	UFUNCTION(BlueprintPure, Category = "Components")
	virtual UTraversalComponent* GetTraversalComponent() const override { return TraversalComponent; }

	UFUNCTION(BlueprintPure, Category = "Health")
	virtual bool GetIsDBNO() const override { return bIsDBNO; }

	UFUNCTION(BlueprintPure, Category = "Health")
	virtual float GetBleedoutTimeRemaining() const override { return BleedoutTimeRemaining; }

	UFUNCTION(BlueprintPure, Category = "Animation")
	virtual UExtractionAnimInstance* GetExtractionAnimInstance() const override { return CachedAnimInstance; }

	virtual ETraversalType GetActiveTraversalType() const override;
	virtual bool IsInTraversal() const override;

	UFUNCTION(BlueprintPure, Category = "Movement")
	virtual bool GetIsVaulting() const override;

	UFUNCTION(BlueprintPure, Category = "Movement")
	virtual FVector GetVaultTargetLocation() const override;

	UFUNCTION(BlueprintPure, Category = "Movement")
	virtual float GetVaultSurfaceHeight() const override;

	/** Normalized auto-lean output: -1 = lean left, 0 = none, +1 = lean right.
	 *  Smoothed each frame; set by UpdateAutoLean when ADS and a wall is detected. */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Lean")
	float AutoLeanAlpha = 0.f;

	UFUNCTION(BlueprintPure, Category = "Movement")
	virtual float GetAutoLeanAlpha() const override { return AutoLeanAlpha; }

	/**
	 * Try to start a traversal at the player's current location. Intended to be called
	 * from the kit BP's Jump handler before invoking Jump() — if this returns true,
	 * the jump should be skipped because a traversal is now playing.
	 *
	 * Returns true if a traversal was initiated, false if no obstacle was detected
	 * or the player is in a state that blocks traversal (DBNO, already in traversal,
	 * falling).
	 */
	UFUNCTION(BlueprintCallable, Category = "Movement|Traversal")
	bool TryStartTraversal();

	/** No WeaponSpawn component on this class — kit BP attaches via socket directly. */
	virtual USceneComponent* GetWeaponSpawn() const override { return nullptr; }

	virtual void NotifyWeaponEquipped(AWeaponBase* EquippedWeapon) override { OnWeaponEquipped(EquippedWeapon); }
	virtual void NotifyADSChanged(bool bIsADS) override { OnADSChanged(bIsADS); }

	/** Called by UAnimNotify_TakedownKill at the death frame of the finisher montage.
	 *  Also serves as the fallback when the montage ends/interrupts before the notify fires. */
	void FinishPendingTakedown();

	/** True while the takedown finisher montage is playing.
	 *  AnimBP reads this to gate the procedural FP-arm layer off during the finisher. */
	UFUNCTION(BlueprintPure, Category = "Takedown")
	virtual bool IsInTakedown() const override { return bTakedownMontageActive; }

protected:

	// ---- Gameplay Components ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFootstepNoiseComponent> FootstepNoiseComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTraversalComponent> TraversalComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCompanionCommandComponent> CompanionCommandComponent;

	// ---- Input Mapping Context (assigned in BP child class) ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	// ---- Input Actions (assigned in BP child class) ----

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> ADSAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> VaultAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	TObjectPtr<UInputAction> TakedownAction;

	// ---- Companion Command Input Actions (assigned in BP child class) ----

	/** Middle-mouse ping — camera traces and classifies a pending companion command. */
	UPROPERTY(EditAnywhere, Category = "Input|Companion")
	TObjectPtr<UInputAction> IA_CompanionPing;

	/** Confirm a companion takedown with knife method. */
	UPROPERTY(EditAnywhere, Category = "Input|Companion")
	TObjectPtr<UInputAction> IA_CompanionTakedownKnife;

	/** Confirm a companion takedown with shoot method. */
	UPROPERTY(EditAnywhere, Category = "Input|Companion")
	TObjectPtr<UInputAction> IA_CompanionTakedownShoot;

	/** Confirm a companion breach command. */
	UPROPERTY(EditAnywhere, Category = "Input|Companion")
	TObjectPtr<UInputAction> IA_CompanionBreach;

	/** Cycle companion mode Normal -> Combat -> Stealth (X key). */
	UPROPERTY(EditAnywhere, Category = "Input|Companion")
	TObjectPtr<UInputAction> IA_CompanionModeToggle;

	// ---- Takedown Config ----

	/** Player-side finisher montage. Assign in the BP child class.
	 *  When null, the takedown kills instantly (current behavior).
	 *  When set, death fires from UAnimNotify_TakedownKill placed at the correct frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown")
	TObjectPtr<UAnimMontage> TakedownMontage;

	/** When true, the victim is snapped to PlayerLocation + PlayerForward * TakedownVictimForwardOffset
	 *  so the finisher montage lines up. Disable if your montage uses root motion instead. */
	UPROPERTY(EditAnywhere, Category = "Takedown")
	bool bAlignTakedownVictim = true;

	/** Distance (cm) in front of the player to place the victim when bAlignTakedownVictim is true. */
	UPROPERTY(EditAnywhere, Category = "Takedown", meta = (ClampMin = "0.0", EditCondition = "bAlignTakedownVictim"))
	float TakedownVictimForwardOffset = 90.f;

	/** Near clip plane (cm) applied to the player view while a takedown finisher montage plays, so the head-bone camera does not render the victim's interior geometry at point blank. Set <= 0 to disable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Takedown")
	float TakedownNearClipPlane = 2.f;

	// ---- DBNO / Revive Config ----

	/** Movement speed while downed (DBNO crawl). */
	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "10.0"))
	float DBNOCrawlSpeed = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "1.0"))
	float BleedoutDuration = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "0.5"))
	float ReviveDuration = 4.f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float ReviveHealthPercent = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "50.0"))
	float ReviveProximityRadius = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "50.0"))
	float ReviveTraceDistance = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "5.0"))
	float ReviveTraceSphereRadius = 30.f;

	/** Single-shot get-up montage played on the body mesh while someone is actively reviving this
	 *  player — designer assigns in BP. Missing = no being-revived anim (revive still works via the
	 *  fallback timer). Must be NON-looping: its natural blend-out is what completes the revive
	 *  (OnBeingRevivedMontageBlendOut → ExitDBNO); a looping asset would never fire it. */
	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO")
	TObjectPtr<UAnimMontage> BeingRevivedMontage;

	/** Seconds of full damage immunity after a revive — without it a mid-burst enemy re-downs the
	 *  player the frame they stand up. */
	UPROPERTY(EditDefaultsOnly, Category = "Health|DBNO", meta = (ClampMin = "0.0"))
	float PostReviveDamageGraceSeconds = 3.f;

	// ---- Auto-Lean Config ----

	/** Sideways distance (cm) from probe origin to test for a wall. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Lean", meta = (ClampMin = "10.0"))
	float LeanProbeDistance = 70.f;

	/** Sphere radius for each lateral wall probe — matches traversal trace sizing. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Lean", meta = (ClampMin = "1.0"))
	float LeanProbeRadius = 15.f;

	/** Z offset from camera/eye position for the probe origin. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Lean")
	float LeanProbeVerticalOffset = 0.f;

	/** The open side must be clear at least this far (cm) before leaning toward it.
	 *  Must exceed LeanProbeDistance — the open-side sweep is this long, and a side only counts as a wall
	 *  when its hit is within LeanProbeDistance, so values <= LeanProbeDistance make the open test inert. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Lean", meta = (ClampMin = "10.0"))
	float LeanGapClearance = 120.f;

	/** FInterpTo speed used to smooth AutoLeanAlpha toward the target each frame. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Lean", meta = (ClampMin = "0.1"))
	float AutoLeanInterpSpeed = 8.f;

	/** How often (seconds) the lateral wall probes are fired — ~20 Hz default. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Lean", meta = (ClampMin = "0.01"))
	float LeanProbeInterval = 0.05f;

	/** Maximum magnitude of AutoLeanAlpha (0..1). Reduce to limit lean strength. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Lean", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxAutoLeanMagnitude = 1.f;

	/** Draw debug spheres and lines for the lateral wall probes in editor/dev builds. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Lean")
	bool bDrawLeanDebug = false;

	// ---- Companion Soft Collision ----

	/** AddMovementInput scale applied when the player overlaps the companion capsule. Values above 1 push harder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|SoftCollision", meta = (ClampMin = "0.0"))
	float CompanionPushStrength = 1.75f;

	/** Extra personal-space padding (cm) added on top of the combined capsule radii before the push kicks in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|SoftCollision", meta = (ClampMin = "0.0"))
	float CompanionPushPadding = 0.f;

	// ---- Hitbox Config ----

	/** Maps skeleton bone names to hit regions for damage multiplier lookup.
	 *  Defaults to UE5 mannequin bones. Override in Blueprint for custom skeletons. */
	UPROPERTY(EditDefaultsOnly, Category = "Damage|Hitbox")
	TMap<FName, EHitRegion> BoneToHitRegionMap;

	// ---- Replicated State ----

	UPROPERTY(ReplicatedUsing = OnRep_IsDBNO, BlueprintReadOnly, Category = "Health|State")
	bool bIsDBNO = false;

	UPROPERTY(BlueprintReadOnly, Category = "Health|State")
	float BleedoutTimeRemaining = 0.f;

private:

	// ---- Companion Soft Collision State ----

	/** Tracks which companion instance has its IgnoreActorWhenMoving wired, so respawns re-wire correctly. */
	TWeakObjectPtr<ACompanionCharacter> WiredCompanion;

	void UpdateCompanionSoftCollision();

	// ---- Auto-Lean State ----

	/** True while ADS is active; gates lean probe accumulation. */
	bool bAutoLeanActive = false;

	/** Last computed lean direction fed into the per-frame interp. */
	float AutoLeanTargetAlpha = 0.f;

	/** Accumulates DeltaTime; fires UpdateAutoLean once per LeanProbeInterval. */
	float LeanProbeAccumulator = 0.f;

	/** Fire two lateral sphere sweeps and write AutoLeanTargetAlpha. */
	void UpdateAutoLean(float DeltaTime);

	// ---- Input Handlers ----

	void MoveInput(const FInputActionValue& Value);
	void LookInput(const FInputActionValue& Value);

	void FireStart(const FInputActionValue& Value);
	void FireStop(const FInputActionValue& Value);
	void ReloadStart(const FInputActionValue& Value);
	void ADSStart(const FInputActionValue& Value);
	void ADSStop(const FInputActionValue& Value);

	void VaultStart(const FInputActionValue& Value);

	void InteractStart(const FInputActionValue& Value);
	void InteractStop(const FInputActionValue& Value);

	/** Camera-forward interact trace: loots ILootable containers, keycard-unlocks locked doors.
	 *  Returns true when the press was consumed by a world interaction (revive check is skipped). */
	bool TryWorldInteract();

	/** Max distance (cm) for the Interact world trace. */
	UPROPERTY(EditAnywhere, Category = "Interaction", meta = (ClampMin = "0.0"))
	float InteractTraceRange = 250.f;

	void TakedownInput(const FInputActionValue& Value);
	void StartMontageDeferred(AEnemyCharacter* Victim);

	// ---- Companion command input handlers ----
	void CompanionPingInput(const FInputActionValue& Value);
	void CompanionConfirmTakedownKnifeInput(const FInputActionValue& Value);
	void CompanionConfirmTakedownShootInput(const FInputActionValue& Value);
	void CompanionConfirmBreachInput(const FInputActionValue& Value);
	void CompanionModeToggleInput(const FInputActionValue& Value);

	UFUNCTION()
	void OnTakedownMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// ---- Traversal ----

	void HandleTraversalStarted(ETraversalType Type, float PlayRate, FVector ObstacleLocation, FVector LandingLocation);
	void HandleTraversalEnded();

	UFUNCTION()
	void OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// ---- Health / DBNO ----

	UFUNCTION()
	void HandleDeath();

	void EnterDBNO();
	void OnBleedoutExpired();
	void FullDeath();

	/** While downed, the BP spring arm (which inherits head-bone pitch/yaw for the procedural FP
	 *  camera) switches to pawn control rotation so the crawl/revive anims can't drag the view.
	 *  Local-only cosmetic; restores the saved flag on revive. */
	void SetDBNOCameraFreeLook(bool bEnable);

	/** BP-owned spring arm, resolved lazily by class (C++ has no camera components). */
	TWeakObjectPtr<USpringArmComponent> CachedSpringArm;

	bool bDBNOFreeLookActive = false;
	bool bSavedSpringArmUsePawnControlRotation = false;

	/** True while a reviver holds the revive on this player; gates the montage against re-triggers,
	 *  locks look/move input (DoAim/DoMove), and marks controller-yaw follow as suspended. */
	bool bBeingRevivedAnimActive = false;

	/** bUseControllerRotationYaw as it was before the being-revived lock suspended it. */
	bool bSavedUseControllerRotationYaw = false;

	float GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent) const;

	UFUNCTION()
	void OnRep_IsDBNO();

	/** Temp debug: apply 25 damage to self (bound to H key) */
	void DebugApplyDamage();

	/** Pre-DBNO crouched speed, restored on ExitDBNO. */
	float SavedMaxWalkSpeedCrouched = 0.f;

	/** World time of the last revive — drives the post-revive damage grace. */
	float LastReviveWorldTime = -1e9f;

	FTimerHandle BleedoutTimerHandle;

#if !UE_BUILD_SHIPPING
	// Edge-triggered map: tracks the last logged CanBeSeenFrom result per observer to avoid log spam.
	TMap<TWeakObjectPtr<const AActor>, bool> DebugLastCanBeSeenResult;
#endif

	// ---- Companion Debug Exec Commands ----
	// (UHT forbids UFUNCTION inside preprocessor blocks — kept unguarded; console exec is dev-only at runtime.)

	/** Resolve the companion to drive from console (cached wired ref, else first in world). */
	ACompanionCharacter* ResolveDebugCompanion() const;

	/** console: CompAim 1|0 — force the companion into/out of aim (ADS grip pose). */
	UFUNCTION(Exec)
	void CompAim(bool bEnable);

	/** console: CompFire 1|0 — start/stop the companion firing. */
	UFUNCTION(Exec)
	void CompFire(bool bEnable);

	/** console: CompReload — trigger a companion reload. */
	UFUNCTION(Exec)
	void CompReload();

	/** console: CompLowReady 1|0 — lower/raise the companion's weapon. */
	UFUNCTION(Exec)
	void CompLowReady(bool bEnable);

	// console: CompDebug 1 — pause the companion's AI so CompAim/CompFire/CompReload stick; CompDebug 0 — resume.
	UFUNCTION(Exec)
	void CompDebug(bool bFreeze);

	/** console: PlayerDown — force this player into DBNO (zeroes health via the normal death path)
	 *  so the companion revive can be tested on demand. */
	UFUNCTION(Exec)
	void PlayerDown();

	// ---- Takedown state ----

	/** Victim held during a montage-deferred takedown. Cleared after kill or montage abort. */
	TWeakObjectPtr<AEnemyCharacter> PendingTakedownVictim;

	/** Whether a takedown montage is currently playing (guards against double-finish). */
	bool bTakedownMontageActive = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Tags")
	FGameplayTagContainer OwnedTags;

	UPROPERTY()
	TObjectPtr<UExtractionAnimInstance> CachedAnimInstance;

	// ---- Revive ----

	void UpdateRevive(float DeltaTime);
	AExtractionPlayer* FindReviveTarget() const;
	void CancelRevive();
	void CompleteRevive();

	UFUNCTION(Server, Reliable)
	void Server_CompleteRevive(AExtractionPlayer* Target);

	UPROPERTY()
	TObjectPtr<AExtractionPlayer> ReviveTarget;

	float ReviveElapsed = 0.f;
	bool bIsReviving = false;
};
