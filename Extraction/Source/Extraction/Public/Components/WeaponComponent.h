// Character component — equip, ADS, Server RPCs for weapon system.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponComponent.generated.h"

class AWeaponBase;
class IExtractionPlayerInterface;

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

	void StartFire();
	void StopFire();
	void StartReload();
	void SetAiming(bool bNewAiming);

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

private:

	// ---- Replicated State ----

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<AWeaponBase> CurrentWeapon;

	/** Previous weapon — cached in OnRep so we can detach it if a swap occurs without destroy */
	UPROPERTY()
	TObjectPtr<AWeaponBase> PreviousWeapon;

	UPROPERTY(ReplicatedUsing = OnRep_IsAiming)
	bool bIsAiming;

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

	/** Cached owner actor (GC-safe UPROPERTY anchor) */
	UPROPERTY()
	TObjectPtr<AActor> OwnerActor;

	/** Interface pointer into the same UObject — valid as long as OwnerActor is alive */
	IExtractionPlayerInterface* OwnerIface = nullptr;
};
