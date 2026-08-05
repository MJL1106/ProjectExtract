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
class UObjectiveMarkerLayer;

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

	/** True while this ping's locator has been shifted off its true projected point to clear an
	 *  on-screen objective marker (see ObjectiveClearanceRadius). LeaderLineOffset is only meaningful
	 *  while this is true. */
	UPROPERTY(BlueprintReadOnly, Category = "Ping|Marker|Avoidance")
	bool bIsDisplaced = false;

	/** From the displaced locator position back to the ping's TRUE projected screen position, in
	 *  DPI-scaled slate units -- draw a 1px leader line along this vector in the WBP. Zero while
	 *  bIsDisplaced is false. */
	UPROPERTY(BlueprintReadOnly, Category = "Ping|Marker|Avoidance")
	FVector2D LeaderLineOffset = FVector2D::ZeroVector;

	/** True while this ping's world location is currently within ObjectivePingRadius of an on-screen
	 *  objective marker -- the objective marker's own OnObjectivePingFlash IS the feedback for that, so
	 *  the WBP should hide this widget's own locator and leader line entirely while this is set.
	 *  Re-evaluated every tick (ApplyObjectiveAvoidance), NOT latched at commit: a target-following
	 *  objective (escort/VIP) that walks away from the ping must stop suppressing the locator instead
	 *  of leaving it suppressed for the rest of the ping's life. */
	UPROPERTY(BlueprintReadOnly, Category = "Ping|Marker|Objective")
	bool bIsObjectivePing = false;

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

	/** Fired exactly once, on the aiming -> placed transition -- the same edge OnPingAimingStateChanged
	 *  detects, and never on a placed ping that was already placed (a distance update, or the marker
	 *  re-entering the screen). Drives a ~180ms one-shot broken-cyan pulse in the WBP. The pulse asset
	 *  is one-shot: a repeat fire here would read on screen as a second ping. Does NOT fire when this
	 *  ping resolved onto an objective (bIsObjectivePing) -- the objective's own flash already told the
	 *  player their ping landed, and a second cue on top of it would be redundant. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Ping|Marker")
	void OnPingConfirmed();

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

	/** Slate units -- how close the ping's projected position can get to an on-screen objective marker
	 *  before it counts as overlapping and the locator shifts clear of it. */
	UPROPERTY(EditDefaultsOnly, Category = "Ping|Marker|Avoidance", meta = (ClampMin = "0.0"))
	float ObjectiveClearanceRadius = 48.f;

	/** Displacement-shift smoothing speed -- eases the locator toward its clear position instead of
	 *  snapping when an objective marker drifts across the ping. Same convention as
	 *  PositionInterpSpeed: 0 snaps every frame. */
	UPROPERTY(EditDefaultsOnly, Category = "Ping|Marker|Avoidance", meta = (ClampMin = "0.0"))
	float AvoidanceInterpSpeed = 14.f;

	/** World centimetres -- how close a ping's resolved world location must be to an objective's own
	 *  resolved location before it counts as "the player pinged the objective itself" rather than a
	 *  nearby point. Defaulted to roughly a doorway's width: tight enough that a ping meant for
	 *  something merely near an objective does not get swallowed by it. */
	UPROPERTY(EditDefaultsOnly, Category = "Ping|Marker|Objective", meta = (ClampMin = "0.0"))
	float ObjectivePingRadius = 300.f;

	/** Multiplier on ObjectivePingRadius when LEAVING the suppressed state -- entering uses
	 *  ObjectivePingRadius itself (narrow), leaving requires clearing ObjectivePingRadius * this
	 *  (wide). Same convention as ReentryHysteresis's on/off-screen test: both sides of the proximity
	 *  check move every tick (a target-anchored ping resolves through the pinged actor, and
	 *  FObjectiveMarker::ResolveLocation re-queries the target's bounds base on every call), so without
	 *  this gap an objective sitting near the boundary flips bIsObjectivePing -- and the WBP's
	 *  show/hide of the locator with it -- every single frame on bounds noise alone. ClampMin 1.0:
	 *  below 1 would shrink the exit radius below the entry radius, inverting the hysteresis and
	 *  making the strobe worse instead of fixing it. */
	UPROPERTY(EditDefaultsOnly, Category = "Ping|Marker|Objective", meta = (ClampMin = "1.0"))
	float ObjectivePingExitRadiusMultiplier = 1.25f;

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

	/** Edge-only wrapper around OnPingAimingStateChanged, mirroring OnPingMarkerUpdated's own dedup.
	 *  Deliberately does nothing else -- see EvaluatePlacedEdge for why the confirm pulse and the
	 *  objective-ping check cannot ride this same edge. */
	void BroadcastAimingStateIfChanged(bool bAiming);

	/** Fires the confirmation pulse and the objective-ping commit check on the PLACED false->true edge
	 *  -- a SEPARATE latch (bLastPlaced) from BroadcastAimingStateIfChanged's aiming<->placed dedup
	 *  above, and deliberately so: that edge only fires for a ping that actually passed through the
	 *  AIMING state first, but a snap-ping (committed before the 10Hz candidate probe --
	 *  UCompanionCommandComponent::PingCandidateInterval -- has reported anything) or a re-ping of the
	 *  same target issued right after clearing the previous one (EvaluatePingCandidate refuses to
	 *  report a candidate while one is pending, so the probe is left a full interval behind) both
	 *  commit with NO preceding aiming frame -- the aiming edge never fires for either, so the confirm
	 *  pulse and the objective-ping check must not depend on it. Do not fold this back into
	 *  BroadcastAimingStateIfChanged. */
	void EvaluatePlacedEdge(bool bPlaced, const FVector& WorldLocation);

	void ApplyProjection(const FScreenMarkerProjection& Projection, const FVector& WorldLocation, float DeltaTime);

	/** Shifts the ping locator clear of every on-screen objective marker (or leaves it at TruePosition
	 *  when nothing needs clearing / this ping is suppressed), easing the shift via AvoidanceInterpSpeed
	 *  and updating bIsDisplaced / LeaderLineOffset / DisplayedPosition for this frame. TruePosition is
	 *  this tick's own smoothed projected point -- the value ApplyProjection would otherwise render
	 *  directly. WorldLocation is this tick's tracked world point, needed to re-evaluate bIsObjectivePing
	 *  every frame (see that property's own comment for why it is not latched). bSnap forces the
	 *  displaced position to jump rather than ease, matching ApplyProjection's own reset cases (first
	 *  frame, boundary crossed, bearing reversed). */
	void ApplyObjectiveAvoidance(const FVector2D& TruePosition, const FVector& WorldLocation, float DeltaTime, bool bSnap);

	/** Where the locator should sit so it clears every entry in MarkerPositions by at least
	 *  ObjectiveClearanceRadius, given it currently wants to be at TruePosition, without stepping
	 *  outside [RectMin, RectMax] (the same padded viewport rectangle ApplyProjection's own clamp
	 *  uses). Returns TruePosition unchanged when it already clears everything. Tries the four axis
	 *  directions (left, right, up, down), in that fixed order, and takes the first one that both
	 *  clears every marker AND stays inside the rect -- all four probe the same distance
	 *  (ObjectiveClearanceRadius) from TruePosition, so "shortest" is a tie among all four whenever more
	 *  than one clears, and resolving that tie by a fixed order keeps the locator from jittering
	 *  between two equally-valid sides frame to frame. Falls back to stepping directly away from the
	 *  single nearest marker, clamped into the rect, when none of the four axis probes qualifies. */
	FVector2D ResolveClearPosition(const FVector2D& TruePosition, const TArray<FVector2D>& MarkerPositions,
		const FVector2D& RectMin, const FVector2D& RectMax) const;

	/** The objective marker layer, resolved lazily and cached weakly -- it is a separate PC-owned
	 *  top-level widget with no parent/child path to this one (see
	 *  AExtractionPlayerController::GetObjectiveMarkerLayer), and it may not exist yet the first few
	 *  ticks this widget is alive. Re-resolves on its own if the cached pointer ever goes stale. */
	UObjectiveMarkerLayer* ResolveObjectiveLayer();

	TWeakObjectPtr<UCompanionCommandComponent> CachedCommandComp;

	TWeakObjectPtr<UObjectiveMarkerLayer> CachedObjectiveLayer;

	FScreenMarkerViewContext ViewContext;

	/** Reused every tick by ApplyObjectiveAvoidance so the marker-position query costs no per-tick
	 *  allocation past the first few frames' growth. Reset (not emptied) before each fill. */
	TArray<FVector2D> ScratchMarkerPositions;

	FVector2D SmoothedPosition = FVector2D::ZeroVector;
	FVector2D LastEdgeDirection = FVector2D(0.f, 1.f);
	bool bHasSmoothedPosition = false;
	bool bPendingAppearEvent = false;

	/** The position actually rendered this frame -- SmoothedPosition plus AvoidanceOffset. Not itself
	 *  interpolated; see AvoidanceOffset for why. */
	FVector2D DisplayedPosition = FVector2D::ZeroVector;

	/** Eased separately from SmoothedPosition -- an OFFSET from the true projected point, not an
	 *  absolute position, and zero when nothing needs clearing. The true point is itself moving every
	 *  frame (camera turning, off-screen edge slide); easing an absolute displayed position toward an
	 *  absolute target that is itself in motion would stack a second lag on top of SmoothedPosition's
	 *  own, so a ping with nothing nearby to dodge would track a half-step behind where it used to.
	 *  Easing the offset instead means it settles at zero and DisplayedPosition == SmoothedPosition
	 *  exactly, frame for frame, whenever there is nothing to avoid. */
	FVector2D AvoidanceOffset = FVector2D::ZeroVector;

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

	/** Dedup baseline for EvaluatePlacedEdge -- see its declaration for why this must be a separate
	 *  latch from bLastAiming above rather than sharing it. Reset to false when the ping clears
	 *  (TrackLocation), so a re-ping of the same target right after clearing the previous one still
	 *  sees a false->true transition instead of a stale true carried over from the ping that just ended. */
	bool bLastPlaced = false;

	/** Below this, two tracked points count as the same target. Deliberately generous: the
	 *  candidate probe samples at 10Hz (UCompanionCommandComponent::PingCandidateInterval), so its
	 *  last reading can be up to ~100ms stale relative to a fresh IssuePing trace at the moment of
	 *  commit, and camera rotation during that window shifts a distant hit point by tens of
	 *  centimetres even when the player never moved off the same physical target. No actor identity
	 *  crosses this boundary (OnPingCandidateChanged carries a location only, deliberately, per the
	 *  bridge contract), so distance is the only continuity signal available. */
	static constexpr float TargetContinuityEpsilonCm = 50.f;
};
