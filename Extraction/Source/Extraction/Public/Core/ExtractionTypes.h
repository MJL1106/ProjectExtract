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
