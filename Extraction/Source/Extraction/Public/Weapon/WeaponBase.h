// Base weapon actor — mesh, ammo, fire logic, hitscan, recoil.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExtractionTypes.h"
#include "WeaponBase.generated.h"

class UStaticMeshComponent;
class UWeaponDataAsset;
class AExtractionCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponFired);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReloadComplete);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAmmoChanged, int32, CurrentAmmo, int32, ReserveAmmo);

UCLASS(Blueprintable)
class EXTRACTION_API AWeaponBase : public AActor
{
	GENERATED_BODY()

public:

	AWeaponBase();

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

	UStaticMeshComponent* GetWeaponMesh() const { return WeaponMesh; }

	/** World-space location of the muzzle. Returns the MuzzleSocket if the weapon mesh has one, else the actor location. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FVector GetMuzzleLocation() const;

	/** Set by WeaponComponent when ADS state changes */
	void SetOwnerIsAiming(bool bAiming) { bOwnerIsAiming = bAiming; }

	/** Initialize ammo from data asset (called after spawn/equip) */
	void InitializeAmmo();

	// ---- Delegates ----

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponFired OnWeaponFired;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnReloadComplete OnReloadComplete;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnAmmoChanged OnAmmoChanged;

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	/** Data asset with all tuning values for this weapon */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Config")
	TObjectPtr<UWeaponDataAsset> WeaponData;

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

	// ---- Reload ----

	void OnReloadFinished();

	// ---- Recoil ----

	void ApplyRecoil();
	void OnRecoilResetTimer();

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
};
