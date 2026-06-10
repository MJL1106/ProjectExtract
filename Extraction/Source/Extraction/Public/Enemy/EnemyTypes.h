// Enemy archetype and awareness enums used across the enemy AI system.

#pragma once

#include "CoreMinimal.h"
#include "EnemyTypes.generated.h"

UENUM(BlueprintType)
enum class EEnemyArchetype : uint8
{
	Grunt		UMETA(DisplayName = "Grunt"),
	Rusher		UMETA(DisplayName = "Rusher"),
	Heavy		UMETA(DisplayName = "Heavy"),
	Sniper		UMETA(DisplayName = "Sniper"),
	Officer		UMETA(DisplayName = "Officer"),
	Grenadier	UMETA(DisplayName = "Grenadier"),
	Shield		UMETA(DisplayName = "Shield"),
};

UENUM(BlueprintType)
enum class EEnemyAwarenessState : uint8
{
	Unaware		UMETA(DisplayName = "Unaware"),
	Suspicious	UMETA(DisplayName = "Suspicious"),
	Searching	UMETA(DisplayName = "Searching"),
	Combat		UMETA(DisplayName = "Combat"),
};

UENUM(BlueprintType)
enum class EEnemyMoveSpeedMode : uint8
{
	Patrol	UMETA(DisplayName = "Patrol"),
	Combat	UMETA(DisplayName = "Combat"),
};

/** Per-enemy morale state (Phase 4). Floor = Broken — enemies never rout (design §7). */
UENUM(BlueprintType)
enum class EMoraleState : uint8
{
	Confident	UMETA(DisplayName = "Confident"),
	Shaken		UMETA(DisplayName = "Shaken"),
	Broken		UMETA(DisplayName = "Broken"),
};

/** Mission phase — set by level scripting, consumed by the director for pacing and composition. */
UENUM(BlueprintType)
enum class EMissionPhase : uint8
{
	Infiltration	UMETA(DisplayName = "Infiltration"),
	Objective		UMETA(DisplayName = "Objective"),
	Extraction		UMETA(DisplayName = "Extraction"),
};

/** Level-wide alert state. One-way ladder until the director (Phase 6) owns de-escalation. */
UENUM(BlueprintType)
enum class EGlobalAlertLevel : uint8
{
	Calm		UMETA(DisplayName = "Calm"),
	Searching	UMETA(DisplayName = "Searching"),
	Loud		UMETA(DisplayName = "Loud"),
};

/** Bark categories — the legibility layer (design §8). Subtitle text first, VO later. */
UENUM(BlueprintType)
enum class EBarkType : uint8
{
	HeardSomething	UMETA(DisplayName = "Heard Something"),
	SearchArea		UMETA(DisplayName = "Search Area"),
	Contact			UMETA(DisplayName = "Contact"),
	LostTarget		UMETA(DisplayName = "Lost Target"),
	BodyFound		UMETA(DisplayName = "Body Found"),
	GrenadeOut		UMETA(DisplayName = "Grenade Out"),
	ManDown			UMETA(DisplayName = "Man Down"),
	Pinned			UMETA(DisplayName = "Pinned"),
	FallingBack		UMETA(DisplayName = "Falling Back"),
	Flanking		UMETA(DisplayName = "Flanking"),
	Suppressing		UMETA(DisplayName = "Suppressing"),
	FocusTarget		UMETA(DisplayName = "Focus Target"),
};
