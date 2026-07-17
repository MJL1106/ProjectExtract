// Automation coverage for player weapon presentation data contracts.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Data/PlayerWeaponAttachmentDefinition.h"
#include "Data/PlayerWeaponPresentationProfile.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/DataAsset.h"
#include "Misc/DataValidation.h"
#include "Weapon/PlayerWeaponView.h"

namespace PlayerWeaponPresentationProfileTest
{
	bool HasErrorContaining(const TArray<FString>& Errors, const FString& Fragment)
	{
		return Errors.ContainsByPredicate(
			[&Fragment](const FString& Error) { return Error.Contains(Fragment); });
	}

	UPlayerWeaponAttachmentDefinition* MakeOptic(FName AttachmentId)
	{
		UPlayerWeaponAttachmentDefinition* Optic =
			NewObject<UPlayerWeaponAttachmentDefinition>(GetTransientPackage());
		Optic->AttachmentId = AttachmentId;
		Optic->Slot = EPlayerWeaponAttachmentSlot::Optic;
		Optic->ViewClass = APlayerWeaponAttachmentView::StaticClass();
		Optic->CompatibleWeaponTypes.Add(EWeaponType::Rifle);
		return Optic;
	}

	UPlayerWeaponAttachmentDefinition* MakeGrip(
		FName AttachmentId, EPlayerWeaponSupportHandStyle Style)
	{
		UPlayerWeaponAttachmentDefinition* Grip =
			NewObject<UPlayerWeaponAttachmentDefinition>(GetTransientPackage());
		Grip->AttachmentId = AttachmentId;
		Grip->Slot = EPlayerWeaponAttachmentSlot::UnderbarrelGrip;
		Grip->ViewClass = APlayerWeaponAttachmentView::StaticClass();
		Grip->CompatibleWeaponTypes.Add(EWeaponType::Rifle);
		Grip->GripOverride.bOverrideSupportHandStyle = true;
		Grip->GripOverride.SupportHandStyle = Style;
		return Grip;
	}

	FPlayerWeaponReloadAction MakeReloadAction(EPlayerWeaponReloadVariant Variant)
	{
		FPlayerWeaponReloadAction Action;
		Action.Variant = Variant;
		if (Variant == EPlayerWeaponReloadVariant::Interrupted)
			Action.bBlendOutOnly = true;
		else
			Action.ArmsAction = NewObject<UAnimSequence>(GetTransientPackage());
		return Action;
	}

	UPlayerWeaponPresentationProfile* MakeValidRifleProfile()
	{
		UPlayerWeaponPresentationProfile* Profile =
			NewObject<UPlayerWeaponPresentationProfile>(GetTransientPackage());
		Profile->ProfileId = TEXT("weapon.rifle.test");
		Profile->WeaponType = EWeaponType::Rifle;
		Profile->ViewClass = APlayerWeaponView::StaticClass();
		Profile->ProceduralPose =
			NewObject<UPlayerWeaponProceduralPoseDefinition>(GetTransientPackage());
		Profile->ProceduralPose->KitPoseAsset =
			NewObject<UWeaponDataAsset>(GetTransientPackage());
		Profile->SeatPolicy = EPlayerWeaponSeatPolicy::WeaponSeatMarker;
		Profile->MarkerRequirements.bRequireWeaponSeat = true;
		Profile->MarkerRequirements.bRequireMuzzle = true;
		Profile->MarkerRequirements.bRequireOpticMount = true;
		Profile->MarkerRequirements.bRequireSupportHand = true;

		FPlayerWeaponSupportHandProfile SupportHand;
		SupportHand.Style = EPlayerWeaponSupportHandStyle::Standard;
		SupportHand.HandPose = NewObject<UAnimSequence>(GetTransientPackage());
		SupportHand.IKWeight = 1.f;
		Profile->SupportHandProfiles.Add(SupportHand);
		Profile->DefaultSupportHandStyle = EPlayerWeaponSupportHandStyle::Standard;

		Profile->ReloadActions.Add(MakeReloadAction(EPlayerWeaponReloadVariant::Tactical));
		Profile->ReloadActions.Add(MakeReloadAction(EPlayerWeaponReloadVariant::Empty));
		Profile->ReloadActions.Add(MakeReloadAction(EPlayerWeaponReloadVariant::Interrupted));

		FPlayerWeaponMovingPartMapping Magazine;
		Magazine.Part = EPlayerWeaponMovingPart::Magazine;
		Magazine.TargetKind = EPlayerWeaponMovingPartTarget::SceneComponent;
		Magazine.TargetName = TEXT("Magazine");
		Profile->MovingPartMappings.Add(Magazine);

		UPlayerWeaponAttachmentDefinition* Optic = MakeOptic(TEXT("optic.test"));
		Profile->CompatibleAttachments.Add(Optic);
		Profile->DefaultOpticId = Optic->AttachmentId;
		return Profile;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponPresentationLegacyDefaultsTest,
	"Extraction.PlayerWeapon.Profile.LegacyDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponPresentationLegacyDefaultsTest::RunTest(const FString& Parameters)
{
	UWeaponDataAsset* WeaponData = NewObject<UWeaponDataAsset>(GetTransientPackage());
	TestEqual(TEXT("existing weapon assets retain rifle default"), WeaponData->WeaponType, EWeaponType::Rifle);
	TestNull(TEXT("existing weapon assets do not gain an implicit profile"), WeaponData->GetPlayerPresentationProfile());
	TestEqual(
		TEXT("unconfigured assets preserve their existing non-player behavior"),
		WeaponData->GetPlayerPresentationMigrationState(),
		EPlayerWeaponPresentationMigrationState::Unconfigured);

	TArray<FString> Errors;
	WeaponData->ValidatePlayerPresentation(Errors);
	TestEqual(TEXT("unconfigured legacy-safe data stays valid"), Errors.Num(), 0);

	WeaponData->KitVisualWeaponClass = AActor::StaticClass();
	TestEqual(
		TEXT("existing kit visual fields are recognized as the legacy bridge"),
		WeaponData->GetPlayerPresentationMigrationState(),
		EPlayerWeaponPresentationMigrationState::LegacyKitBridge);

	TestTrue(
		TEXT("SMG is a real gameplay weapon category"),
		StaticEnum<EWeaponType>()->GetIndexByValue(
			static_cast<int64>(EWeaponType::SMG)) != INDEX_NONE);
	TestTrue(
		TEXT("shotgun is a real gameplay weapon category"),
		StaticEnum<EWeaponType>()->GetIndexByValue(
			static_cast<int64>(EWeaponType::Shotgun)) != INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponPresentationProfileResolutionTest,
	"Extraction.PlayerWeapon.Profile.ProfileResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponPresentationProfileResolutionTest::RunTest(const FString& Parameters)
{
	UWeaponDataAsset* WeaponData = NewObject<UWeaponDataAsset>(GetTransientPackage());
	UPlayerWeaponPresentationProfile* Profile =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	WeaponData->PlayerPresentationProfile = Profile;

	const float GameplayFOV = WeaponData->ADSFOV;
	const float GameplayTransition = WeaponData->ADSTransitionTime;
	const float GameplayMoveSpeed = WeaponData->ADSMovementSpeed;
	const float GameplayReloadTime = WeaponData->ReloadTime;
	Profile->ADSDefaults.FieldOfView = 42.f;
	Profile->ADSDefaults.TransitionTime = 0.4f;

	TestEqual(TEXT("weapon resolves its assigned presentation profile"), WeaponData->GetPlayerPresentationProfile(), Profile);
	TestEqual(
		TEXT("assigned profile becomes the explicit migration state"),
		WeaponData->GetPlayerPresentationMigrationState(),
		EPlayerWeaponPresentationMigrationState::Profile);
	TestEqual(TEXT("presentation FOV does not overwrite gameplay data"), WeaponData->ADSFOV, GameplayFOV);
	TestEqual(TEXT("presentation transition does not overwrite gameplay data"), WeaponData->ADSTransitionTime, GameplayTransition);
	TestEqual(TEXT("presentation does not overwrite ADS movement policy"), WeaponData->ADSMovementSpeed, GameplayMoveSpeed);
	TestEqual(TEXT("presentation does not overwrite reload timing"), WeaponData->ReloadTime, GameplayReloadTime);

	TestNotNull(
		TEXT("default support hand resolves"),
		Profile->FindSupportHandProfile(EPlayerWeaponSupportHandStyle::Standard));
	TestNotNull(
		TEXT("tactical reload action resolves"),
		Profile->FindReloadAction(EPlayerWeaponReloadVariant::Tactical));
	TestNotNull(TEXT("stable optic ID resolves"), Profile->FindAttachmentDefinition(TEXT("optic.test")));
	TestTrue(
		TEXT("declared optic is compatible with the profile"),
		Profile->IsAttachmentCompatible(TEXT("optic.test"), EPlayerWeaponAttachmentSlot::Optic));
	TestTrue(
		TEXT("NAME_None remains the compatible no-attachment choice"),
		Profile->IsAttachmentCompatible(NAME_None, EPlayerWeaponAttachmentSlot::Optic));

	TArray<FString> Errors;
	WeaponData->ValidatePlayerPresentation(Errors);
	TestEqual(TEXT("complete magazine profile validates"), Errors.Num(), 0);

	UWeaponDataAsset* MismatchedWeapon =
		NewObject<UWeaponDataAsset>(GetTransientPackage());
	MismatchedWeapon->WeaponType = EWeaponType::Pistol;
	MismatchedWeapon->PlayerPresentationProfile =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	FDataValidationContext WeaponValidationContext;
	const UWeaponDataAsset* WeaponForValidation = MismatchedWeapon;
	TestEqual(
		TEXT("weapon-level editor validation rejects cross-record profile errors"),
		WeaponForValidation->IsDataValid(WeaponValidationContext),
		EDataValidationResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponPresentationProfileValidationTest,
	"Extraction.PlayerWeapon.Profile.ValidationAndAttachments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponPresentationProfileValidationTest::RunTest(const FString& Parameters)
{
	UPlayerWeaponAttachmentDefinition* Optic =
		PlayerWeaponPresentationProfileTest::MakeOptic(TEXT("optic.compatibility"));
	TestTrue(TEXT("explicit rifle compatibility succeeds"), Optic->IsCompatibleWith(EWeaponType::Rifle));
	TestFalse(TEXT("undeclared pistol compatibility fails"), Optic->IsCompatibleWith(EWeaponType::Pistol));

	TArray<FString> AttachmentErrors;
	Optic->CompatibleWeaponTypes.Reset();
	Optic->ValidateConfiguration(AttachmentErrors);
	TestTrue(
		TEXT("attachment compatibility cannot silently wildcard"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(AttachmentErrors, TEXT("compatible weapon type")));

	UPlayerWeaponPresentationProfile* Profile =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	UPlayerWeaponAttachmentDefinition* DuplicateOptic =
		PlayerWeaponPresentationProfileTest::MakeOptic(TEXT("optic.test"));
	Profile->CompatibleAttachments.Add(DuplicateOptic);

	const FPlayerWeaponSupportHandProfile DuplicateSupport =
		Profile->SupportHandProfiles[0];
	const FPlayerWeaponReloadAction DuplicateReload = Profile->ReloadActions[0];
	const FPlayerWeaponMovingPartMapping DuplicateMovingPart =
		Profile->MovingPartMappings[0];
	Profile->SupportHandProfiles.Add(DuplicateSupport);
	Profile->ReloadActions.Add(DuplicateReload);
	Profile->MovingPartMappings.Add(DuplicateMovingPart);
	Profile->ADSDefaults.FieldOfView = 0.f;

	TArray<FString> Errors;
	Profile->ValidateConfiguration(Errors);
	TestTrue(
		TEXT("duplicate stable attachment IDs are rejected"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(Errors, TEXT("Duplicate attachment ID")));
	TestTrue(
		TEXT("duplicate support-hand styles are rejected"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(Errors, TEXT("Duplicate support-hand style")));
	TestTrue(
		TEXT("duplicate reload variants are rejected"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(Errors, TEXT("Duplicate reload variant")));
	TestTrue(
		TEXT("duplicate moving-part mappings are rejected"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(Errors, TEXT("Duplicate moving part")));
	TestTrue(
		TEXT("invalid ADS values are rejected"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(Errors, TEXT("ADS field of view")));

	UPlayerWeaponPresentationProfile* MissingADSSolution =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	MissingADSSolution->DefaultOpticId = NAME_None;
	TArray<FString> MissingADSErrors;
	MissingADSSolution->ValidateConfiguration(MissingADSErrors);
	TestTrue(
		TEXT("profiles require an explicit irons or default-optic ADS solution"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(
			MissingADSErrors, TEXT("ADS solution")));

	UPlayerWeaponPresentationProfile* MissingOpticMount =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	MissingOpticMount->MarkerRequirements.bRequireOpticMount = false;
	TArray<FString> MissingOpticMountErrors;
	MissingOpticMount->ValidateConfiguration(MissingOpticMountErrors);
	TestTrue(
		TEXT("profiles supporting optics require an optic mount"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(
			MissingOpticMountErrors, TEXT("optic mount")));

	UPlayerWeaponPresentationProfile* MissingSupportMarker =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	MissingSupportMarker->MarkerRequirements.bRequireSupportHand = false;
	TArray<FString> MissingSupportMarkerErrors;
	MissingSupportMarker->ValidateConfiguration(MissingSupportMarkerErrors);
	TestTrue(
		TEXT("active default hand styles require support-hand geometry"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(
			MissingSupportMarkerErrors, TEXT("support-hand marker")));

	UPlayerWeaponPresentationProfile* MissingGripProfile =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	MissingGripProfile->CompatibleAttachments.Add(
		PlayerWeaponPresentationProfileTest::MakeGrip(
			TEXT("grip.vertical"), EPlayerWeaponSupportHandStyle::Vertical));
	TArray<FString> MissingGripProfileErrors;
	MissingGripProfile->ValidateConfiguration(MissingGripProfileErrors);
	TestTrue(
		TEXT("grips cannot select an undefined support-hand profile"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(
			MissingGripProfileErrors, TEXT("support-hand profile")));

	UPlayerWeaponPresentationProfile* BrokenSoftReference =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	BrokenSoftReference->ViewClass =
		TSoftClassPtr<APlayerWeaponView>(
			FSoftObjectPath(TEXT("/Game/Tests/MissingWeaponView.MissingWeaponView_C")));
	FDataValidationContext BrokenReferenceContext;
	const UPlayerWeaponPresentationProfile* ProfileForValidation =
		BrokenSoftReference;
	TestEqual(
		TEXT("editor validation rejects broken soft references"),
		ProfileForValidation->IsDataValid(BrokenReferenceContext),
		EDataValidationResult::Invalid);

	UPlayerWeaponPresentationProfile* BrokenPoseReference =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	BrokenPoseReference->ProceduralPose->KitPoseAsset =
		TSoftObjectPtr<UDataAsset>(
			FSoftObjectPath(TEXT("/Game/Tests/MissingPose.MissingPose")));
	FDataValidationContext BrokenPoseContext;
	const UPlayerWeaponPresentationProfile* PoseProfileForValidation =
		BrokenPoseReference;
	TestEqual(
		TEXT("typed Procedural wrapper rejects a missing kit pose asset"),
		PoseProfileForValidation->IsDataValid(BrokenPoseContext),
		EDataValidationResult::Invalid);

	UPlayerWeaponPresentationProfile* BoltProfile =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	BoltProfile->ManualCycleAction = EPlayerWeaponManualCycleAction::Bolt;
	TArray<FString> BoltErrors;
	BoltProfile->ValidateConfiguration(BoltErrors);
	TestTrue(
		TEXT("bolt-action policy requires its cycle action"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(
			BoltErrors, TEXT("BoltCycle")));
	BoltProfile->ReloadActions.Add(
		PlayerWeaponPresentationProfileTest::MakeReloadAction(
			EPlayerWeaponReloadVariant::BoltCycle));
	BoltErrors.Reset();
	BoltProfile->ValidateConfiguration(BoltErrors);
	TestTrue(
		TEXT("bolt-action policy requires a mapped moving part"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(
			BoltErrors, TEXT("bolt moving-part")));
	FPlayerWeaponMovingPartMapping BoltMapping;
	BoltMapping.Part = EPlayerWeaponMovingPart::Bolt;
	BoltMapping.TargetName = TEXT("Bolt");
	BoltProfile->MovingPartMappings.Add(BoltMapping);
	BoltErrors.Reset();
	BoltProfile->ValidateConfiguration(BoltErrors);
	TestEqual(TEXT("bolt profile validates with its cycle action"), BoltErrors.Num(), 0);

	UPlayerWeaponPresentationProfile* PumpProfile =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	PumpProfile->ManualCycleAction = EPlayerWeaponManualCycleAction::Pump;
	TArray<FString> PumpErrors;
	PumpProfile->ValidateConfiguration(PumpErrors);
	TestTrue(
		TEXT("pump-action policy requires its cycle action"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(
			PumpErrors, TEXT("PumpCycle")));
	PumpProfile->ReloadActions.Add(
		PlayerWeaponPresentationProfileTest::MakeReloadAction(
			EPlayerWeaponReloadVariant::PumpCycle));
	PumpErrors.Reset();
	PumpProfile->ValidateConfiguration(PumpErrors);
	TestTrue(
		TEXT("pump-action policy requires a mapped moving part"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(
			PumpErrors, TEXT("pump moving-part")));
	FPlayerWeaponMovingPartMapping PumpMapping;
	PumpMapping.Part = EPlayerWeaponMovingPart::Pump;
	PumpMapping.TargetName = TEXT("Pump");
	PumpProfile->MovingPartMappings.Add(PumpMapping);
	PumpErrors.Reset();
	PumpProfile->ValidateConfiguration(PumpErrors);
	TestEqual(TEXT("pump profile validates with its cycle action"), PumpErrors.Num(), 0);

	UWeaponDataAsset* MissingInterruptionWeapon =
		NewObject<UWeaponDataAsset>(GetTransientPackage());
	MissingInterruptionWeapon->PlayerPresentationProfile =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	MissingInterruptionWeapon->PlayerPresentationProfile->ReloadActions.RemoveAll(
		[](const FPlayerWeaponReloadAction& Action)
		{
			return Action.Variant == EPlayerWeaponReloadVariant::Interrupted;
		});
	TArray<FString> MissingInterruptionErrors;
	MissingInterruptionWeapon->ValidatePlayerPresentation(
		MissingInterruptionErrors);
	TestTrue(
		TEXT("reload profiles cannot omit interruption handling"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(
			MissingInterruptionErrors, TEXT("Interrupted")));

	UWeaponDataAsset* ShellWeapon = NewObject<UWeaponDataAsset>(GetTransientPackage());
	ShellWeapon->bShellByShellReload = true;
	ShellWeapon->PlayerPresentationProfile =
		PlayerWeaponPresentationProfileTest::MakeValidRifleProfile();
	TArray<FString> ShellErrors;
	ShellWeapon->ValidatePlayerPresentation(ShellErrors);
	TestTrue(
		TEXT("shell reload requires a shell-insert presentation action"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(ShellErrors, TEXT("ShellInsert")));

	ShellWeapon->PlayerPresentationProfile->ReloadActions.Add(
		PlayerWeaponPresentationProfileTest::MakeReloadAction(EPlayerWeaponReloadVariant::ShellInsert));
	ShellErrors.Reset();
	ShellWeapon->ValidatePlayerPresentation(ShellErrors);
	TestTrue(
		TEXT("shell reload also requires its start action"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(ShellErrors, TEXT("ShellStart")));
	TestTrue(
		TEXT("shell reload also requires its end action"),
		PlayerWeaponPresentationProfileTest::HasErrorContaining(ShellErrors, TEXT("ShellEnd")));

	ShellWeapon->PlayerPresentationProfile->ReloadActions.Add(
		PlayerWeaponPresentationProfileTest::MakeReloadAction(EPlayerWeaponReloadVariant::ShellStart));
	ShellWeapon->PlayerPresentationProfile->ReloadActions.Add(
		PlayerWeaponPresentationProfileTest::MakeReloadAction(EPlayerWeaponReloadVariant::ShellEnd));
	ShellErrors.Reset();
	ShellWeapon->ValidatePlayerPresentation(ShellErrors);
	TestEqual(TEXT("complete shell profile validates"), ShellErrors.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
