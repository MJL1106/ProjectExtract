// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeaponDataAsset.h"

#include "Core/Extraction.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

void UWeaponDataAsset::PostLoad()
{
	Super::PostLoad();

	if (PelletCount > 1 && PelletSpreadDeg <= 0.f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UWeaponDataAsset '%s': PelletCount=%d but PelletSpreadDeg=0 — all pellets stack on one point (N× damage). Set PelletSpreadDeg > 0 for a real spread pattern."),
			*GetName(), PelletCount);
	}

	if (IsValid(PlayerPresentationProfile))
	{
		TArray<FString> Errors;
		ValidatePlayerPresentation(Errors);
		for (const FString& Error : Errors)
		{
			UE_LOG(LogExtraction, Error,
				TEXT("UWeaponDataAsset '%s' player presentation: %s"),
				*GetName(), *Error);
		}
	}
}

EPlayerWeaponPresentationMigrationState
UWeaponDataAsset::GetPlayerPresentationMigrationState() const
{
	if (IsValid(PlayerPresentationProfile))
		return EPlayerWeaponPresentationMigrationState::Profile;
	if (IsValid(KitWeaponPoseAsset) || KitVisualWeaponClass)
		return EPlayerWeaponPresentationMigrationState::LegacyKitBridge;
	return EPlayerWeaponPresentationMigrationState::Unconfigured;
}

namespace
{
	void RequireReloadVariant(
		const UPlayerWeaponPresentationProfile& Profile,
		EPlayerWeaponReloadVariant Variant,
		const TCHAR* Error,
		TArray<FString>& OutErrors)
	{
		if (!Profile.FindReloadAction(Variant))
			OutErrors.Add(Error);
	}
}

void UWeaponDataAsset::ValidatePlayerPresentation(
	TArray<FString>& OutErrors, bool bResolveSoftReferences) const
{
	if (!IsValid(PlayerPresentationProfile)) return;

	PlayerPresentationProfile->ValidateConfiguration(
		OutErrors, bResolveSoftReferences);
	if (PlayerPresentationProfile->WeaponType != WeaponType)
		OutErrors.Add(TEXT("Presentation profile weapon type must match gameplay weapon type."));

	if (bShellByShellReload)
	{
		RequireReloadVariant(*PlayerPresentationProfile,
			EPlayerWeaponReloadVariant::ShellStart,
			TEXT("Shell-by-shell reload requires the ShellStart presentation variant."),
			OutErrors);
		RequireReloadVariant(*PlayerPresentationProfile,
			EPlayerWeaponReloadVariant::ShellInsert,
			TEXT("Shell-by-shell reload requires the ShellInsert presentation variant."),
			OutErrors);
		RequireReloadVariant(*PlayerPresentationProfile,
			EPlayerWeaponReloadVariant::ShellEnd,
			TEXT("Shell-by-shell reload requires the ShellEnd presentation variant."),
			OutErrors);
	}
	else
	{
		RequireReloadVariant(*PlayerPresentationProfile,
			EPlayerWeaponReloadVariant::Tactical,
			TEXT("Magazine reload requires the Tactical presentation variant."),
			OutErrors);
		RequireReloadVariant(*PlayerPresentationProfile,
			EPlayerWeaponReloadVariant::Empty,
			TEXT("Magazine reload requires the Empty presentation variant."),
			OutErrors);
	}

	RequireReloadVariant(*PlayerPresentationProfile,
		EPlayerWeaponReloadVariant::Interrupted,
		TEXT("Player presentation requires the Interrupted reload variant."),
		OutErrors);
}

#if WITH_EDITOR
EDataValidationResult UWeaponDataAsset::IsDataValid(
	FDataValidationContext& Context) const
{
	const EDataValidationResult SuperResult = Super::IsDataValid(Context);
	TArray<FString> Errors;
	ValidatePlayerPresentation(Errors, true);
	for (const FString& Error : Errors)
		Context.AddError(FText::FromString(Error));

	if (Errors.Num() > 0) return EDataValidationResult::Invalid;
	return SuperResult == EDataValidationResult::Invalid
		? EDataValidationResult::Invalid
		: EDataValidationResult::Valid;
}
#endif
