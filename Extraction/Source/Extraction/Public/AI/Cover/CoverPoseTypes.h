// Cover pose enums shared by the cover system, pose component, and animation instances.

#pragma once

#include "CoreMinimal.h"
#include "CoverPoseTypes.generated.h"

UENUM(BlueprintType)
enum class ECoverHeight : uint8
{
	Stand,
	Crouch
};

UENUM(BlueprintType)
enum class ECoverLean : uint8
{
	None,
	Left,
	Right,
	Front
};
