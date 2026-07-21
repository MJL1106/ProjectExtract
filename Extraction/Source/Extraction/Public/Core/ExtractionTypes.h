// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "Movement/TraversalTypes.h"
#include "ExtractionTypes.generated.h"

// --- Gameplay Tags ---
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Enemy);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Companion);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Player);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_BT_EnemyCombat);

/**
 * Weapon type categories.
 * Add new entries here when introducing new weapon classes.
 */
UENUM(BlueprintType)
enum class EWeaponType : uint8
{
	Unarmed		UMETA(DisplayName = "Unarmed"),
	Pistol		UMETA(DisplayName = "Pistol"),
	Rifle		UMETA(DisplayName = "Rifle"),
};

/**
 * Identifies which transition animation to play when entering prone.
 * Selected at runtime based on the character's current movement state.
 */
UENUM(BlueprintType)
enum class EProneTransitionType : uint8
{
	None		UMETA(DisplayName = "None"),
	FromIdle	UMETA(DisplayName = "From Idle"),
	FromWalk	UMETA(DisplayName = "From Walk"),
	FromSprint	UMETA(DisplayName = "From Sprint"),
	FromCrouch	UMETA(DisplayName = "From Crouch"),
};

/**
 * Hit region categories for hitbox-based damage multipliers.
 * Mapped from skeleton bone names via BoneToHitRegionMap on the character.
 */
UENUM(BlueprintType)
enum class EHitRegion : uint8
{
	Head	UMETA(DisplayName = "Head"),
	Torso	UMETA(DisplayName = "Torso"),
	Arms	UMETA(DisplayName = "Arms"),
	Legs	UMETA(DisplayName = "Legs"),
};

/**
 * Current weapon operational state.
 * Drives animation selection and input gating.
 */
UENUM(BlueprintType)
enum class EWeaponState : uint8
{
	Idle		UMETA(DisplayName = "Idle"),
	Firing		UMETA(DisplayName = "Firing"),
	Reloading	UMETA(DisplayName = "Reloading"),
	Equipping	UMETA(DisplayName = "Equipping"),
};

/**
 * Attachment slot categories, mirroring the kit ST_Attachments fields.
 * Used by attachment DataAssets and world attachment pickups to identify which
 * slot an attachment occupies.
 */
UENUM(BlueprintType)
enum class EAttachmentSlot : uint8
{
	Sight		UMETA(DisplayName = "Sight"),
	Muzzle		UMETA(DisplayName = "Muzzle"),
	Laser		UMETA(DisplayName = "Laser"),
	Grip		UMETA(DisplayName = "Grip"),
	Handguard	UMETA(DisplayName = "Handguard"),
};

/**
 * Per-slot attachment selection for a weapon — raw kit enum bytes as written into
 * ST_Attachments by the loadout UI / attachment pickups. 0 = the slot's first kit
 * enum value (typically empty/ironsight), which resolves to no stat modifiers.
 */
USTRUCT(BlueprintType)
struct FWeaponAttachmentSelection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachments")
	uint8 Sight = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachments")
	uint8 Muzzle = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachments")
	uint8 Laser = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachments")
	uint8 Grip = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachments")
	uint8 Handguard = 0;
};

/**
 * Per-weapon recoil pattern data.
 * Points define camera offset per shot (X=yaw, Y=pitch).
 */
USTRUCT(BlueprintType)
struct FRecoilPattern
{
	GENERATED_BODY()

	/** Recoil offset per shot index (X=yaw, Y=pitch in degrees) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	TArray<FVector2D> Points;

	/** Time after last shot before pattern index resets to 0 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0.01"))
	float ResetDelay = 0.15f;

	/** Duration for camera to recover to pre-recoil rotation after firing stops */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0.01"))
	float RecoveryTime = 0.4f;

	/** Recoil scale multiplier while aiming down sights (1.0 = no reduction) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ADSMultiplier = 0.7f;
};
