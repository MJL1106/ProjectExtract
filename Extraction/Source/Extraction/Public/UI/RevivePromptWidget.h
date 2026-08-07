// URevivePromptWidget -- the player's revive hold-prompt ("[F] Revive"), filling a progress bar
// during the hold. Polls the owning pawn each tick (no delegate plumbing), same self-managed
// pattern as the other legacy HUD widgets.
//
// SCOPE: revive ONLY. It used to serve IWorldInteractable prompts as well; the HUD's own prompt
// container owns those now, and this widget kept drawing a second copy of the same hint. This
// class survives purely as the fallback until the HUD module's revive label is proven in play.
//
// WBP must contain:
//   UPanelWidget  "PromptContainer"  -- root of the hint; toggled visible/collapsed
//   UTextBlock    "PromptText"       -- displays the active source's prompt text
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
	FText ReviveHint = NSLOCTEXT("RevivePrompt", "Revive", "[F] Revive");

	UPROPERTY(EditAnywhere, Category = "Revive|Hints")
	FText RevivingHint = NSLOCTEXT("RevivePrompt", "Reviving", "Reviving...");

	// INERT: kept only so the WBP's authored values are not lost while this class waits to be
	// retired. Nothing reads them since the interact branch moved to the HUD prompt container.

	UPROPERTY(EditAnywhere, Category = "Interaction|Hints")
	FText InteractHintFormat = NSLOCTEXT("RevivePrompt", "InteractFormat", "[F] {0}");

	UPROPERTY(EditAnywhere, Category = "Interaction|Hints")
	FText InteractHoldingHint = NSLOCTEXT("RevivePrompt", "InteractHolding", "Hold F...");
};
