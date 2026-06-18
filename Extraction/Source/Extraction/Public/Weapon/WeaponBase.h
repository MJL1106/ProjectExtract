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

	// ---- Replicated State ----

	UPROPERTY(ReplicatedUsing = OnRep_CurrentState, BlueprintReadOnly, Category = "Weapon|State")
	EWeaponState CurrentState;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentAmmo, BlueprintReadOnly, Category = "Weapon|State")
	int32 CurrentAmmo;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Weapon|State")
	int32 ReserveAmmo;

private:

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
