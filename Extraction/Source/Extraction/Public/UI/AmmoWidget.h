// HUD widget — ammo counter and reload progress indicator.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AmmoWidget.generated.h"

class UTextBlock;
class UProgressBar;
class UWeaponComponent;
class AWeaponBase;

/**
 * Displays current/reserve ammo and a reload progress bar.
 * Requires a Widget Blueprint with:
 *   - TextBlock named "AmmoText"
 *   - TextBlock named "ReloadText"
 *   - ProgressBar named "ReloadProgressBar"
 */
UCLASS()
class EXTRACTION_API UAmmoWidget : public UUserWidget
{
	GENERATED_BODY()

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> AmmoText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ReloadText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> ReloadProgressBar;

private:

	bool BindToWeaponComponent();
	void BindToWeapon(AWeaponBase* Weapon);
	void UnbindFromWeapon(AWeaponBase* Weapon);

	UFUNCTION()
	void OnPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void OnAmmoUpdated(int32 CurrentAmmo, int32 ReserveAmmo);

	UFUNCTION()
	void OnReloadFinished();

	void UpdateAmmoDisplay(int32 CurrentAmmo, int32 ReserveAmmo);
	void ShowReloadIndicator();
	void HideReloadIndicator();

	TWeakObjectPtr<UWeaponComponent> CachedWeaponComponent;
	TWeakObjectPtr<AWeaponBase> CachedWeapon;

	bool bIsReloading;
	float ReloadStartTime;
	float ReloadDuration;
};
