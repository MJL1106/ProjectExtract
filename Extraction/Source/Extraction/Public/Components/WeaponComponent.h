// Character component — equip, ADS, Server RPCs for weapon system.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionTypes.h"
#include "WeaponComponent.generated.h"

class AWeaponBase;
class IExtractionPlayerInterface;
enum class EEnemyWeaponAnimType : uint8;

/** Broadcast per actual shot with stealth exemption context. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerWeaponShot, bool, bStealthExempt);

/** Broadcast when the EFFECTIVE active weapon changes. Null means "no C++ hitscan weapon in
 *  hand" — an empty slot, a failed spawn, or the kit throwable being out. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveWeaponChanged, AWeaponBase*, NewWeapon);

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
	 *  cancels any in-progress reload on the current weapon. Blocked while DBNO/in a takedown. */
	void SwitchToPrimary();
	void SwitchToSecondary();

	/** Activate a weapon slot by enum. Authority-only; refused while DBNO/in a takedown.
	 *  No-ops for None or when the slot is empty. Used by the checkpoint restore to select
	 *  the exact slot the player had out, independent of the kit's SwapWeapon pipeline. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void ActivateSlot(EWeaponSlot Slot);

	/** Replace a slot's weapon with a new class (weapon-pickup swap). Authority-only; cancels
	 *  any in-progress reload on the held weapon. Refused while DBNO/in a takedown. Mag/Reserve < 0
	 *  keep the new weapon's data-asset defaults. Activates the new weapon if the replaced slot
	 *  was the held one. Returns the spawned weapon, or null when refused. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	AWeaponBase* ReplaceSlotWeapon(bool bPrimarySlot, TSubclassOf<AWeaponBase> NewWeaponClass, int32 Mag = -1, int32 Reserve = -1);

	/** Mark the kit throwable (grenade) slot equipped/unequipped — called by the char BP's kit
	 *  swap pipeline. While set, fire/reload/ADS divert away from the C++ hitscan weapon and the
	 *  char BP routes fire to the kit item. Cancels any in-progress reload on the held weapon.
	 *  Returns false (and changes nothing) when refused: DBNO or takedown. Cleared automatically
	 *  on any real weapon switch. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Throwable")
	bool SetThrowableEquipped(bool bEquipped);

	UFUNCTION(BlueprintPure, Category = "Weapon|Throwable")
	bool IsThrowableEquipped() const { return bThrowableEquipped; }

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

	/** Edge-only relay of the effective active weapon — the HUD binds here instead of polling
	 *  GetCurrentWeapon every frame. Fires with null when the hand empties. */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnActiveWeaponChanged OnActiveWeaponChanged;

	// ---- Getters ----

	UFUNCTION(BlueprintPure, Category = "Weapon")
	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	AWeaponBase* GetPrimaryWeapon() const { return PrimaryWeapon; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	AWeaponBase* GetSecondaryWeapon() const { return SecondaryWeapon; }

	/** Which slot the currently held weapon occupies (None when no weapon is held or it matches
	 *  neither slot pointer — e.g. after both slots were destroyed). */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	EWeaponSlot GetActiveWeaponSlot() const;

	/** The weapon in the slot the player is NOT currently holding. Null when there is no held
	 *  weapon or the other slot is empty. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	AWeaponBase* GetStowedWeapon() const;

	/** First carried weapon whose ammo category matches — held weapon first, then the stowed
	 *  slot. Null when neither carried weapon uses this category. */
	AWeaponBase* FindWeaponByAmmoCategory(EEnemyWeaponAnimType Category) const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsAiming() const { return bIsAiming; }

	// ---- Audio suppression (checkpoint restore) ----

	/** Suppresses pickup and weapon-switch foley while set. The BP restore chain brackets its
	 *  ReplaceSlotWeapon / SetActiveWeapon calls between Begin/End so the level restart sounds
	 *  like a normal spawn, not a weapon swap. Refcounted so nested callers are safe. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Audio")
	void BeginAudioSuppression();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Audio")
	void EndAudioSuppression();

	/** True while any audio suppression scope is active. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Audio")
	bool IsAudioSuppressed() const { return AudioSuppressionDepth > 0; }

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

	/** Kit throwable slot held (single-player state, not replicated). See SetThrowableEquipped. */
	bool bThrowableEquipped = false;

	/** Refcount for audio suppression scopes — see BeginAudioSuppression / EndAudioSuppression. */
	int32 AudioSuppressionDepth = 0;

	/** Spawn + attach + init a slot weapon. Spawns hidden — SetActiveWeapon unhides on activation. */
	AWeaponBase* SpawnWeaponActor(TSubclassOf<AWeaponBase> WeaponClass);

	/** Authority: make NewWeapon the held weapon. Hides/silences the old slot, rebinds the fire
	 *  delegate, syncs aim state, and fires the kit-visual equip flow. Never re-inits ammo. */
	void SetActiveWeapon(AWeaponBase* NewWeapon);

	/** Raises OnActiveWeaponChanged when the effective weapon actually moved. CurrentWeapon is not
	 *  the only thing that decides what is in the player's hands — a nulled slot and the kit
	 *  throwable both change the answer without a SetActiveWeapon call — so every writer routes
	 *  through here and the diff below suppresses the duplicates that produces.
	 *  bForce bypasses that diff entirely — ReplaceSlotWeapon's only caller for it — because a
	 *  successful replace into the STOWED slot changes which AActor is sitting there without
	 *  changing the effective (held) weapon at all, so the normal diff would never see anything to
	 *  report. The broadcast still carries the unchanged held weapon; it exists purely as the
	 *  signal that something about the loadout moved, which is what tells the HUD bridge to
	 *  re-resolve and re-subscribe to whatever is in the stowed slot now. */
	void BroadcastActiveWeaponChanged(bool bForce = false);

	/** Effective weapon as of the last OnActiveWeaponChanged broadcast. Weak: a destroyed weapon
	 *  reads back as null, which is exactly the comparison the next broadcast wants. */
	TWeakObjectPtr<AWeaponBase> LastBroadcastActiveWeapon;

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
