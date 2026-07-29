// UObjectiveMarkerLayer -- full-screen HUD layer that owns one UObjectiveMarkerWidget per
// active objective. Subscribes to UObjectiveSubsystem::OnObjectivesChanged and rebuilds its
// canvas children on every change. Added to the player screen by AExtractionPlayerController.
//
// WBP must contain:
//   UCanvasPanel "MarkerCanvas" -- full-screen canvas the marker widgets are spawned into

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ObjectiveMarkerLayer.generated.h"

class UCanvasPanel;
class UObjectiveMarkerWidget;
class UObjectiveSubsystem;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UObjectiveMarkerLayer : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- Bound widget (designer wires in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> MarkerCanvas;

	/** Per-marker widget class (WBP_ObjectiveMarker) — assigned on the WBP layer subclass. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective")
	TSubclassOf<UObjectiveMarkerWidget> MarkerWidgetClass;

private:
	UFUNCTION()
	void RebuildMarkers();

	/** Targeted label update — avoids the full ClearChildren + CreateWidget rebuild, preserving
	 *  each marker widget's per-tick RenderTranslation smoothing state. */
	UFUNCTION()
	void HandleLabelChanged(FName Id, const FText& NewLabel);

	TWeakObjectPtr<UObjectiveSubsystem> CachedSubsystem;
};
