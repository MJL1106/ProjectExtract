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
	Loot,
	Explore, // location command (no target actor) — go search a pinged spot, loot what's there
};

UENUM(BlueprintType)
enum class ETakedownMethod : uint8
{
	Knife,
	Shoot,
};

/** How the companion executes a breach. Derived from ECompanionMode at confirm time:
 *  Combat -> Loud, Normal -> Tactical, Stealth -> Quiet. Drives the montage + noise profile.
 *  Tactical is deliberately value 0 — a missing/unset blackboard key reads as 0, and the safe
 *  default is the middle option, not Loud. */
UENUM(BlueprintType)
enum class EBreachType : uint8
{
	Tactical,
	Loud,
	Quiet,
};
