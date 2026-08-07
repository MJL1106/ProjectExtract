// Shared screen-space projection maths for HUD markers that track a world position (objective
// markers, companion pings). It lives here rather than on any one widget because the behind-camera
// and edge-clamp cases are the easy things to get subtly wrong twice.
//
// Everything is expressed in DPI-SCALED SLATE UNITS -- the space UWidget::SetRenderTranslation
// works in -- never raw viewport pixels.

#pragma once

#include "CoreMinimal.h"

class APlayerController;
class UUserWidget;

/** Camera + viewport state for one frame. Built once and shared by every marker that frame:
 *  re-resolving the player controller, the player screen geometry and the DPI scale per marker is
 *  pure waste as soon as more than one marker is on screen. */
struct EXTRACTION_API FScreenMarkerViewContext
{
	TWeakObjectPtr<APlayerController> PlayerController;

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;

	/** Player screen size in DPI-scaled slate units. */
	FVector2D ViewportSize = FVector2D::ZeroVector;

	/** Viewport pixels per slate unit. */
	float DpiScale = 1.f;

	bool IsValid() const;

	/** Resolves the widget's owning player view point, screen size and DPI scale. False when any of
	 *  them is unavailable this frame (no controller yet, viewport not laid out yet). */
	static bool Build(const UUserWidget* Widget, FScreenMarkerViewContext& OutContext);
};

/** Inputs that shape the clamp, grouped so the projection signature stays readable. */
struct EXTRACTION_API FScreenMarkerClampParams
{
	/** Minimum gap the marker keeps from every viewport edge, on-screen and off. */
	float EdgeMargin = 64.f;

	/** Extra inset a target must clear before an off-screen marker is allowed back on-screen.
	 *  Without it a target parked exactly on the boundary toggles state every single frame. */
	float ReentryHysteresis = 16.f;

	/** Last frame's answer -- drives the hysteresis above. */
	bool bWasOffScreen = false;
};

/** One frame's answer for one marker. */
struct EXTRACTION_API FScreenMarkerProjection
{
	/** Marker centre in slate units, already constrained to the padded viewport rectangle. */
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	/** Unit direction from screen centre toward the target (+X right, +Y down). */
	FVector2D EdgeDirection = FVector2D(0.f, 1.f);

	/** EdgeDirection as degrees in [0, 360): 0 = the target lies to the right of screen centre,
	 *  increasing clockwise. Feeds a chevron image's render transform angle directly. */
	float EdgeAngleDegrees = 0.f;

	float DistanceMeters = 0.f;

	bool bIsOffScreen = false;

	/** True when the target sits behind the camera plane. */
	bool bIsBehind = false;

	/** False when the context was unusable -- callers must skip the frame rather than move. */
	bool bIsValid = false;

	static FScreenMarkerProjection Project(const FScreenMarkerViewContext& Context,
		const FVector& WorldLocation, const FScreenMarkerClampParams& Params);

	/** Unit screen-space bearing from viewport centre toward a target expressed in camera space
	 *  (+X forward, +Y right, +Z up). Deliberately ignores X: the bearing is identical for a target
	 *  in front and its mirror behind, which is what keeps the edge marker on the correct side as
	 *  the target crosses the camera plane. Falls back to straight down for a degenerate input.
	 *  Split out from Project so it is testable without a player controller. */
	static FVector2D BearingFromCameraSpace(const FVector& CameraSpaceOffset);

	/** Where a ray leaving the viewport centre along Bearing meets the viewport rectangle inset by
	 *  Margin on every side. Rectangle, not circle -- the marker has to slide along the edge the
	 *  target left through instead of orbiting the centre. */
	static FVector2D EdgeIntersection(const FVector2D& Bearing, const FVector2D& ViewportSize, float Margin);

	/** Frame-rate independent exponential smoothing: equivalent elapsed time gives an equivalent
	 *  result whatever the frame budget did. Speed <= 0 snaps. */
	static FVector2D InterpolatePosition(const FVector2D& Current, const FVector2D& Target,
		float DeltaTime, float Speed);
};
