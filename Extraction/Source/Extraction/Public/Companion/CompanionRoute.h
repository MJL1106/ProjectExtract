// Designer-placed one-shot route for the AI companion.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Companion/CompanionRouteTypes.h"
#include "CompanionRoute.generated.h"

class UBillboardComponent;
class UCompanionRouteVisComponent;

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

	/** Route-wide focus actor (e.g. a door): aimed at on every leg that authors no aim of its own.
	 *  A waypoint's AimTargetActor or bOverrideAim wins over this for its leg. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	TObjectPtr<AActor> AimTargetActor;

	/** How close the companion must get to a waypoint before it counts as reached (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0.0"))
	float WaypointAcceptanceRadius = 60.f;

	/** Hang-safety: max seconds a wait-for-player gate will block before auto-advancing (0 = forever). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "0.0"))
	float MaxWaitForPlayerSeconds = 15.f;

	/** Route-wide companion walk speed (cm/s). 0 = use each leg's stance default.
	 *  A waypoint's own SpeedOverride still wins over this when set (> 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Speed", meta = (ClampMin = "0.0"))
	float CompanionSpeed = 0.f;

	/** Locks the player's walk speed to this value for the duration of the route (0 = no lock).
	 *  Governs regular walking AND sprinting; crouch is capped but never boosted.
	 *  Applied/released by BTTask_CompanionFollowRoute via AExtractionPlayer::SetRouteSpeedLock. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Speed", meta = (ClampMin = "0.0"))
	float PlayerSpeedLock = 0.f;

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

	/** Returns the world-space aim target for a given waypoint. Priority: waypoint AimTargetActor
	 *  (bounds center, falling back to actor location) > bOverrideAim's AimTargetLocal (transformed
	 *  to world) > route-wide AimTargetActor > a procedural look-ahead 300 cm beyond this waypoint
	 *  toward the next. */
	UFUNCTION(BlueprintPure, Category = "Route")
	FVector GetWaypointAimWorld(int32 Index) const;

	/** True when the leg into Index has an authored aim: the waypoint's focus actor / local
	 *  override, or the route-wide AimTargetActor. */
	bool HasAuthoredAim(int32 Index) const;

	/** Plain C++ accessor — returns the waypoint struct at the clamped index. */
	const FCompanionRouteWaypoint& GetWaypoint(int32 Index) const;

	/** Called by the BT task when the companion arrives at a waypoint. */
	void NotifyWaypointReached(int32 Index);

	/** Called by the BT task when the route finishes or is aborted. */
	void NotifyRouteCompleted(bool bAborted);

	/** Draws debug spheres, lines, and aim arrows when companion.RouteDebug is non-zero. */
	void DrawDebugRoute() const;

	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	/** Editor-only authoring UX: new/duplicated waypoints inherit the previous entry's settings and
	 *  are offset along the last leg's direction so they don't spawn stacked on top of it. */
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Route")
	TObjectPtr<UBillboardComponent> Billboard;

	/** Editor-only marker component: lets FCompanionRouteVisualizer (ExtractionEditor module)
	 *  register per-component-class and draw/edit waypoints in the level viewport. */
	UPROPERTY(VisibleAnywhere, Category = "Route")
	TObjectPtr<UCompanionRouteVisComponent> RouteVisComponent;

	/** Static fallback returned when Waypoints is empty. */
	static const FCompanionRouteWaypoint DefaultWaypoint;

	/** Procedural look-ahead distance (cm) used when bOverrideAim is false. */
	static constexpr float LookAheadDistance = 300.f;

	/** Placement offset (cm) applied to a newly added/duplicated waypoint along the last leg's
	 *  direction, so its widget doesn't spawn stacked on the entry it was copied from. */
	static constexpr float NewWaypointOffsetDistance = 150.f;
};
