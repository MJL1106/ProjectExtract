// Shared types for the pre-authored companion route tool.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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

/** What crossing an ACompanionRouteTrigger does. */
UENUM(BlueprintType)
enum class ECompanionRouteTriggerMode : uint8
{
	/** Walk the route — the original behaviour. Deliberately the zero value: every trigger placed
	 *  before this property existed deserializes to 0 and must keep doing exactly what it did. */
	ExecuteRoute         UMETA(DisplayName = "Execute Route"),

	/** Install the route as the companion's facing reference. The route is never walked — the
	 *  companion follows the player as usual but faces the way the route runs. */
	SetFacingReference   UMETA(DisplayName = "Set Facing Reference"),

	/** Uninstall the facing reference. Needs no route — for sections with no meaningful direction. */
	ClearFacingReference UMETA(DisplayName = "Clear Facing Reference"),
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
	 *  On the FINAL waypoint this also defines the authored hold facing. Ignored when AimTargetActor is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint")
	bool bOverrideAim = false;

	/** Local-space aim target; draggable. Used when bOverrideAim and AimTargetActor is unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint", meta = (MakeEditWidget, EditCondition = "bOverrideAim"))
	FVector AimTargetLocal = FVector(300.f, 0.f, 0.f);

	/** Level actor to keep looking at on this leg (e.g. a door), highest aim priority. Assign the same
	 *  actor across a run of waypoints to hold focus down a whole stretch (a stairwell descent, say).
	 *  Null = unused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Waypoint")
	TObjectPtr<AActor> AimTargetActor;

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

	/** True if this waypoint authors an aim target (actor or local point) rather than relying on the
	 *  procedural look-ahead. Used to decide whether the BT task's aim-focus tick should hold on the
	 *  authored target instead of following the path. */
	bool HasAuthoredAim() const { return IsValid(AimTargetActor) || bOverrideAim; }
};
