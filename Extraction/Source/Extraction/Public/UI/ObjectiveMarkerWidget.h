// UObjectiveMarkerWidget -- one on-screen objective waypoint: icon with distance-in-metres
// underneath (and an optional label). Projects the objective's world location to screen each
// tick (UPingMarkerWidget's pattern) and, unlike the ping marker, EDGE-CLAMPS when the
// objective is off-screen or behind the camera so the player always knows which way to turn.
// Spawned and positioned by UObjectiveMarkerLayer — not added to the screen directly.
//
// WBP must contain:
//   UImage     "MarkerIcon"   -- waypoint icon
//   UTextBlock "DistanceText" -- distance line, e.g. "42 m"
//   UTextBlock "LabelText"    -- (optional) objective label

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

	/** Vertical offset (px) applied while the objective is on screen — floats the icon above the spot. */
	UPROPERTY(EditAnywhere, Category = "Objective|Marker")
	float VerticalScreenOffset = -30.f;

private:
	/** Objective being tracked (copy — resolves a moving TargetActor each tick). */
	FObjectiveMarker Objective;

	void UpdateDistanceText(const FVector& WorldLocation);
};
