// UObjectiveMarkerWidget -- screen-space tracker for one objective. It owns no visuals: it projects
// the objective's world position, decides on-screen vs edge-clamped, smooths the result, and hands
// the WBP everything it needs (off-screen flag, chevron angle, distance, objective state) as
// BlueprintReadOnly state plus events. Styling lives entirely in the WBP.
//
// Positioning is ABSOLUTE player-screen space -- this widget is a child of the UObjectiveMarkerLayer
// canvas, NOT a HUD module. Re-parenting it under anything that applies its own anchor/offset (or a
// second HUD scale) displaces the projection twice.
//
// Chevron convention: EdgeAngleDegrees is 0 when the target lies to the RIGHT of screen centre and
// increases clockwise. Author the chevron pointing right at zero rotation and feed the angle into
// the CHEVRON image's render transform -- rotating the widget root would spin the label with it.
//
// WBP contract (all optional -- bind only what the design uses):
//   UImage     "MarkerIcon"
//   UTextBlock "DistanceText" -- auto-filled with "<n> m" when bound
//   UTextBlock "LabelText"    -- auto-filled with the objective label, collapsed when empty

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/ObjectiveSubsystem.h"
#include "UI/ScreenMarkerProjection.h"
#include "ObjectiveMarkerWidget.generated.h"

class UImage;
class UObjectiveMarkerLayer;
class UTextBlock;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UObjectiveMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Called by the layer once, right after spawn. Resets tracking and arms the appear pulse. */
	void SetObjective(const FObjectiveMarker& InObjective, UObjectiveMarkerLayer* InLayer);

	/** In-place data rewrite for a marker that is already live (the objective moved, re-targeted, or
	 *  changed flags). Deliberately keeps the smoothed position and does NOT replay the pulse. */
	void RefreshObjective(const FObjectiveMarker& InObjective);

	/** Narrow update paths. The layer routes the subsystem's label/state deltas here so one
	 *  objective changing never costs the whole marker set its interpolation continuity. */
	void UpdateLabel(const FText& NewLabel);
	void UpdateState(EObjectiveState NewState);

	FName GetObjectiveId() const { return Objective.Id; }

	UFUNCTION(BlueprintPure, Category = "Objective|Marker")
	FText GetObjectiveLabel() const { return Objective.Label; }

	// --- Per-frame state the WBP reads ---

	/** True while the target is behind the camera or outside the padded viewport rectangle. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective|Marker")
	bool bIsOffScreen = false;

	/** Chevron rotation in degrees, [0, 360). Only meaningful while bIsOffScreen. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective|Marker")
	float EdgeAngleDegrees = 0.f;

	/** Straight-line distance from the camera to the objective, in metres. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective|Marker")
	float DistanceMeters = 0.f;

	/** Objective lifecycle -- lets completed/failed markers restyle without asking the subsystem. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective|Marker")
	EObjectiveState State = EObjectiveState::Tracked;

	/** Distance-driven scale this widget resolved for the current frame. Already applied to the
	 *  render transform unless bDriveRenderTransform is off. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective|Marker")
	float MarkerScale = 1.f;

	/** Distance-driven opacity for the current frame. Applied unless bDriveRenderTransform is off. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective|Marker")
	float MarkerOpacity = 1.f;

	// --- Events the WBP implements ---

	/** Fired once, on the first frame this marker resolves a valid position. Play the intro pulse
	 *  here. Not replayed when the objective's data is rewritten or when it re-enters the screen. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Objective|Marker")
	void OnMarkerAppeared();

	/** Fired whenever a tracked value actually moves -- drive chevron rotation, distance text and
	 *  fades from here instead of ticking the WBP. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Objective|Marker")
	void OnMarkerStateUpdated(bool bOffScreen, float EdgeAngle, float Distance);

	/** Fired on setup and on every genuine Tracked -> Succeeded/Failed transition. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Objective|Marker")
	void OnObjectiveStateUpdated(EObjectiveState NewState);

	/** Fired on setup and on every label rewrite (a defend beat re-labels itself once a second). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Objective|Marker")
	void OnObjectiveLabelUpdated(const FText& NewLabel);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- Bound widgets (designer wires in WBP; all optional) ---

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> MarkerIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DistanceText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	// --- Tuning: clamping ---

	/** Minimum gap (DPI-scaled px) the marker keeps from every viewport edge, on-screen and off.
	 *  Sized so a marker never sits half off the screen or under the corner HUD elements. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Layout", meta = (ClampMin = "0.0"))
	float EdgeMargin = 64.f;

	/** Extra inset the target must clear before an off-screen marker flips back to on-screen. Capped:
	 *  once it approaches half the smaller viewport dimension the on-screen test rect floors out and
	 *  an off-screen marker can never come back. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Layout", meta = (ClampMin = "0.0", ClampMax = "128.0"))
	float ReentryHysteresis = 16.f;

	/** Off-screen edge-slide smoothing speed, and the interp rate for the distance scale/fade.
	 *  Higher tracks tighter; 0 snaps every frame. Does NOT apply on-screen: there the marker always
	 *  snaps to the exact projected point, because smoothing it lags the world during a turn. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Layout", meta = (ClampMin = "0.0"))
	float PositionInterpSpeed = 14.f;

	/** Collapses the marker to zero opacity while the target is on-screen. Turn this on only when
	 *  the world-space billboard (AObjectiveMarkerDisplay) is doing the on-screen job. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Layout")
	bool bHideWhenOnScreen = false;

	// --- Tuning: distance response ---

	/** At or below this distance (m) the marker draws at MaxMarkerScale. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Distance", meta = (ClampMin = "0.0"))
	float ScaleNearDistanceMeters = 8.f;

	/** At or beyond this distance (m) the marker draws at MinMarkerScale. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Distance", meta = (ClampMin = "0.0"))
	float ScaleFarDistanceMeters = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Distance", meta = (ClampMin = "0.01"))
	float MaxMarkerScale = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Distance", meta = (ClampMin = "0.01"))
	float MinMarkerScale = 0.65f;

	/** Distance (m) at which the marker starts fading toward MinMarkerOpacity. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Distance", meta = (ClampMin = "0.0"))
	float FadeStartDistanceMeters = 60.f;

	/** Distance (m) at which the fade has fully reached MinMarkerOpacity. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Distance", meta = (ClampMin = "0.0"))
	float FadeEndDistanceMeters = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Distance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinMarkerOpacity = 0.35f;

	/** Scale and fade are a proximity read, so they apply on-screen only -- an off-screen chevron
	 *  200 m out still has to be legible. Turn off to let distance drive both states. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Distance")
	bool bDistanceVisualsOnScreenOnly = true;

	/** When false this widget writes only its render TRANSLATION and leaves scale and opacity to the
	 *  WBP. Turn it off if a UMG animation on the widget root drives either -- C++ writing them
	 *  every tick would fight the animation. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Marker|Distance")
	bool bDriveRenderTransform = true;

private:
	/** The frame's shared camera/viewport data, from the owning layer when there is one. Null when
	 *  nothing usable could be resolved -- the caller must skip the frame rather than move. */
	const FScreenMarkerViewContext* ResolveViewContext();

	void ApplyProjection(const FScreenMarkerProjection& Projection, float DeltaTime);
	void ApplyDistanceVisuals(float DeltaTime, bool bSnap);
	void PushLabelToWidget();
	void PushDistanceToWidget();

	FObjectiveMarker Objective;

	TWeakObjectPtr<UObjectiveMarkerLayer> OwningLayer;

	/** Only used when this widget has no layer (standalone placement) -- otherwise the layer's
	 *  once-per-frame context is shared instead. */
	FScreenMarkerViewContext LocalViewContext;

	FVector2D SmoothedPosition = FVector2D::ZeroVector;
	FVector2D LastEdgeDirection = FVector2D(0.f, 1.f);
	bool bHasSmoothedPosition = false;

	/** Set by SetObjective, cleared by the first frame that resolves a position -- gates the pulse. */
	bool bPendingAppearEvent = false;

	/** Last values pushed to OnMarkerStateUpdated, so a stationary marker costs no BP calls. */
	bool bLastBroadcastOffScreen = false;
	float LastBroadcastAngle = 0.f;
	float LastBroadcastDistance = 0.f;
	bool bHasBroadcastState = false;

	/** Last whole-metre value written to DistanceText -- the text only changes ~once per metre. */
	int32 LastDisplayedMetres = -1;
};
