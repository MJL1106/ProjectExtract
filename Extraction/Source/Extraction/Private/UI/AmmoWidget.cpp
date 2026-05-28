// Copyright Epic Games, Inc. All Rights Reserved.

#include "AmmoWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "WeaponComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "Character/ExtractionPlayerInterface.h"

void UAmmoWidget::NativeConstruct()
{
	Super::NativeConstruct();

	bIsReloading = false;
	ReloadStartTime = 0.f;
	ReloadDuration = 0.f;

	HideReloadIndicator();

	if (!BindToWeaponComponent())
	{
		// Pawn not possessed yet — retry when it arrives
		if (APlayerController* PC = GetOwningPlayer())
			PC->OnPossessedPawnChanged.AddDynamic(this, &UAmmoWidget::OnPawnChanged);
	}
}

void UAmmoWidget::NativeDestruct()
{
	if (APlayerController* PC = GetOwningPlayer())
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UAmmoWidget::OnPawnChanged);

	if (CachedWeapon.IsValid())
		UnbindFromWeapon(CachedWeapon.Get());

	Super::NativeDestruct();
}

void UAmmoWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!CachedWeaponComponent.IsValid()) return;

	AWeaponBase* CurrentWeapon = CachedWeaponComponent->GetCurrentWeapon();

	// Weapon swap detection
	if (CurrentWeapon != CachedWeapon.Get())
	{
		if (CachedWeapon.IsValid())
			UnbindFromWeapon(CachedWeapon.Get());

		if (IsValid(CurrentWeapon))
		{
			BindToWeapon(CurrentWeapon);
			UpdateAmmoDisplay(CurrentWeapon->GetCurrentAmmo(), CurrentWeapon->GetReserveAmmo());
		}
	}

	if (!IsValid(CurrentWeapon)) return;

	// Reload start detection (state transition polling)
	const bool bWeaponIsReloading = CurrentWeapon->IsReloading();
	if (bWeaponIsReloading && !bIsReloading)
	{
		ShowReloadIndicator();
		const UWeaponDataAsset* Data = CurrentWeapon->GetWeaponData();
		if (IsValid(Data))
		{
			ReloadDuration = Data->ReloadTime;
			ReloadStartTime = GetWorld()->GetTimeSeconds();
		}
	}

	// Update progress bar while reloading
	if (bIsReloading && IsValid(ReloadProgressBar))
	{
		const float Elapsed = GetWorld()->GetTimeSeconds() - ReloadStartTime;
		const float Progress = (ReloadDuration > 0.f) ? FMath::Clamp(Elapsed / ReloadDuration, 0.f, 1.f) : 1.f;
		ReloadProgressBar->SetPercent(Progress);
	}
}

// ---- Binding ----

bool UAmmoWidget::BindToWeaponComponent()
{
	const APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return false;

	const IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(PC->GetPawn());
	if (!PlayerIface) return false;

	UWeaponComponent* WC = PlayerIface->GetWeaponComponent();
	if (!IsValid(WC)) return false;

	CachedWeaponComponent = WC;

	AWeaponBase* CurrentWeapon = WC->GetCurrentWeapon();
	if (IsValid(CurrentWeapon))
	{
		BindToWeapon(CurrentWeapon);
		UpdateAmmoDisplay(CurrentWeapon->GetCurrentAmmo(), CurrentWeapon->GetReserveAmmo());
	}

	return true;
}

void UAmmoWidget::BindToWeapon(AWeaponBase* Weapon)
{
	if (!IsValid(Weapon)) return;

	CachedWeapon = Weapon;
	Weapon->OnAmmoChanged.AddDynamic(this, &UAmmoWidget::OnAmmoUpdated);
	Weapon->OnReloadComplete.AddDynamic(this, &UAmmoWidget::OnReloadFinished);

	// If weapon is already mid-reload when we bind
	if (Weapon->IsReloading())
	{
		ShowReloadIndicator();
		const UWeaponDataAsset* Data = Weapon->GetWeaponData();
		if (IsValid(Data))
		{
			ReloadDuration = Data->ReloadTime;
			ReloadStartTime = GetWorld()->GetTimeSeconds();
		}
	}
}

void UAmmoWidget::UnbindFromWeapon(AWeaponBase* Weapon)
{
	if (!IsValid(Weapon)) return;

	Weapon->OnAmmoChanged.RemoveDynamic(this, &UAmmoWidget::OnAmmoUpdated);
	Weapon->OnReloadComplete.RemoveDynamic(this, &UAmmoWidget::OnReloadFinished);
	CachedWeapon.Reset();
}

void UAmmoWidget::OnPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	if (BindToWeaponComponent())
	{
		if (APlayerController* PC = GetOwningPlayer())
			PC->OnPossessedPawnChanged.RemoveDynamic(this, &UAmmoWidget::OnPawnChanged);
	}
}

// ---- Delegate Callbacks ----

void UAmmoWidget::OnAmmoUpdated(int32 CurrentAmmo, int32 ReserveAmmo)
{
	UpdateAmmoDisplay(CurrentAmmo, ReserveAmmo);
}

void UAmmoWidget::OnReloadFinished()
{
	HideReloadIndicator();
}

// ---- UI Helpers ----

void UAmmoWidget::UpdateAmmoDisplay(int32 CurrentAmmo, int32 ReserveAmmo)
{
	if (!IsValid(AmmoText)) return;
	AmmoText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), CurrentAmmo, ReserveAmmo)));
}

void UAmmoWidget::ShowReloadIndicator()
{
	bIsReloading = true;

	if (IsValid(ReloadText))
		ReloadText->SetVisibility(ESlateVisibility::HitTestInvisible);

	if (IsValid(ReloadProgressBar))
	{
		ReloadProgressBar->SetVisibility(ESlateVisibility::HitTestInvisible);
		ReloadProgressBar->SetPercent(0.f);
	}
}

void UAmmoWidget::HideReloadIndicator()
{
	bIsReloading = false;

	if (IsValid(ReloadText))
		ReloadText->SetVisibility(ESlateVisibility::Collapsed);

	if (IsValid(ReloadProgressBar))
		ReloadProgressBar->SetVisibility(ESlateVisibility::Collapsed);
}
