// Character component — equip, ADS, Server RPCs for weapon system.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponComponent.generated.h"

class AWeaponBase;
class IExtractionPlayerInterface;

/** Broadcast per actual shot with stealth exemption context. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerWeaponShot, bool, bStealthExempt);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EXTRACTION_API UWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UWeaponComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- Weapon Control ----

	/** Spawn and equip a weapon into the primary slot by class */
	void EquipWeapon(TSubclassOf<AWeaponBase> WeaponClass);

	/** Activate the primary/secondary slot weapon (1 / 2 keys). Server-authoritative;
	 *  blocked while the current weapon is reloading or the owner is DBNO/in a takedown. */
	void SwitchToPrimary();
	void SwitchToSecondary();

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

	// ---- Getters ----

	UFUNCTION(BlueprintPure, Category = "Weapon")
	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsAiming() const { return bIsAiming; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Weapon class to spawn on BeginPlay */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Config")
	TSubclassOf<AWeaponBase> DefaultWeaponClass;

	/** Secondary-slot weapon class spawned on BeginPlay (2 key). None = single-weapon loadout. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Config")
	TSubclassOf<AWeaponBase> DefaultSecondaryWeaponClass;

private:

	// ---- Replicated State ----

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<AWeaponBase> CurrentWeapon;

	/** Slot weapons — both spawned on BeginPlay, both persist their own ammo across switches. */
	UPROPERTY(Replicated)
	TObjectPtr<AWeaponBase> PrimaryWeapon;

	UPROPERTY(Replicated)
	TObjectPtr<AWeaponBase> SecondaryWeapon;

	/** Previous weapon — cached in OnRep so we can detach it if a swap occurs without destroy */
	UPROPERTY()
	TObjectPtr<AWeaponBase> PreviousWeapon;

	UPROPERTY(ReplicatedUsing = OnRep_IsAiming)
	bool bIsAiming;

	/** True for the FIRST shot of a trigger pull that coincides with a companion shoot-takedown.
	 *  Consumed (cleared) on the first OnWeaponFiredCallback. */
	bool bNextShotStealthExempt = false;

	/** Spawn + attach + init a slot weapon. Spawns hidden — SetActiveWeapon unhides on activation. */
	AWeaponBase* SpawnWeaponActor(TSubclassOf<AWeaponBase> WeaponClass);

	/** Authority: make NewWeapon the held weapon. Hides/silences the old slot, rebinds the fire
	 *  delegate, syncs aim state, and fires the kit-visual equip flow. Never re-inits ammo. */
	void SetActiveWeapon(AWeaponBase* NewWeapon);

	// ---- Server RPCs ----

	UFUNCTION(Server, Reliable)
	void Server_SwitchWeapon(uint8 SlotIndex);

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
