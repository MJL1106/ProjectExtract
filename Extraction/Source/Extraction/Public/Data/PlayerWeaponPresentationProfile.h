// Focused player presentation data, independent from weapon gameplay tuning.

#pragma once

#include "CoreMinimal.h"
#include "Core/ExtractionTypes.h"
#include "Data/PlayerWeaponAttachmentDefinition.h"
#include "Data/PlayerWeaponPresentationTypes.h"
#include "Engine/DataAsset.h"
#include "Weapon/PlayerWeaponView.h"
#include "PlayerWeaponPresentationProfile.generated.h"

UENUM(BlueprintType)
enum class EPlayerWeaponPresentationMigrationState : uint8
{
	Unconfigured	UMETA(DisplayName = "Unconfigured"),
	LegacyKitBridge	UMETA(DisplayName = "Legacy Procedural Bridge"),
	Profile			UMETA(DisplayName = "Presentation Profile"),
};

/**
 * Typed project boundary around the Procedural FPS Kit's opaque pose DataAsset.
 * The kit asset stays soft so loading a gameplay weapon does not pull presentation content.
 */
UCLASS(BlueprintType)
class EXTRACTION_API UPlayerWeaponProceduralPoseDefinition : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Procedural")
	TSoftObjectPtr<UDataAsset> KitPoseAsset;

	void ValidateConfiguration(
		TArray<FString>& OutErrors, bool bResolveSoftReferences = false) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType)
class EXTRACTION_API UPlayerWeaponPresentationProfile : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, AssetRegistrySearchable, Category = "Presentation|Identity")
	FName ProfileId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Identity")
	EWeaponType WeaponType = EWeaponType::Rifle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|View")
	TSoftClassPtr<APlayerWeaponView> ViewClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Procedural")
	TObjectPtr<UPlayerWeaponProceduralPoseDefinition> ProceduralPose;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Seat")
	EPlayerWeaponSeatPolicy SeatPolicy = EPlayerWeaponSeatPolicy::WeaponSeatMarker;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Hands")
	FPlayerWeaponHandOffsets HandOffsets;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Hands")
	EPlayerWeaponSupportHandStyle DefaultSupportHandStyle =
		EPlayerWeaponSupportHandStyle::Standard;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Hands")
	TArray<FPlayerWeaponSupportHandProfile> SupportHandProfiles;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|ADS")
	FPlayerWeaponADSDefaults ADSDefaults;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Reload")
	TArray<FPlayerWeaponReloadAction> ReloadActions;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Reload")
	EPlayerWeaponManualCycleAction ManualCycleAction =
		EPlayerWeaponManualCycleAction::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Markers")
	FPlayerWeaponMarkerRequirements MarkerRequirements;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Moving Parts")
	TArray<FPlayerWeaponMovingPartMapping> MovingPartMappings;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Attachments")
	TArray<TObjectPtr<UPlayerWeaponAttachmentDefinition>> CompatibleAttachments;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Attachments")
	FName DefaultOpticId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation|Attachments")
	FName DefaultGripId = NAME_None;

	const FPlayerWeaponSupportHandProfile* FindSupportHandProfile(
		EPlayerWeaponSupportHandStyle Style) const;
	const FPlayerWeaponReloadAction* FindReloadAction(
		EPlayerWeaponReloadVariant Variant) const;
	UPlayerWeaponAttachmentDefinition* FindAttachmentDefinition(FName AttachmentId) const;
	bool IsAttachmentCompatible(FName AttachmentId, EPlayerWeaponAttachmentSlot Slot) const;
	void ValidateConfiguration(
		TArray<FString>& OutErrors, bool bResolveSoftReferences = false) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
