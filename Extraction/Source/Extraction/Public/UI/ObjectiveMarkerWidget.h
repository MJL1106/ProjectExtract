// UObjectiveMarkerWidget -- edge-clamped off-screen indicator for objectives. Shows ONLY when
// the objective is off-screen or behind the camera. While the world-space billboard is visible
// (projection valid and on-screen) this widget hides itself. Keeps wayfinding without the
// sprint-bob problem of the old screen-projection approach.
//
// WBP must contain:
//   UImage     "MarkerIcon"   -- directional arrow/icon
//   UTextBlock "DistanceText" -- hidden in edge mode (kept for layout compat)
//   UTextBlock "LabelText"    -- (optional) hidden in edge mode

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/ObjectiveSubsystem.h"
#include "ObjectiveMarkerWidget.generated.h"

class UImage;
class UTextBlock;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UObjectiveMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Called by the layer right after spawn. */
	void SetObjective(const FObjectiveMarker& InObjective);

	/** Exponential interpolation with the same result for equivalent elapsed time. */
	static FVector2D InterpolateScreenPosition(const FVector2D& Current, const FVector2D& Target,
		float DeltaTime, float Speed);

	/** Constrains a marker centre to the padded viewport rectangle. */
	static FVector2D ClampToViewport(const FVector2D& Position, const FVector2D& ViewportSize, float Padding);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- Bound widgets (designer wires in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> MarkerIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DistanceText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	// --- Tuning ---

	/** Minimum distance (px, DPI-scaled) the marker keeps from the viewport edge when clamped. */
	UPROPERTY(EditAnywhere, Category = "Objective|Marker", meta = (ClampMin = "0.0"))
	float EdgePadding = 60.f;

	/** Screen-space smoothing speed. Zero snaps directly to the projected target. */
	UPROPERTY(EditAnywhere, Category = "Objective|Marker", meta = (ClampMin = "0.0"))
	float InterpolationSpeed = 12.f;

	/** Fraction of screen inset from each edge -- if the projection lands inside this region the
	 *  marker is considered "on-screen" and this widget hides. Prevents flickering at edges. */
	UPROPERTY(EditAnywhere, Category = "Objective|Marker", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float OnScreenInsetFraction = 0.05f;

private:
	FObjectiveMarker Objective;
	FVector2D SmoothedScreenPosition = FVector2D::ZeroVector;
	bool bHasSmoothedScreenPosition = false;
	bool bLastPositionWasValidProjection = false;
};
