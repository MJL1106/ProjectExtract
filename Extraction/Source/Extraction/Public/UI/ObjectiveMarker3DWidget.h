// UObjectiveMarker3DWidget -- C++ base for the world-space objective marker widget rendered by
// AObjectiveMarkerDisplay's WidgetComponent. No projection logic, no NativeTick -- the display
// actor drives position/scale and pushes distance/label updates.
//
// WBP must contain:
//   UImage     "MarkerIcon"   -- waypoint icon
//   UTextBlock "DistanceText" -- distance line, e.g. "42 m"
//   UTextBlock "LabelText"    -- (optional) objective label

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ObjectiveMarker3DWidget.generated.h"

class UImage;
class UTextBlock;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UObjectiveMarker3DWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Set the objective label. Empty text collapses the label block. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void SetLabel(const FText& InLabel);

	/** Set the displayed distance string. Called only when the rounded value changes. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void SetDistance(int32 DistanceMetres);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> MarkerIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DistanceText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;
};
