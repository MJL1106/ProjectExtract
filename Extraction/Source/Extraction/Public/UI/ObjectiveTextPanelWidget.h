// UObjectiveTextPanelWidget -- fixed screen-space objective list (anchored middle-right in the
// WBP). Shows one text line per active objective (primary first by registration order; optional
// objectives carry their own "Optional: " label prefix). Subscribes to
// UObjectiveSubsystem::OnObjectivesChanged and rebuilds the joined text on every change.
// Added to the player screen by AExtractionPlayerController.
//
// WBP must contain:
//   UTextBlock "ObjectiveListText" -- the multi-line objective list

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ObjectiveTextPanelWidget.generated.h"

class UTextBlock;
class UObjectiveSubsystem;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UObjectiveTextPanelWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ObjectiveListText;

private:
	UFUNCTION()
	void Rebuild();

	TWeakObjectPtr<UObjectiveSubsystem> CachedSubsystem;
};
