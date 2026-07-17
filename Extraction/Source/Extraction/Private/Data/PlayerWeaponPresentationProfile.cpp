// Focused player presentation data, independent from weapon gameplay tuning.

#include "Data/PlayerWeaponPresentationProfile.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	template <typename TSoftReference>
	bool SoftReferenceFailsToLoad(const TSoftReference& Reference)
	{
		return !Reference.IsNull() && Reference.LoadSynchronous() == nullptr;
	}
}

void UPlayerWeaponProceduralPoseDefinition::ValidateConfiguration(
	TArray<FString>& OutErrors, bool bResolveSoftReferences) const
{
	if (KitPoseAsset.IsNull())
		OutErrors.Add(TEXT("Procedural pose definition must assign a kit pose asset."));
	else if (bResolveSoftReferences && SoftReferenceFailsToLoad(KitPoseAsset))
		OutErrors.Add(TEXT("Procedural kit pose asset could not be loaded."));
}

#if WITH_EDITOR
EDataValidationResult UPlayerWeaponProceduralPoseDefinition::IsDataValid(
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

const FPlayerWeaponSupportHandProfile*
UPlayerWeaponPresentationProfile::FindSupportHandProfile(
	EPlayerWeaponSupportHandStyle Style) const
{
	return SupportHandProfiles.FindByPredicate(
		[Style](const FPlayerWeaponSupportHandProfile& Profile)
		{
			return Profile.Style == Style;
		});
}

const FPlayerWeaponReloadAction* UPlayerWeaponPresentationProfile::FindReloadAction(
	EPlayerWeaponReloadVariant Variant) const
{
	return ReloadActions.FindByPredicate(
		[Variant](const FPlayerWeaponReloadAction& Action)
		{
			return Action.Variant == Variant;
		});
}

UPlayerWeaponAttachmentDefinition*
UPlayerWeaponPresentationProfile::FindAttachmentDefinition(FName AttachmentId) const
{
	if (AttachmentId.IsNone()) return nullptr;

	const TObjectPtr<UPlayerWeaponAttachmentDefinition>* Found =
		CompatibleAttachments.FindByPredicate(
			[AttachmentId](const TObjectPtr<UPlayerWeaponAttachmentDefinition>& Definition)
			{
				return IsValid(Definition) && Definition->AttachmentId == AttachmentId;
			});
	return Found ? Found->Get() : nullptr;
}

bool UPlayerWeaponPresentationProfile::IsAttachmentCompatible(
	FName AttachmentId, EPlayerWeaponAttachmentSlot Slot) const
{
	if (AttachmentId.IsNone()) return true;

	const UPlayerWeaponAttachmentDefinition* Definition =
		FindAttachmentDefinition(AttachmentId);
	return IsValid(Definition)
		&& Definition->Slot == Slot
		&& Definition->IsCompatibleWith(WeaponType);
}

namespace
{
	void ValidateProfileCore(
		const UPlayerWeaponPresentationProfile& Profile,
		bool bResolveSoftReferences,
		TArray<FString>& OutErrors)
	{
		if (Profile.ProfileId.IsNone())
			OutErrors.Add(TEXT("Profile ID must not be None."));
		if (Profile.WeaponType == EWeaponType::Unarmed)
			OutErrors.Add(TEXT("A weapon presentation profile cannot use the Unarmed type."));
		if (Profile.ViewClass.IsNull())
			OutErrors.Add(TEXT("Profile view class must use APlayerWeaponView."));
		else if (bResolveSoftReferences
			&& SoftReferenceFailsToLoad(Profile.ViewClass))
			OutErrors.Add(TEXT("Profile view class could not be loaded."));
		if (!IsValid(Profile.ProceduralPose))
			OutErrors.Add(TEXT("Profile must assign a typed Procedural pose definition."));
		else
			Profile.ProceduralPose->ValidateConfiguration(
				OutErrors, bResolveSoftReferences);
		if (Profile.SeatPolicy == EPlayerWeaponSeatPolicy::WeaponSeatMarker
			&& !Profile.MarkerRequirements.bRequireWeaponSeat)
			OutErrors.Add(TEXT("WeaponSeatMarker policy requires the WeaponSeat marker."));
		if (Profile.MarkerRequirements.bRequireIronRear
			!= Profile.MarkerRequirements.bRequireIronFront)
			OutErrors.Add(TEXT("Iron rear and iron front markers must be required as a pair."));

		const FPlayerWeaponHandOffsets& Offsets = Profile.HandOffsets;
		if (Offsets.HandGunSocketOffset.ContainsNaN()
			|| Offsets.RightHandSocketOffset.ContainsNaN()
			|| Offsets.LeftHandSocketOffset.ContainsNaN())
			OutErrors.Add(TEXT("Hand offsets must contain only finite values."));
	}

	void ValidateADSDefaults(
		const FPlayerWeaponADSDefaults& ADS, TArray<FString>& OutErrors)
	{
		if (!FMath::IsFinite(ADS.FieldOfView)
			|| ADS.FieldOfView < 20.f
			|| ADS.FieldOfView > 120.f)
			OutErrors.Add(TEXT("ADS field of view must be between 20 and 120."));
		if (!FMath::IsFinite(ADS.TransitionTime) || ADS.TransitionTime <= 0.f)
			OutErrors.Add(TEXT("ADS transition time must be greater than zero."));
		if (!FMath::IsFinite(ADS.SensitivityMultiplier) || ADS.SensitivityMultiplier <= 0.f)
			OutErrors.Add(TEXT("ADS sensitivity multiplier must be greater than zero."));
		if (!FMath::IsFinite(ADS.AimDistanceFromCameraCm) || ADS.AimDistanceFromCameraCm < 0.f)
			OutErrors.Add(TEXT("ADS aim distance must not be negative."));
		if (!FMath::IsFinite(ADS.EyeReliefCm) || ADS.EyeReliefCm < 0.f)
			OutErrors.Add(TEXT("ADS eye relief must not be negative."));
	}

	void ValidateSupportHands(
		const UPlayerWeaponPresentationProfile& Profile,
		bool bResolveSoftReferences,
		TArray<FString>& OutErrors)
	{
		TSet<EPlayerWeaponSupportHandStyle> SeenStyles;
		SeenStyles.Reserve(Profile.SupportHandProfiles.Num());
		for (const FPlayerWeaponSupportHandProfile& Support : Profile.SupportHandProfiles)
		{
			if (SeenStyles.Contains(Support.Style))
				OutErrors.Add(FString::Printf(
					TEXT("Duplicate support-hand style: %d."),
					static_cast<int32>(Support.Style)));
			SeenStyles.Add(Support.Style);

			if (!FMath::IsFinite(Support.IKWeight)
				|| Support.IKWeight < 0.f
				|| Support.IKWeight > 1.f)
				OutErrors.Add(TEXT("Support-hand IK weight must be between zero and one."));
			if (Support.EffectorOffset.ContainsNaN() || Support.JointTargetOffset.ContainsNaN())
				OutErrors.Add(TEXT("Support-hand offsets must contain only finite values."));
			if (Support.Style == EPlayerWeaponSupportHandStyle::None
				&& !FMath::IsNearlyZero(Support.IKWeight))
				OutErrors.Add(TEXT("No-support-hand style must use zero IK weight."));
			if (Support.Style != EPlayerWeaponSupportHandStyle::None
				&& (Support.HandPose.IsNull() || Support.IKWeight <= 0.f))
				OutErrors.Add(TEXT("Active support-hand styles require a hand pose and positive IK weight."));
			if (Support.Style != EPlayerWeaponSupportHandStyle::None
				&& bResolveSoftReferences
				&& SoftReferenceFailsToLoad(Support.HandPose))
				OutErrors.Add(TEXT("Support-hand pose could not be loaded."));
		}
		if (!Profile.FindSupportHandProfile(Profile.DefaultSupportHandStyle))
			OutErrors.Add(TEXT("Default support-hand style must resolve to one profile entry."));
		if (Profile.DefaultSupportHandStyle != EPlayerWeaponSupportHandStyle::None
			&& !Profile.MarkerRequirements.bRequireSupportHand)
			OutErrors.Add(TEXT(
				"An active default hand style requires the support-hand marker."));
	}
}

namespace
{
	void ValidateManualCycleAction(
		const UPlayerWeaponPresentationProfile& Profile,
		TArray<FString>& OutErrors)
	{
		const EPlayerWeaponManualCycleAction Policy = Profile.ManualCycleAction;
		if (Policy == EPlayerWeaponManualCycleAction::Bolt
			&& !Profile.FindReloadAction(EPlayerWeaponReloadVariant::BoltCycle))
			OutErrors.Add(TEXT("Bolt-cycle policy requires the BoltCycle presentation variant."));
		if (Policy == EPlayerWeaponManualCycleAction::Pump
			&& !Profile.FindReloadAction(EPlayerWeaponReloadVariant::PumpCycle))
			OutErrors.Add(TEXT("Pump-cycle policy requires the PumpCycle presentation variant."));

		const bool bHasBoltMapping = Profile.MovingPartMappings.ContainsByPredicate(
			[](const FPlayerWeaponMovingPartMapping& Mapping)
			{
				return Mapping.Part == EPlayerWeaponMovingPart::Bolt
					|| Mapping.Part == EPlayerWeaponMovingPart::ChargingHandle;
			});
		if (Policy == EPlayerWeaponManualCycleAction::Bolt && !bHasBoltMapping)
			OutErrors.Add(TEXT(
				"Bolt-cycle policy requires a bolt moving-part mapping."));

		const bool bHasPumpMapping = Profile.MovingPartMappings.ContainsByPredicate(
			[](const FPlayerWeaponMovingPartMapping& Mapping)
			{
				return Mapping.Part == EPlayerWeaponMovingPart::Pump;
			});
		if (Policy == EPlayerWeaponManualCycleAction::Pump && !bHasPumpMapping)
			OutErrors.Add(TEXT(
				"Pump-cycle policy requires a pump moving-part mapping."));
	}

	void ValidateReloadActions(
		const UPlayerWeaponPresentationProfile& Profile,
		bool bResolveSoftReferences,
		TArray<FString>& OutErrors)
	{
		TSet<EPlayerWeaponReloadVariant> SeenVariants;
		SeenVariants.Reserve(Profile.ReloadActions.Num());
		for (const FPlayerWeaponReloadAction& Action : Profile.ReloadActions)
		{
			if (SeenVariants.Contains(Action.Variant))
				OutErrors.Add(FString::Printf(
					TEXT("Duplicate reload variant: %d."),
					static_cast<int32>(Action.Variant)));
			SeenVariants.Add(Action.Variant);
			if (!FMath::IsFinite(Action.PlayRate) || Action.PlayRate <= 0.f)
				OutErrors.Add(TEXT("Reload action play rate must be greater than zero."));
			if (!Action.bBlendOutOnly
				&& Action.ArmsAction.IsNull()
				&& Action.WeaponAction.IsNull())
				OutErrors.Add(TEXT("Reload action requires an arms or weapon animation."));
			if (Action.bBlendOutOnly
				&& Action.Variant != EPlayerWeaponReloadVariant::Interrupted)
				OutErrors.Add(TEXT("Only the Interrupted reload variant may be blend-out-only."));
			if (bResolveSoftReferences
				&& SoftReferenceFailsToLoad(Action.ArmsAction))
				OutErrors.Add(TEXT("Reload arms animation could not be loaded."));
			if (bResolveSoftReferences
				&& SoftReferenceFailsToLoad(Action.WeaponAction))
				OutErrors.Add(TEXT("Reload weapon animation could not be loaded."));
		}

		ValidateManualCycleAction(Profile, OutErrors);
	}

	void ValidateMovingParts(
		const UPlayerWeaponPresentationProfile& Profile, TArray<FString>& OutErrors)
	{
		TSet<EPlayerWeaponMovingPart> SeenParts;
		SeenParts.Reserve(Profile.MovingPartMappings.Num());
		for (const FPlayerWeaponMovingPartMapping& Mapping : Profile.MovingPartMappings)
		{
			if (SeenParts.Contains(Mapping.Part))
				OutErrors.Add(FString::Printf(
					TEXT("Duplicate moving part: %d."),
					static_cast<int32>(Mapping.Part)));
			SeenParts.Add(Mapping.Part);
			if (Mapping.TargetName.IsNone())
				OutErrors.Add(TEXT("Moving-part mappings require a target name."));
		}
	}

	void ValidateAttachments(
		const UPlayerWeaponPresentationProfile& Profile,
		bool bResolveSoftReferences,
		TArray<FString>& OutErrors)
	{
		TSet<FName> SeenIds;
		SeenIds.Reserve(Profile.CompatibleAttachments.Num());
		for (const UPlayerWeaponAttachmentDefinition* Definition : Profile.CompatibleAttachments)
		{
			if (!IsValid(Definition))
			{
				OutErrors.Add(TEXT("Compatible attachment entries must not be null."));
				continue;
			}
			if (SeenIds.Contains(Definition->AttachmentId))
				OutErrors.Add(FString::Printf(
					TEXT("Duplicate attachment ID: %s."),
					*Definition->AttachmentId.ToString()));
			SeenIds.Add(Definition->AttachmentId);

			TArray<FString> DefinitionErrors;
			Definition->ValidateConfiguration(
				DefinitionErrors, bResolveSoftReferences);
			for (const FString& Error : DefinitionErrors)
				OutErrors.Add(FString::Printf(
					TEXT("Attachment %s: %s"),
					*Definition->AttachmentId.ToString(), *Error));
			if (!Definition->IsCompatibleWith(Profile.WeaponType))
				OutErrors.Add(FString::Printf(
					TEXT("Attachment %s is not compatible with the profile weapon type."),
					*Definition->AttachmentId.ToString()));
			if (Definition->Slot == EPlayerWeaponAttachmentSlot::UnderbarrelGrip
				&& Definition->GripOverride.bOverrideSupportHandStyle
				&& !Profile.FindSupportHandProfile(
					Definition->GripOverride.SupportHandStyle))
				OutErrors.Add(FString::Printf(
					TEXT("Grip %s selects an undefined support-hand profile."),
					*Definition->AttachmentId.ToString()));
		}
	}

	void ValidateADSSolution(
		const UPlayerWeaponPresentationProfile& Profile,
		TArray<FString>& OutErrors)
	{
		const bool bHasIrons =
			Profile.MarkerRequirements.bRequireIronRear
			&& Profile.MarkerRequirements.bRequireIronFront;
		const bool bHasDefaultOptic =
			!Profile.DefaultOpticId.IsNone()
			&& Profile.IsAttachmentCompatible(
				Profile.DefaultOpticId, EPlayerWeaponAttachmentSlot::Optic);
		if (!bHasIrons && !bHasDefaultOptic)
			OutErrors.Add(TEXT(
				"Profile requires an ADS solution: paired irons or a compatible default optic."));

		const bool bSupportsOptics = Profile.CompatibleAttachments.ContainsByPredicate(
			[](const TObjectPtr<UPlayerWeaponAttachmentDefinition>& Definition)
			{
				return IsValid(Definition)
					&& Definition->Slot == EPlayerWeaponAttachmentSlot::Optic;
			});
		if (bSupportsOptics && !Profile.MarkerRequirements.bRequireOpticMount)
			OutErrors.Add(TEXT("Profiles supporting optics must require an optic mount."));
	}
}

void UPlayerWeaponPresentationProfile::ValidateConfiguration(
	TArray<FString>& OutErrors, bool bResolveSoftReferences) const
{
	ValidateProfileCore(*this, bResolveSoftReferences, OutErrors);
	ValidateADSDefaults(ADSDefaults, OutErrors);
	ValidateSupportHands(*this, bResolveSoftReferences, OutErrors);
	ValidateReloadActions(*this, bResolveSoftReferences, OutErrors);
	ValidateMovingParts(*this, OutErrors);
	ValidateAttachments(*this, bResolveSoftReferences, OutErrors);
	ValidateADSSolution(*this, OutErrors);

	if (!DefaultOpticId.IsNone()
		&& !IsAttachmentCompatible(DefaultOpticId, EPlayerWeaponAttachmentSlot::Optic))
		OutErrors.Add(TEXT("Default optic ID must resolve to a compatible optic."));
	if (!DefaultGripId.IsNone()
		&& !IsAttachmentCompatible(DefaultGripId, EPlayerWeaponAttachmentSlot::UnderbarrelGrip))
		OutErrors.Add(TEXT("Default grip ID must resolve to a compatible underbarrel grip."));
}

#if WITH_EDITOR
EDataValidationResult UPlayerWeaponPresentationProfile::IsDataValid(
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
