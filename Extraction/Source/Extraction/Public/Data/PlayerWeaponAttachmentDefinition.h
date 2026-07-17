// Stable attachment identity, compatibility, and presentation overrides.

#pragma once

#include "CoreMinimal.h"
#include "Core/ExtractionTypes.h"
#include "Data/PlayerWeaponPresentationTypes.h"
#include "Engine/DataAsset.h"
#include "Weapon/PlayerWeaponView.h"
#include "PlayerWeaponAttachmentDefinition.generated.h"

UCLASS(BlueprintType)
class EXTRACTION_API UPlayerWeaponAttachmentDefinition : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, AssetRegistrySearchable, Category = "Attachment|Identity")
	FName AttachmentId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Identity")
	EPlayerWeaponAttachmentSlot Slot = EPlayerWeaponAttachmentSlot::Optic;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|View")
	TSoftClassPtr<APlayerWeaponAttachmentView> ViewClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Compatibility")
	TSet<EWeaponType> CompatibleWeaponTypes;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Presentation")
	FPlayerWeaponOpticOverride OpticOverride;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Presentation")
	FPlayerWeaponGripOverride GripOverride;

	UFUNCTION(BlueprintPure, Category = "Attachment|Compatibility")
	bool IsCompatibleWith(EWeaponType WeaponType) const;

	void ValidateConfiguration(
		TArray<FString>& OutErrors, bool bResolveSoftReferences = false) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
