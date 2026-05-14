// Shared companion enums — posture state and peek side.

#pragma once

#include "CoreMinimal.h"
#include "CompanionTypes.generated.h"

UENUM(BlueprintType)
enum class ECompanionPosture : uint8
{
	Exploration UMETA(DisplayName = "Exploration"),
	Combat      UMETA(DisplayName = "Combat"),
	Stealth     UMETA(DisplayName = "Stealth"),
};

UENUM(BlueprintType)
enum class EPeekSide : uint8
{
	Left  UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right"),
};
