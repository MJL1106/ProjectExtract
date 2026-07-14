// URevivePromptWidget -- "[E] Revive" hint shown while the local player looks at a downed
// teammate in revive range; fills a progress bar during the hold. Polls the owning pawn each
// tick (no delegate plumbing), same self-managed pattern as the other HUD widgets.
//
// WBP must contain:
//   UPanelWidget  "PromptContainer"  -- root of the hint; toggled visible/collapsed
//   UTextBlock    "PromptText"       -- displays ReviveHint text
//   UProgressBar  "HoldProgressBar"  -- (optional) 0-1 fill during the hold

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RevivePromptWidget.generated.h"

class UPanelWidget;
class UProgressBar;
class UTextBlock;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API URevivePromptWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- Bound widgets (designer wires these in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> PromptContainer;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PromptText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HoldProgressBar;

	// --- Designer-editable labels ---

	UPROPERTY(EditAnywhere, Category = "Revive|Hints")
	FText ReviveHint = NSLOCTEXT("RevivePrompt", "Revive", "[E] Revive");

	UPROPERTY(EditAnywhere, Category = "Revive|Hints")
	FText RevivingHint = NSLOCTEXT("RevivePrompt", "Reviving", "Reviving...");
};
