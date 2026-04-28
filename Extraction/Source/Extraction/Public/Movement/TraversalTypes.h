// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraversalTypes.generated.h"

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
