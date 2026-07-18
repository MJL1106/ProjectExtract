// Shared typed records for player weapon presentation assets.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequenceBase.h"
#include "PlayerWeaponPresentationTypes.generated.h"

UENUM(BlueprintType)
enum class EPlayerWeaponSeatPolicy : uint8
{
	WeaponSeatMarker	UMETA(DisplayName = "Authored Weapon Seat Marker"),
	LegacyViewRoot		UMETA(DisplayName = "Legacy View Root"),
};

UENUM(BlueprintType)
enum class EPlayerWeaponAttachmentSlot : uint8
{
	Optic				UMETA(DisplayName = "Optic"),
	UnderbarrelGrip	UMETA(DisplayName = "Underbarrel Grip"),
};

UENUM(BlueprintType)
enum class EPlayerWeaponSupportHandStyle : uint8
{
	Standard	UMETA(DisplayName = "Standard"),
	Vertical	UMETA(DisplayName = "Vertical Grip"),
	Angled		UMETA(DisplayName = "Angled Grip"),
	None		UMETA(DisplayName = "No Support Hand"),
};

UENUM(BlueprintType)
enum class EPlayerWeaponReloadVariant : uint8
{
	Tactical	UMETA(DisplayName = "Tactical"),
	Empty		UMETA(DisplayName = "Empty"),
	ShellStart	UMETA(DisplayName = "Shell Start"),
	ShellInsert	UMETA(DisplayName = "Shell Insert"),
	ShellEnd	UMETA(DisplayName = "Shell End"),
	BoltCycle	UMETA(DisplayName = "Bolt Cycle"),
	PumpCycle	UMETA(DisplayName = "Pump Cycle"),
	Interrupted	UMETA(DisplayName = "Interrupted"),
};

UENUM(BlueprintType)
enum class EPlayerWeaponManualCycleAction : uint8
{
	None	UMETA(DisplayName = "None"),
	Bolt	UMETA(DisplayName = "Bolt Cycle"),
	Pump	UMETA(DisplayName = "Pump Cycle"),
};

UENUM(BlueprintType)
enum class EPlayerWeaponMovingPart : uint8
{
	Magazine		UMETA(DisplayName = "Magazine"),
	Bolt			UMETA(DisplayName = "Bolt"),
	Slide			UMETA(DisplayName = "Slide"),
	Pump			UMETA(DisplayName = "Pump"),
	ChargingHandle	UMETA(DisplayName = "Charging Handle"),
	Trigger			UMETA(DisplayName = "Trigger"),
	Selector		UMETA(DisplayName = "Selector"),
};

UENUM(BlueprintType)
enum class EPlayerWeaponMovingPartTarget : uint8
{
	SkeletalBone	UMETA(DisplayName = "Skeletal Bone"),
	SceneComponent	UMETA(DisplayName = "Scene Component"),
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FPlayerWeaponHandOffsets
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation")
	FTransform HandGunSocketOffset = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation")
	FTransform RightHandSocketOffset = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation")
	FTransform LeftHandSocketOffset = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FPlayerWeaponADSDefaults
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|ADS",
		meta = (ClampMin = "20.0", ClampMax = "120.0"))
	float FieldOfView = 65.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|ADS",
		meta = (ClampMin = "0.01"))
	float TransitionTime = 0.15f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|ADS",
		meta = (ClampMin = "0.01"))
	float SensitivityMultiplier = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|ADS",
		meta = (ClampMin = "0.0"))
	float AimDistanceFromCameraCm = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|ADS",
		meta = (ClampMin = "0.0"))
	float EyeReliefCm = 0.f;
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FPlayerWeaponResolvedSight
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Presentation|ADS")
	bool bIsValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Presentation|ADS")
	bool bUsesOptic = false;

	/** Stable optic attachment ID. NAME_None identifies the authored iron sights. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Presentation|ADS")
	FName OpticId = NAME_None;

	/** Canonical aim anchor relative to the animated ik_hand_gun socket. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Presentation|ADS")
	FTransform AimSourceInHandSpace = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Presentation|ADS")
	FPlayerWeaponADSDefaults ADSSettings;
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FPlayerWeaponSupportHandProfile
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Grip")
	EPlayerWeaponSupportHandStyle Style = EPlayerWeaponSupportHandStyle::Standard;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Grip")
	TSoftObjectPtr<UAnimSequenceBase> HandPose;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Grip")
	FTransform EffectorOffset = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Grip")
	FVector JointTargetOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Grip",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IKWeight = 1.f;
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FPlayerWeaponReloadAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Reload")
	EPlayerWeaponReloadVariant Variant = EPlayerWeaponReloadVariant::Tactical;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Reload")
	TSoftObjectPtr<UAnimSequenceBase> ArmsAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Reload")
	TSoftObjectPtr<UAnimSequenceBase> WeaponAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Reload",
		meta = (ClampMin = "0.01"))
	float PlayRate = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Reload")
	bool bBlendOutOnly = false;
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FPlayerWeaponMarkerRequirements
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	bool bRequireWeaponSeat = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	bool bRequireSupportHand = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	bool bRequireIronRear = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	bool bRequireIronFront = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	bool bRequireOpticMount = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	bool bRequireMuzzle = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	bool bRequireCasing = false;
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FPlayerWeaponMovingPartMapping
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Moving Parts")
	EPlayerWeaponMovingPart Part = EPlayerWeaponMovingPart::Magazine;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Moving Parts")
	EPlayerWeaponMovingPartTarget TargetKind = EPlayerWeaponMovingPartTarget::SkeletalBone;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Moving Parts")
	FName TargetName = NAME_None;
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FPlayerWeaponOpticOverride
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Optic")
	bool bOverrideFieldOfView = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Optic",
		meta = (EditCondition = "bOverrideFieldOfView", ClampMin = "20.0", ClampMax = "120.0"))
	float FieldOfView = 55.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Optic")
	bool bOverrideTransitionTime = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Optic",
		meta = (EditCondition = "bOverrideTransitionTime", ClampMin = "0.01"))
	float TransitionTime = 0.15f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Optic")
	bool bOverrideSensitivity = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Optic",
		meta = (EditCondition = "bOverrideSensitivity", ClampMin = "0.01"))
	float SensitivityMultiplier = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Optic")
	bool bOverrideAimDistance = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Optic",
		meta = (EditCondition = "bOverrideAimDistance", ClampMin = "0.0"))
	float AimDistanceFromCameraCm = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Optic")
	bool bOverrideEyeRelief = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Optic",
		meta = (EditCondition = "bOverrideEyeRelief", ClampMin = "0.0"))
	float EyeReliefCm = 0.f;

	bool HasAnyOverride() const
	{
		return bOverrideFieldOfView
			|| bOverrideTransitionTime
			|| bOverrideSensitivity
			|| bOverrideAimDistance
			|| bOverrideEyeRelief;
	}
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FPlayerWeaponGripOverride
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Grip")
	bool bOverrideSupportHandStyle = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Presentation|Grip",
		meta = (EditCondition = "bOverrideSupportHandStyle"))
	EPlayerWeaponSupportHandStyle SupportHandStyle = EPlayerWeaponSupportHandStyle::Standard;
};
