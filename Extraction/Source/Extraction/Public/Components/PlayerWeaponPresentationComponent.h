// Player-owned bridge between authoritative weapon state and first-person presentation.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/ExtractionTypes.h"
#include "PlayerWeaponPresentationComponent.generated.h"

class AExtractionPlayer;
class AWeaponBase;
class UWeaponComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresentedWeaponChangedNative, AWeaponBase*);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresentedBooleanStateChangedNative, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresentationActiveChangedNative, bool);
DECLARE_MULTICAST_DELEGATE(FOnPresentedShotNative);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresentedAmmoChangedNative, int32, int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresentedReloadPhaseChangedNative, EWeaponReloadPhase);

/**
 * Event-driven owner of the player's weapon presentation state.
 * Gameplay authority remains in UWeaponComponent and AWeaponBase.
 */
UCLASS(ClassGroup=(Weapon))
class EXTRACTION_API UPlayerWeaponPresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UPlayerWeaponPresentationComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Re-evaluates local presentation eligibility after possession/controller changes. */
	void RefreshPresentation();

	FOnPresentationActiveChangedNative OnPresentationActiveChangedNative;
	FOnPresentedWeaponChangedNative OnPresentedWeaponChangedNative;
	FOnPresentedBooleanStateChangedNative OnPresentedAimingChangedNative;
	FOnPresentedBooleanStateChangedNative OnPresentedTriggerChangedNative;
	FOnPresentedShotNative OnPresentedShotNative;
	FOnPresentedAmmoChangedNative OnPresentedAmmoChangedNative;
	FOnPresentedReloadPhaseChangedNative OnPresentedReloadPhaseChangedNative;

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon.Get(); }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	bool IsPresentationActive() const { return bPresentationActive; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	bool IsAiming() const { return bIsAiming; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	bool IsTriggerHeld() const { return bTriggerHeld; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	int32 GetCurrentAmmo() const { return CurrentAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	int32 GetReserveAmmo() const { return ReserveAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	EWeaponReloadPhase GetLastReloadPhase() const { return LastReloadPhase; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	bool HasObservedReloadPhase() const { return bHasObservedReloadPhase; }

protected:

	virtual void BeginPlay() override;

private:

	void DiscoverAndBindWeaponComponent();
	void UnbindWeaponComponent();

	void HandleWeaponChanged(AWeaponBase* Weapon);
	void HandleAimingChanged(bool bNewAiming);
	void HandleTriggerChanged(bool bNewTriggerHeld);
	void HandleShot();
	void HandleAmmoChanged(int32 NewCurrentAmmo, int32 NewReserveAmmo);
	void HandleReloadPhaseChanged(EWeaponReloadPhase Phase);

	bool CanRouteLocalPresentation() const;
	void SetPresentationActive(bool bNewActive);
	void PublishPresentationSnapshot();
	void RouteLegacyWeaponChanged(AWeaponBase* Weapon);
	void RouteLegacyAimingChanged(bool bNewAiming);

	UPROPERTY(Transient)
	TObjectPtr<UWeaponComponent> WeaponComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AWeaponBase> CurrentWeapon;

	TWeakObjectPtr<AExtractionPlayer> PlayerOwner;
	TWeakObjectPtr<AWeaponBase> LastRoutedWeapon;

	int32 CurrentAmmo = 0;
	int32 ReserveAmmo = 0;
	EWeaponReloadPhase LastReloadPhase = EWeaponReloadPhase::Completed;

	bool bIsAiming = false;
	bool bTriggerHeld = false;
	bool bHasObservedReloadPhase = false;
	bool bBoundToWeaponComponent = false;
	bool bPresentationActive = false;
	bool bHasRoutedAiming = false;
	bool bLastRoutedAiming = false;
};
