// Shared enum types for the player→companion command spine.

#pragma once

#include "CoreMinimal.h"
#include "CompanionCommandTypes.generated.h"

UENUM(BlueprintType)
enum class ECompanionCommand : uint8
{
	None,
	Breach,
	Takedown,
};

UENUM(BlueprintType)
enum class ETakedownMethod : uint8
{
	Knife,
	Shoot,
};
