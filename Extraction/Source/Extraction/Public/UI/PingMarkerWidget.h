// UPingMarkerWidget -- small world-anchored marker that tracks the pinged target on screen. Shares
// UObjectiveMarkerWidget's projection maths (FScreenMarkerProjection): on-screen it sits on the
// target, off-screen or behind the camera it slides along the padded viewport edge with a chevron
// angle for the WBP to point at. Collapsed while no ping is active.
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

	/** Resolved world position of whatever this marker is tracking, or false when nothing is. */
	bool GetTrackedLocation(FVector& OutLocation) const;

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
};
