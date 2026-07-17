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
	SMG			UMETA(DisplayName = "SMG"),
	Shotgun		UMETA(DisplayName = "Shotgun"),
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

/** Accepted reload transitions exposed to presentation listeners. */
UENUM(BlueprintType)
enum class EWeaponReloadPhase : uint8
{
	Started			UMETA(DisplayName = "Started"),
	ShellInserted	UMETA(DisplayName = "Shell Inserted"),
	Completed		UMETA(DisplayName = "Completed"),
	Interrupted		UMETA(DisplayName = "Interrupted"),
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
