// Shared types for the pre-authored companion route tool.

#pragma once

#include "CoreMinimal.h"
#include "CompanionRouteTypes.generated.h"

UENUM(BlueprintType)
enum class ECompanionRouteStance : uint8
{
	Relaxed UMETA(DisplayName = "Relaxed Walk"),
	Alert   UMETA(DisplayName = "Weapon-Up Alert"),
	Crouch  UMETA(DisplayName = "Stealth Crouch"),
};

UENUM(BlueprintType)
enum class ECompanionRouteEndBehavior : uint8
{
	ReturnToFollow UMETA(DisplayName = "Return To Follow"),
	HoldAtFinal    UMETA(DisplayName = "Hold At Final"),
};

USTRUCT(BlueprintType)
struct FCompanionRouteWaypoint
{
	GENERATED_BODY()

	/** Local-space position; draggable in the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint", meta = (MakeEditWidget))
	FVector Location = FVector::ZeroVector;

	/** Movement stance for the leg leading INTO this waypoint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint")
	ECompanionRouteStance Stance = ECompanionRouteStance::Relaxed;

	/** When true, the companion aims at AimTargetLocal on this leg instead of the procedural look-ahead.
	 *  On the FINAL waypoint this also defines the authored hold facing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint")
	bool bOverrideAim = false;

	/** Local-space aim target; draggable. Used when bOverrideAim. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint", meta = (MakeEditWidget, EditCondition = "bOverrideAim"))
	FVector AimTargetLocal = FVector(300.f, 0.f, 0.f);

	/** Hold at this waypoint until the player is within WaitForPlayerRadius before continuing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint")
	bool bWaitForPlayer = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint", meta = (ClampMin = "0.0", EditCondition = "bWaitForPlayer"))
	float WaitForPlayerRadius = 300.f;

	/** Extra dwell after arrival at this waypoint (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint", meta = (ClampMin = "0.0"))
	float DwellSeconds = 0.f;

	/** Per-leg movement speed override (cm/s). 0 = use the stance's default speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint", meta = (ClampMin = "0.0"))
	float SpeedOverride = 0.f;
};
