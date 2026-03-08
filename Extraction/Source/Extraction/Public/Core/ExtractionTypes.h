// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ExtractionTypes.generated.h"

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
 * Active traversal action type.
 * Determines which montage plays and what clearance logic applies.
 */
UENUM(BlueprintType)
enum class ETraversalType : uint8
{
	None	UMETA(DisplayName = "None"),
	Vault	UMETA(DisplayName = "Vault"),
	Climb	UMETA(DisplayName = "Climb"),
	Mantle	UMETA(DisplayName = "Mantle"),
};
