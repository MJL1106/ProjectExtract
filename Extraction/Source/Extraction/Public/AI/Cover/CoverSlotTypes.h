#pragma once

#include "CoreMinimal.h"
#include "AI/Cover/CoverPoseTypes.h"
#include "CoverSlotTypes.generated.h"

// ECoverHeight now lives in CoverPoseTypes.h — this include keeps all existing includers compiling.

UENUM(BlueprintType)
enum class ECoverPeekSide : uint8
{
	Left,
	Right,
	Either
};
