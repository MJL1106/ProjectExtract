// Stable attachment identity, compatibility, and presentation overrides.

#include "Data/PlayerWeaponAttachmentDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool UPlayerWeaponAttachmentDefinition::IsCompatibleWith(EWeaponType WeaponType) const
{
	return !AttachmentId.IsNone() && CompatibleWeaponTypes.Contains(WeaponType);
}

void UPlayerWeaponAttachmentDefinition::ValidateConfiguration(
	TArray<FString>& OutErrors, bool bResolveSoftReferences) const
{
	if (AttachmentId.IsNone())
		OutErrors.Add(TEXT("Attachment ID must not be None."));
	if (ViewClass.IsNull())
		OutErrors.Add(TEXT("Attachment view class must use APlayerWeaponAttachmentView."));
	else if (bResolveSoftReferences && !ViewClass.LoadSynchronous())
		OutErrors.Add(TEXT("Attachment view class could not be loaded."));
	if (CompatibleWeaponTypes.Num() == 0)
		OutErrors.Add(TEXT("Attachment must declare at least one compatible weapon type."));
	if (CompatibleWeaponTypes.Contains(EWeaponType::Unarmed))
		OutErrors.Add(TEXT("Attachments cannot declare Unarmed as a compatible weapon type."));

	if (Slot == EPlayerWeaponAttachmentSlot::Optic)
	{
		if (GripOverride.bOverrideSupportHandStyle)
			OutErrors.Add(TEXT("Optic attachments cannot override the support-hand style."));
		if (OpticOverride.bOverrideFieldOfView
			&& (!FMath::IsFinite(OpticOverride.FieldOfView)
				|| OpticOverride.FieldOfView < 20.f
				|| OpticOverride.FieldOfView > 120.f))
			OutErrors.Add(TEXT("Optic field-of-view override must be between 20 and 120."));
		if (OpticOverride.bOverrideTransitionTime
			&& (!FMath::IsFinite(OpticOverride.TransitionTime) || OpticOverride.TransitionTime <= 0.f))
			OutErrors.Add(TEXT("Optic transition-time override must be greater than zero."));
		if (OpticOverride.bOverrideSensitivity
			&& (!FMath::IsFinite(OpticOverride.SensitivityMultiplier) || OpticOverride.SensitivityMultiplier <= 0.f))
			OutErrors.Add(TEXT("Optic sensitivity override must be greater than zero."));
		if (OpticOverride.bOverrideAimDistance
			&& (!FMath::IsFinite(OpticOverride.AimDistanceFromCameraCm) || OpticOverride.AimDistanceFromCameraCm < 0.f))
			OutErrors.Add(TEXT("Optic aim distance override must not be negative."));
		if (OpticOverride.bOverrideEyeRelief
			&& (!FMath::IsFinite(OpticOverride.EyeReliefCm) || OpticOverride.EyeReliefCm < 0.f))
			OutErrors.Add(TEXT("Optic eye-relief override must not be negative."));
		return;
	}

	if (!GripOverride.bOverrideSupportHandStyle)
		OutErrors.Add(TEXT("Underbarrel grips must select a support-hand style."));
	if (OpticOverride.HasAnyOverride())
		OutErrors.Add(TEXT("Underbarrel grips cannot carry optic presentation overrides."));
}

#if WITH_EDITOR
EDataValidationResult UPlayerWeaponAttachmentDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	const EDataValidationResult SuperResult = Super::IsDataValid(Context);
	TArray<FString> Errors;
	ValidateConfiguration(Errors, true);
	for (const FString& Error : Errors)
		Context.AddError(FText::FromString(Error));

	if (Errors.Num() > 0) return EDataValidationResult::Invalid;
	return SuperResult == EDataValidationResult::Invalid
		? EDataValidationResult::Invalid
		: EDataValidationResult::Valid;
}
#endif
