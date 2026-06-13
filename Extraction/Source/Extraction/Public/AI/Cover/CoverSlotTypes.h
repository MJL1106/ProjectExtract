#pragma once

#include "CoreMinimal.h"
#include "CoverSlotTypes.generated.h"

UENUM(BlueprintType)
enum class ECoverHeight : uint8
{
	Stand,
	Crouch
};

UENUM(BlueprintType)
enum class ECoverPeekSide : uint8
{
	Left,
	Right,
	Either
};
