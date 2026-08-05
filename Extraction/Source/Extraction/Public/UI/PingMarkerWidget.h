// UPingMarkerWidget -- small world-anchored marker that tracks the pinged target on screen. Shares
// UObjectiveMarkerWidget's projection maths (FScreenMarkerProjection): on-screen it sits on the
// target, off-screen or behind the camera it slides along the padded viewport edge with a chevron
// angle for the WBP to point at. Tracks two states -- AIMING (a live candidate under the crosshair,
// not yet committed) and PLACED (a committed ping) -- and collapses only when neither is live.
//
// Chevron convention matches the objective marker: EdgeAngleDegrees is 0 when the target lies to
// the RIGHT of screen centre and increases clockwise.
//
// WBP must contain:
//   UImage "MarkerIcon" -- the marker image positioned by this widget

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Companion/CompanionCommandTypes.h"
#include "UI/ScreenMarkerProjection.h"
#include "PingMarkerWidget.generated.h"

class UCompanionCommandComponent;
class UImage;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UPingMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// --- Per-frame state the WBP reads ---

	/** True while the target is behind the camera or outside the padded viewport rectangle. */
	UPROPERTY(BlueprintReadOnly, Category = "Ping|Marker")
	bool bIsOffScreen = false;

	/** Chevron rotation in degrees, [0, 360). Only meaningful while bIsOffScreen. */
	UPROPERTY(BlueprintReadOnly, Category = "Ping|Marker")
	float EdgeAngleDegrees = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Ping|Marker")
	float DistanceMeters = 0.f;

	/** Command the live ping resolved to (breach/search/loot/takedown/cover/none). Hook for
	 *  per-ping-type glyphs — nothing consumes it yet. */
	UPROPERTY(BlueprintReadOnly, Category = "Ping|Marker")
	ECompanionCommand PingCommand = ECompanionCommand::None;

	// --- Events the WBP implements ---

	/** Fired once per ping, on the first frame the marker resolves a position. Intro pulse hook. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Ping|Marker")
	void OnPingMarkerAppeared();

	/** Fired whenever a tracked value actually moves. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Ping|Marker")
	void OnPingMarkerUpdated(bool bOffScreen, float EdgeAngle, float Distance);

	/** Raised on the aiming<->placed edge -- true while merely aiming at a pingable target (ring +
	 *  diamond + "MMB PING" tag per the V5 design), false once a ping is committed (diamond +
	 *  distance only). Driven from C++ rather than a separate bridge hop since C++ already owns
	 *  this widget's visibility/collapse decisions -- implement by calling SetPingState(bAiming). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Ping|Marker")
	void OnPingAimingStateChanged(bool bAiming);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- Bound widget (designer wires in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> MarkerIcon;

	// --- Tuning ---

	/** Vertical offset (screen pixels) applied on-screen. Defaults to zero: the marker now tracks
	 *  the actual ping impact point (world-anchored via the command component), which needs no
	 *  lift — a fixed offset would slide the marker off that point under camera rotation. */
	UPROPERTY(EditAnywhere, Category = "Ping|Marker")
	float VerticalScreenOffset = 0.f;

	/** Minimum gap (DPI-scaled px) the marker keeps from every viewport edge. */
	UPROPERTY(EditDefaultsOnly, Category = "Ping|Marker", meta = (ClampMin = "0.0"))
	float EdgeMargin = 64.f;

	/** Extra inset the target must clear before an off-screen marker flips back to on-screen. Capped
	 *  for the same reason as the objective marker's: an unbounded value floors the on-screen test
	 *  rect and strands the marker off-screen permanently. */
	UPROPERTY(EditDefaultsOnly, Category = "Ping|Marker", meta = (ClampMin = "0.0", ClampMax = "128.0"))
	float ReentryHysteresis = 16.f;

	/** Screen-space smoothing speed; higher tracks tighter. 0 snaps every frame. */
	UPROPERTY(EditDefaultsOnly, Category = "Ping|Marker", meta = (ClampMin = "0.0"))
	float PositionInterpSpeed = 14.f;

	/** When true an off-screen ping slides along the screen edge; when false it fades out instead. */
	UPROPERTY(EditDefaultsOnly, Category = "Ping|Marker")
	bool bClampWhenOffScreen = true;

private:
	UFUNCTION()
	void HandlePingChanged(ECompanionCommand PendingCommand, AActor* PingedTarget);

	/** Un-collapse-only counterpart to HandlePingChanged for the candidate probe. Collapsing back
	 *  out is NativeTick's job (it already owns every other reason this marker collapses, e.g. a
	 *  committed target dying mid-ping) -- Slate does not tick a collapsed widget, so THIS is the
	 *  one transition (nothing showing, now something is) that must be driven off the delegate. */
	UFUNCTION()
	void HandlePingCandidateChanged(bool bHasCandidate, FVector WorldLocation);

	/** Resolved world position of whatever this marker is tracking -- a committed ping if one
	 *  exists, else a live candidate, else false. bOutAiming is true only for the candidate case:
	 *  a committed ping always outranks a candidate, so the two states never render at once. */
	bool GetTrackedLocation(FVector& OutLocation, bool& bOutAiming) const;

	/** Re-arms tracking (resets smoothing and queues the appear pulse) only when NewLocation is
	 *  meaningfully different from the last one tracked -- shared by every NativeTick pass so a
	 *  candidate resolving into a committed ping at the same point (or the reverse, clearing a ping
	 *  while still aiming at the same spot) does not slide in or re-pulse for a target that never
	 *  actually changed. A genuinely different point re-arms exactly like a brand new ping always
	 *  has. bHasLocation false (nothing tracked) resets the baseline so the NEXT appearance is
	 *  always treated as fresh. */
	void TrackLocation(const FVector& NewLocation, bool bHasLocation);

	/** Edge-only wrapper around OnPingAimingStateChanged, mirroring OnPingMarkerUpdated's own dedup. */
	void BroadcastAimingStateIfChanged(bool bAiming);

	void ApplyProjection(const FScreenMarkerProjection& Projection, float DeltaTime);

	TWeakObjectPtr<UCompanionCommandComponent> CachedCommandComp;

	FScreenMarkerViewContext ViewContext;

	FVector2D SmoothedPosition = FVector2D::ZeroVector;
	FVector2D LastEdgeDirection = FVector2D(0.f, 1.f);
	bool bHasSmoothedPosition = false;
	bool bPendingAppearEvent = false;

	bool bLastBroadcastOffScreen = false;
	float LastBroadcastAngle = 0.f;
	float LastBroadcastDistance = 0.f;
	bool bHasBroadcastState = false;

	/** Re-arm baseline for TrackLocation -- the last world point actually tracked, world-space (not
	 *  screen-space) because the point must stay comparable across camera movement. */
	FVector LastTrackedWorldLocation = FVector::ZeroVector;
	bool bHasTrackedWorldLocation = false;

	/** Dedup baseline for OnPingAimingStateChanged. */
	bool bLastAiming = false;
	bool bHasBroadcastAiming = false;

	/** Below this, two tracked points count as the same target. Deliberately generous: the
	 *  candidate probe samples at 10Hz (UCompanionCommandComponent::PingCandidateInterval), so its
	 *  last reading can be up to ~100ms stale relative to a fresh IssuePing trace at the moment of
	 *  commit, and camera rotation during that window shifts a distant hit point by tens of
	 *  centimetres even when the player never moved off the same physical target. No actor identity
	 *  crosses this boundary (OnPingCandidateChanged carries a location only, deliberately, per the
	 *  bridge contract), so distance is the only continuity signal available. */
	static constexpr float TargetContinuityEpsilonCm = 50.f;
};
