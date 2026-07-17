// Character component — equip, ADS, Server RPCs for weapon system.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponComponent.generated.h"

class AWeaponBase;
class IExtractionPlayerInterface;
enum class EWeaponReloadPhase : uint8;

/** Broadcast per actual shot with stealth exemption context. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerWeaponShot, bool, bStealthExempt);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCurrentWeaponChangedNative, AWeaponBase*);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnWeaponBooleanStateChangedNative, bool);
DECLARE_MULTICAST_DELEGATE(FOnWeaponShotNative);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnWeaponAmmoChangedNative, int32, int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnWeaponReloadPhaseRelayNative, EWeaponReloadPhase);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EXTRACTION_API UWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UWeaponComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- Weapon Control ----

	/** Spawn and equip a weapon by class */
	void EquipWeapon(TSubclassOf<AWeaponBase> WeaponClass);

	/** Begin firing. bAuthorityTakedownSnapshot = true when the authority has confirmed a
	 *  companion shoot-takedown is armed at trigger-pull time (first shot only is exempt). */
	void StartFire(bool bAuthorityTakedownSnapshot = false);
	void StopFire();
	void StartReload();
	void SetAiming(bool bNewAiming);

	// ---- Events ----

	/** Per-shot relay with stealth discipline exemption flag. Authority-only. */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnPlayerWeaponShot OnPlayerWeaponShot;

	/** Native presentation-facing state stream. The gameplay methods above remain authoritative. */
	FOnCurrentWeaponChangedNative OnCurrentWeaponChangedNative;
	FOnWeaponBooleanStateChangedNative OnAimingChangedNative;
	FOnWeaponBooleanStateChangedNative OnTriggerChangedNative;
	FOnWeaponShotNative OnWeaponShotNative;
	FOnWeaponAmmoChangedNative OnWeaponAmmoChangedNative;
	FOnWeaponReloadPhaseRelayNative OnWeaponReloadPhaseChangedNative;

	// ---- Getters ----

	UFUNCTION(BlueprintPure, Category = "Weapon")
	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsAiming() const { return bIsAiming; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsTriggerHeld() const { return bTriggerHeld; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Weapon class to spawn on BeginPlay */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Config")
	TSubclassOf<AWeaponBase> DefaultWeaponClass;

private:

	// ---- Replicated State ----

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<AWeaponBase> CurrentWeapon;

	/** Previous weapon — cached in OnRep so we can detach it if a swap occurs without destroy */
	UPROPERTY()
	TObjectPtr<AWeaponBase> PreviousWeapon;

	UPROPERTY(ReplicatedUsing = OnRep_IsAiming)
	bool bIsAiming;

	/** Local input edge state used only by the normalized presentation event stream. */
	bool bTriggerHeld = false;

	/** True for the FIRST shot of a trigger pull that coincides with a companion shoot-takedown.
	 *  Consumed (cleared) on the first OnWeaponFiredCallback. */
	bool bNextShotStealthExempt = false;

	// ---- Server RPCs ----

	UFUNCTION(Server, Reliable)
	void Server_StartFire();

	UFUNCTION(Server, Reliable)
	void Server_StopFire();

	UFUNCTION(Server, Reliable)
	void Server_Reload();

	UFUNCTION(Server, Reliable)
	void Server_SetAiming(bool bNewAiming);

	// ---- Multicast ----

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_OnFired();

	// ---- RepNotify ----

	UFUNCTION()
	void OnRep_CurrentWeapon();

	UFUNCTION()
	void OnRep_IsAiming();

	/** Bound to weapon's OnWeaponFired — triggers multicast for 3P effects */
	UFUNCTION()
	void OnWeaponFiredCallback();

	UFUNCTION()
	void OnWeaponAmmoChangedCallback(int32 CurrentAmmo, int32 ReserveAmmo);

	void OnWeaponReloadPhaseChangedCallback(EWeaponReloadPhase Phase);
	void BindWeaponEvents(AWeaponBase* Weapon);
	void UnbindWeaponEvents(AWeaponBase* Weapon);

	/** Re-seats the weapon after SnapToTarget so GripSocket coincides with ik_hand_gun. */
	void SeatWeaponGripSocket();

	/** Resolves the companion on the server for shoot-takedown snapshot. */
	bool ResolveServerTakedownSnapshot();

	/** Cached owner actor (GC-safe UPROPERTY anchor) */
	UPROPERTY()
	TObjectPtr<AActor> OwnerActor;

	/** Interface pointer into the same UObject — valid as long as OwnerActor is alive */
	IExtractionPlayerInterface* OwnerIface = nullptr;
};
