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

	// --- Facing reference ---

	/** Lets this route be installed as the companion's facing reference by a route trigger set to
	 *  "Set Facing Reference": while following the player normally the companion faces the way this
	 *  route runs. It never walks the route, and formation and follow distance are untouched.
	 *  Needs at least 2 waypoints — the direction is read off the polyline, not the actor rotation.
	 *  Do NOT tick this on a route whose End Behavior is "Hold At Final": that route holds RouteActive
	 *  true forever and an active route outranks facing, so it can never face along itself. Author the
	 *  facing line as its own route actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Facing Reference")
	bool bUseAsFacingReference = false;

	/** Per-route look-ahead along the polyline (cm) used to derive the facing direction.
	 *  0 = use the companion tuning default. Larger values start the corner sweep earlier and turn
	 *  more gently; the override exists for sections whose legs are short relative to that default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route|Facing Reference", meta = (ClampMin = "0.0", EditCondition = "bUseAsFacingReference"))
	float FacingReferenceLookAheadOverride = 0.f;

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

	/** Look-ahead to use for facing: this route's override when authored (> 0), else TuningDefault.
	 *  The default arrives as a parameter so the route actor stays free of any tuning-asset coupling. */
	float GetFacingReferenceLookAhead(float TuningDefault) const;

	/**
	 * The direction this route runs past QueryLocation, for facing only — nothing here moves the
	 * companion. QueryLocation is projected onto the nearest non-degenerate segment, then LookAhead cm
	 * are walked forward along the polyline; OutHeading is the normalised projection -> look-ahead
	 * chord. Because that chord tracks the route line rather than the pawn, the heading is independent
	 * of how far off the line the companion happens to be standing, and it keeps pointing the way the
	 * section ran once the companion is past the final waypoint.
	 *
	 * OutHeading is the output that matters. OutLookAheadPoint and OutProjection are for the caller's
	 * range gates and debug draw ONLY — never anchor a focal point at either. A focal at the look-ahead
	 * point rotates facing by the companion's own lateral offset from the route (tens of degrees at
	 * realistic follow distances, i.e. facing the wall beside the corridor) and reverses past the end.
	 *
	 * False when the route isn't flagged for facing, has fewer than 2 waypoints, has no non-degenerate
	 * segment, or the chord came out zero (LookAhead <= 0).
	 */
	bool GetFacingReferenceAim(const FVector& QueryLocation, float LookAhead,
		FVector& OutHeading, FVector& OutLookAheadPoint, FVector& OutProjection) const;

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

	/** Editor-only authoring guard: warns on the Hold At Final + facing-reference combination, which
	 *  looks reasonable in the details panel but can never work. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
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
