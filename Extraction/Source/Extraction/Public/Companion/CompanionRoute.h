// Designer-placed one-shot route for the AI companion.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Companion/CompanionRouteTypes.h"
#include "CompanionRoute.generated.h"

class UBillboardComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCompanionRouteCompleted, bool, bAborted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCompanionRouteWaypointReached, int32, WaypointIndex);

UCLASS(Blueprintable)
class EXTRACTION_API ACompanionRoute : public AActor
{
	GENERATED_BODY()

public:
	ACompanionRoute();

	// --- Waypoint data ---

	/** Ordered waypoints. Each entry carries its own stance, aim, dwell, and gate settings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (TitleProperty = "Stance"))
	TArray<FCompanionRouteWaypoint> Waypoints;

	/** What the companion does after reaching the final waypoint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	ECompanionRouteEndBehavior EndBehavior = ECompanionRouteEndBehavior::ReturnToFollow;

	/** If true, combat perception can abort the route mid-walk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	bool bInterruptibleByCombat = false;

	/** Default stance shown in the editor; informational — each waypoint can override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	ECompanionRouteStance DefaultStance = ECompanionRouteStance::Relaxed;

	/** How close the companion must get to a waypoint before it counts as reached (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0.0"))
	float WaypointAcceptanceRadius = 60.f;

	/** Hang-safety: max seconds a wait-for-player gate will block before auto-advancing (0 = forever). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0.0"))
	float MaxWaitForPlayerSeconds = 15.f;

	// --- Delegates ---

	UPROPERTY(BlueprintAssignable, Category = "Route|Events")
	FCompanionRouteCompleted OnRouteCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Route|Events")
	FCompanionRouteWaypointReached OnWaypointReached;

	// --- Public API ---

	UFUNCTION(BlueprintPure, Category = "Route")
	int32 NumPoints() const;

	UFUNCTION(BlueprintPure, Category = "Route")
	FVector GetWorldPoint(int32 Index) const;

	/** Returns the world-space aim target for a given waypoint.
	 *  If bOverrideAim is set, transforms the waypoint's AimTargetLocal.
	 *  Otherwise, returns a procedural look-ahead 300 cm beyond this waypoint toward the next. */
	UFUNCTION(BlueprintPure, Category = "Route")
	FVector GetWaypointAimWorld(int32 Index) const;

	/** Plain C++ accessor — returns the waypoint struct at the clamped index. */
	const FCompanionRouteWaypoint& GetWaypoint(int32 Index) const;

	/** Called by the BT task when the companion arrives at a waypoint. */
	void NotifyWaypointReached(int32 Index);

	/** Called by the BT task when the route finishes or is aborted. */
	void NotifyRouteCompleted(bool bAborted);

	/** Draws debug spheres, lines, and aim arrows when companion.RouteDebug is non-zero. */
	void DrawDebugRoute() const;

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Route")
	TObjectPtr<UBillboardComponent> Billboard;

	/** Static fallback returned when Waypoints is empty. */
	static const FCompanionRouteWaypoint DefaultWaypoint;

	/** Procedural look-ahead distance (cm) used when bOverrideAim is false. */
	static constexpr float LookAheadDistance = 300.f;
};
