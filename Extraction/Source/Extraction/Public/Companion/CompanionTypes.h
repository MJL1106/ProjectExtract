// Shared companion enums — player-commanded mode, posture state and peek side.

#pragma once

#include "CoreMinimal.h"
#include "CompanionTypes.generated.h"

/** Player-commanded directive. Sits above ECompanionPosture: mode is what the player ordered,
 *  posture is the situational state the BT service derives from it (and from combat). */
UENUM(BlueprintType)
enum class ECompanionMode : uint8
{
	Normal  UMETA(DisplayName = "Normal"),
	Combat  UMETA(DisplayName = "Combat"),
	Stealth UMETA(DisplayName = "Stealth"),
};

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

/** Stealth catch-up stage, set by the follow task from distance-to-player. Shapes
 *  ACompanionCharacter::TunedWalkSpeed/TunedCrouchedWalkSpeed: FastCrouch hustles at whatever
 *  stance the companion currently holds (standing or crouched — stance is the crouch-mirror's
 *  call, not the catch-up ladder's), Sprint is the only state allowed to break the stealth
 *  sprint veto. */
UENUM(BlueprintType)
enum class EStealthCatchup : uint8
{
	None       UMETA(DisplayName = "None"),
	FastCrouch UMETA(DisplayName = "Fast Crouch"),
	Sprint     UMETA(DisplayName = "Sprint Break"),
};
