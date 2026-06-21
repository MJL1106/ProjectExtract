// Base weapon actor — mesh, ammo, fire logic, hitscan, recoil.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionTypes.h"
#include "Weapon/KitWeaponInterface.h"
#include "WeaponBase.generated.h"

class USkeletalMeshComponent;
class UMeshComponent;
class UWeaponDataAsset;
class USuppressionComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponFired);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReloadComplete);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAmmoChanged, int32, CurrentAmmo, int32, ReserveAmmo);

UCLASS(Blueprintable)
class EXTRACTION_API AWeaponBase : public AActor, public IKitWeaponInterface
{
	GENERATED_BODY()

public:

	AWeaponBase();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ---- Fire Control ----

	void StartFiring();
	void StopFiring();
	bool CanFire() const;

	// ---- Reload ----

	void Reload();
	bool CanReload() const;

	/**
	 * Called by the AnimNotify_EnemyShellInserted notify once per shell seat.
	 * Authority-only. Ticks CurrentAmmo +1, decides whether another loop follows,
	 * advances both body and gun montages, and finalises the reload when the mag is full
	 * or reserve is dry. No-ops when bShellByShellReload is false on this weapon.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Reload")
	void HandleShellInserted();

	/**
	 * Interrupt an in-progress reload, keeping all shells already seated.
	 * Clears the safety timer, stops the gun-mesh loop montage, and returns state to Idle.
	 * No-op when not currently reloading.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Reload")
	void CancelReload();

	/**
	 * Sets both body and gun reload montages to self-loop their Loop section immediately after
	 * Montage_Play. Call once at montage start for shell-by-shell weapons so the first section
	 * boundary loops correctly independent of notify timing. No-op when bShellByShellReload is false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Reload")
	void PrimeShellReloadLoop();

	// ---- Recoil ----

	/** Called from character Tick to interpolate camera back after firing stops */
	void UpdateRecoilRecovery(float DeltaTime);

	/** Cancels recoil recovery (called when player moves mouse) */
	void CancelRecoilRecovery();

	// ---- Getters ----

	UFUNCTION(BlueprintPure, Category = "Weapon")
	EWeaponState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetCurrentAmmo() const { return CurrentAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetReserveAmmo() const { return ReserveAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	const UWeaponDataAsset* GetWeaponData() const { return WeaponData; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsFiring() const { return CurrentState == EWeaponState::Firing; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsReloading() const { return CurrentState == EWeaponState::Reloading; }

	USkeletalMeshComponent* GetWeaponMesh() const { return WeaponMesh; }

	/**
	 * Returns the skeletal mesh component that the enemy's left-hand IK should target.
	 * When a ThirdPersonVisualActor is spawned (all Infima enemies), that actor's first
	 * USkeletalMeshComponent is the visible gun — WeaponMesh is hidden and has no authored
	 * sockets. Falls back to WeaponMesh when no visual actor exists (weapons that use the frame
	 * mesh directly). Returns nullptr if neither source is valid.
	 */
	USkeletalMeshComponent* GetThirdPersonGripMesh() const;

	/** World-space location of the muzzle. Returns the MuzzleSocket if the weapon mesh has one, else the actor location. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FVector GetMuzzleLocation() const;

	/** Set by WeaponComponent when ADS state changes */
	void SetOwnerIsAiming(bool bAiming) { bOwnerIsAiming = bAiming; }

	/** Initialize ammo from data asset (called after spawn/equip) */
	void InitializeAmmo();

	/** Override the auto-reload flag set in the data asset. Enemies force this true so they never
	 *  go permanently silent — no BT reload task exists for enemies. */
	void SetAutoReloadOnEmpty(bool bEnable) { bAutoReloadOnEmpty = bEnable; }

	// ---- Magazine swap (enemy reload visual) ----

	/**
	 * Detaches the cached magazine component from the visual rifle actor and reparents it to
	 * HandSocket on HandMesh. Called by UAnimNotifyState_MagazineSwap::NotifyBegin.
	 * No-ops when MagazineComponentName is NAME_None (player kit weapons).
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Reload")
	void DetachMagazineToHand(USkeletalMeshComponent* HandMesh, FName HandSocket);

	/**
	 * Reparents the magazine back to its original well location inside the visual rifle actor.
	 * Called by UAnimNotifyState_MagazineSwap::NotifyEnd (fires on blend-out/interruption too).
	 * Idempotent — safe to call even when the mag was never detached.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Reload")
	void ReattachMagazine();

	/** Plays the per-weapon WeaponReload montage on the gun visual's WeaponMesh anim instance.
	 *  No-ops when no WeaponReload montage is set or the mesh/anim instance is unavailable.
	 *  PlayRate should match the character reload montage's effective rate to keep the mag pull synced. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Reload")
	void PlayVisualWeaponReload(float PlayRate = 1.f);

	/** Stops the per-weapon WeaponReload montage on the gun visual's WeaponMesh, blending it back
	 *  to idle so the magazine returns home when a reload is cancelled or interrupted. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Reload")
	void StopVisualWeaponReload(float BlendOutTime = 0.1f);

	/** Plays the per-weapon WeaponFire montage on the gun visual's WeaponMesh anim instance, restarting
	 *  it from the top each call so the bolt re-cocks every shot. No-ops when no WeaponFire montage is set
	 *  or the visual mesh/anim instance is unavailable. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	void PlayVisualWeaponFire(float PlayRate = 1.f);

	/** Stops the per-weapon WeaponFire montage on the gun visual's WeaponMesh, blending the bolt back
	 *  home so it doesn't freeze mid-cycle when firing stops or the enemy dies. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	void StopVisualWeaponFire(float BlendOutTime = 0.1f);

	// ---- Weapon fire alignment (enemy fire visual) ----

	/**
	 * One-time setup: captures the rest relative transform and computes the fire-align offset
	 * from the rest socket to FireSocket, both expressed in EnemyMesh component space.
	 * Call once per weapon equip when FireAlignSocketName is set on the ABP.
	 * No-ops if either socket is absent or WeaponMesh is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|FireAlign")
	void SetupFireAlign(USkeletalMeshComponent* EnemyMesh, FName FireSocket);

	/**
	 * Blends WeaponMesh's relative transform between rest (Alpha=0) and fire-aligned (Alpha=1).
	 * No-op when SetupFireAlign hasn't succeeded (bFireAlignReady=false).
	 * Call per-frame from the anim instance with an interpolated alpha.
	 * Safety reset: call with Alpha=0 on death or EndPlay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|FireAlign")
	void SetFireAlignAlpha(float Alpha);

	// ---- Weapon melee alignment (enemy melee visual) ----

	/**
	 * One-time setup: captures the rest relative transform and computes the melee-align offset
	 * from the rest socket to MeleeSocket, both expressed in EnemyMesh component space.
	 * Call once per weapon equip when MeleeAlignSocketName is set on the ABP.
	 * No-ops if either socket is absent or WeaponMesh is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|MeleeAlign")
	void SetupMeleeAlign(USkeletalMeshComponent* EnemyMesh, FName MeleeSocket);

	/**
	 * Blends WeaponMesh's relative transform between rest (Alpha=0) and melee-aligned (Alpha=1).
	 * No-op when SetupMeleeAlign hasn't succeeded (bMeleeAlignReady=false).
	 * Call per-frame from the anim instance with an interpolated alpha.
	 * Safety reset: call with Alpha=0 on death or EndPlay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|MeleeAlign")
	void SetMeleeAlignAlpha(float Alpha);

	/** True once SetupMeleeAlign succeeded (both sockets found). Lets the anim instance skip the
	 *  per-frame melee-align block entirely when the WeaponSocket_Melee socket isn't authored yet. */
	bool IsMeleeAlignReady() const { return bMeleeAlignReady; }

	// ---- Weapon recoil offset (enemy body-kick visual) ----

	/**
	 * Composes Offset onto WeaponMesh's rest relative transform and writes the result.
	 * SetRecoilOffset(FTransform::Identity) restores the exact rest pose.
	 * Rest is captured lazily on the first call (after BeginPlay, once the visual actor is attached).
	 * Currently unused: the enemy body-kick now translates the spine
	 * (UEnemyAnimInstance::RecoilSpineOffset), not the weapon mesh. Retained as public API for a
	 * future in-grip gun jolt (which would IK the right hand to the gun, not offset the mesh).
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Recoil")
	void SetRecoilOffset(const FTransform& Offset);

	// ---- Delegates ----

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponFired OnWeaponFired;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnReloadComplete OnReloadComplete;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnAmmoChanged OnAmmoChanged;

	// ---- IKitWeaponInterface (bridge for kit BP_FPCharacter dispatch) ----

	virtual void KitReload_Implementation() override;
	virtual void KitBeginFire_Implementation() override;
	virtual void KitStopFire_Implementation() override;
	virtual void KitFire_HitScan_Implementation() override;
	virtual void KitInspect_Implementation() override;
	virtual void KitMelee_Implementation() override;
	virtual void KitChangeFireMode_Implementation() override;
	virtual void KitBurstFire_Implementation() override;
	virtual void KitFinishFire_Implementation() override;
	virtual void KitTrigger_Implementation() override;
	virtual void KitSpawnAttachments_Implementation() override;
	virtual void KitUnequip_Implementation() override;
	virtual UDataAsset* GetKitProceduralValues_Implementation() const override;
	virtual FTransform GetKitIK_HandGunSocketOffset_Implementation() const override;
	virtual FTransform GetKitIK_HandRSocketOffset_Implementation() const override;
	virtual FTransform GetKitIK_HandLSocketOffset_Implementation() const override;
	virtual float GetKitAimDistanceFromCamera_Implementation() const override;
	virtual FVector GetKitMuzzleRingScale_Implementation() const override;
	virtual bool GetKitReloading_Implementation() const override;
	virtual bool GetKitIsFire_Implementation() const override;
	virtual void KitSetAmmo_Implementation(int32 AmmoCount, int32 MaxAmmo) override;
	virtual TSubclassOf<AActor> GetKitVisualWeaponClass_Implementation() const override;

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	/** Data asset with all tuning values for this weapon */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Config")
	TObjectPtr<UWeaponDataAsset> WeaponData;

	/** Optional pre-assembled visual weapon actor (e.g. an Infima ..._Default_Example weapon BP).
	 *  When set, this actor class is spawned and attached at the weapon root as the held visual,
	 *  and the skeletal WeaponMesh is hidden. Leave null for weapons that use WeaponMesh directly
	 *  (e.g. the player's kit weapons). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSubclassOf<AActor> ThirdPersonVisualActorClass;

	/** Runtime instance of ThirdPersonVisualActorClass, spawned in BeginPlay. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedVisualActor;

	/** When true, the weapon auto-reloads on empty (suitable for player UX).
	 *  AI-controlled weapons should set this false so the BT task drives reload timing —
	 *  the companion's reload-while-cover-returning flow needs BT control to avoid
	 *  capsule-resize-mid-reload-anim glitches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Reload")
	bool bAutoReloadOnEmpty = true;

	/**
	 * Name of the magazine StaticMeshComponent inside the spawned visual rifle actor
	 * (e.g. the Infima _Default_Example BP). Set this per enemy weapon BP; leave NAME_None
	 * for player kit weapons so the magazine swap code is completely bypassed.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Reload")
	FName MagazineComponentName = NAME_None;

	/** Name of the skeletal mesh component inside the visual actor that carries the magazine bone
	 *  (KINEMATION gun body — typically "WeaponMesh"). Used to play the weapon reload montage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Reload")
	FName WeaponVisualMeshName = TEXT("WeaponMesh");

	// ---- Replicated State ----

	UPROPERTY(ReplicatedUsing = OnRep_CurrentState, BlueprintReadOnly, Category = "Weapon|State")
	EWeaponState CurrentState;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentAmmo, BlueprintReadOnly, Category = "Weapon|State")
	int32 CurrentAmmo;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Weapon|State")
	int32 ReserveAmmo;

private:

	// ---- Magazine swap runtime state ----

	/** Weak ref to the magazine component found inside SpawnedVisualActor (null when MagazineComponentName is NAME_None). */
	TWeakObjectPtr<USceneComponent> CachedMagazineComp;

	/** Weak ref to the gun's WeaponMesh skeletal component inside SpawnedVisualActor (null until resolved). */
	TWeakObjectPtr<USkeletalMeshComponent> CachedWeaponVisualMesh;

	/** Original parent component of the magazine — used to reattach to the exact well. */
	TWeakObjectPtr<USceneComponent> MagazineHomeParent;

	/** Socket name on MagazineHomeParent the mag was originally attached to (often NAME_None for root). */
	FName MagazineHomeSocket = NAME_None;

	/** Relative transform of the mag inside the rifle actor — restored verbatim on ReattachMagazine. */
	FTransform MagazineHomeRelativeTransform = FTransform::Identity;

	/** True while the magazine is detached and riding the hand. */
	bool bMagazineDetached = false;

	// ---- Fire alignment runtime state ----

	/** Relative transform of WeaponMesh at rest (captured by SetupFireAlign). Alpha=0 target. */
	FTransform FireAlignRestRelative = FTransform::Identity;

	/** Pre-composed relative transform that lands the weapon at WeaponSocket_Fire. Alpha=1 target.
	 *  = FireAlignRestRelative * (T_fire * T_rest.Inverse()). Cached at setup; bone-pose-invariant. */
	FTransform FireAlignFireRelative = FTransform::Identity;

	/** True when SetupFireAlign succeeded — both sockets found and targets computed. */
	bool bFireAlignReady = false;

	// ---- Recoil offset runtime state ----

	/**
	 * WeaponMesh rest relative transform captured on the first SetRecoilOffset call.
	 * Lazy capture (not in BeginPlay) so the visual actor is guaranteed attached.
	 * SetRecoilOffset(Identity) restores this exactly.
	 */
	FTransform RecoilRestRelative = FTransform::Identity;

	/** True once RecoilRestRelative has been captured. */
	bool bRecoilRestCaptured = false;

	// ---- Melee alignment runtime state ----

	/** Relative transform of WeaponMesh at rest (captured by SetupMeleeAlign). Alpha=0 target. */
	FTransform MeleeAlignRestRelative = FTransform::Identity;

	/** Pre-composed relative transform that lands the weapon at WeaponSocket_Melee. Alpha=1 target.
	 *  = MeleeAlignRestRelative * (T_melee * T_rest.Inverse()). Cached at setup; bone-pose-invariant. */
	FTransform MeleeAlignMeleeRelative = FTransform::Identity;

	/** True when SetupMeleeAlign succeeded — both sockets found and targets computed. */
	bool bMeleeAlignReady = false;

	// ---- Fire ----

	void FireShot();
	void PerformHitscan();
	void OnAutoFireTimer();

	/** Rebuilds CachedFFIgnoreList from current world pawns sharing the owner's team. AI path only. */
	void RebuildFFIgnoreList();

	/** Rebuilds CachedSuppressionTargets: hostile pawns with USuppressionComponent. All shooters. */
	void RebuildSuppressionTargets();

	/** After hitscan trace, report near-misses to suppression-eligible pawns near the bullet segment. */
	void ReportNearMisses(const FVector& TraceStart, const FVector& TraceEnd, AActor* HitActor);

	// ---- Reload ----

	void OnReloadFinished();

	/** Redirects the next section of both body and gun reload montages.
	 *  bContinue=true → LoopSection→LoopSection; bContinue=false → LoopSection→EndSection. */
	void AdvanceShellReloadSection(bool bContinue);

	/** Finalises the shell-by-shell reload: clears the safety timer, sets state Idle,
	 *  broadcasts OnReloadComplete, and resumes fire if bWantsToFire. No ammo refill —
	 *  ammo was already counted incrementally by HandleShellInserted. */
	void FinishShellReload();

	/** Upper-bounds the total shell reload duration for the safety timer.
	 *  Derived from the body montage's section lengths and the effective play rate. */
	float GetShellReloadSafetyTimeout() const;

	/** Stops the body reload montage on the owning character's mesh. Used by OnReloadFinished
	 *  and CancelReload for shell-by-shell weapons whose Loop section is primed to self-loop. */
	void StopBodyReloadMontage(float BlendOut = 0.1f);

	// ---- Recoil ----

	void ApplyRecoil();
	void OnRecoilResetTimer();

	// ---- RPCs ----

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayFireFX(const FVector& MuzzleLocation, const FVector& EndPoint, bool bHit);

	// ---- RepNotify ----

	UFUNCTION()
	void OnRep_CurrentState();

	UFUNCTION()
	void OnRep_CurrentAmmo();

	// ---- Timers ----

	FTimerHandle AutoFireTimerHandle;
	FTimerHandle ReloadTimerHandle;
	FTimerHandle RecoilResetTimerHandle;

	// ---- Recoil State ----

	/** Current index into the recoil pattern */
	int32 RecoilIndex;

	/** True while fire input is held */
	bool bWantsToFire;

	/** Set by WeaponComponent — true while owner is ADS */
	bool bOwnerIsAiming;

	/** Accumulated recoil pitch for recovery */
	float AccumulatedRecoilPitch;

	/** Accumulated recoil yaw for recovery */
	float AccumulatedRecoilYaw;

	/** True while interpolating camera back after firing stops */
	bool bIsRecoveringRecoil;

	/** Prevents WEAPON-DRY spam — reset when reload completes or new fire cycle begins */
	bool bDryFireLogged = false;

	/** Time elapsed during recoil recovery */
	float RecoilRecoveryElapsed;

	/** Total pitch to recover */
	float RecoilRecoveryPitchTotal;

	/** Total yaw to recover */
	float RecoilRecoveryYawTotal;

	/** Pitch already recovered so far */
	float RecoilRecoveryPitchApplied;

	/** Yaw already recovered so far */
	float RecoilRecoveryYawApplied;

	/** World time when Reload() was last called (for RELOAD-FINISH elapsed diag). Server-only. */
	UPROPERTY(Transient)
	float ReloadStartTimeSeconds = 0.f;

	/** World time before which CanFire() is blocked after a reload completes (settle delay).
	 *  Set from WeaponData->PostReloadFireDelay on reload finish; 0 means no block. */
	UPROPERTY(Transient)
	float FireReadyTimeSeconds = 0.f;

	/** Resolved on BeginPlay: WeaponMesh if valid, else first USkeletalMeshComponent found. Avoids per-shot FindComponentByClass. */
	TWeakObjectPtr<UMeshComponent> CachedEffectiveMesh;

	/** Friendly-fire ignore list rebuilt once per StartFiring call. Per-shot TActorIterator is too expensive. */
	TArray<AActor*> CachedFFIgnoreList;
	/** World time when CachedFFIgnoreList was last built. Used for the 1s refresh during sustained fire. */
	float FFIgnoreListBuiltTime = -1e9f;

	/** Near-miss radius for suppression reporting (cm). Pawn within this distance of the bullet segment gets suppressed. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Suppression", meta = (ClampMin = "0.0"))
	float NearMissRadius = 150.f;

	struct FSuppressionTarget
	{
		TWeakObjectPtr<APawn> Pawn;
		TWeakObjectPtr<USuppressionComponent> Component;
	};

	/** Hostile pawns with USuppressionComponent, rebuilt per burst (mirrors CachedFFIgnoreList pattern). */
	TArray<FSuppressionTarget> CachedSuppressionTargets;
	/** World time when CachedSuppressionTargets was last built. */
	float SuppressionTargetsBuiltTime = -1e9f;
};
