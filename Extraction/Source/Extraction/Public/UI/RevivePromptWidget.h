// URevivePromptWidget -- the player's hold-prompt HUD. Serves two sources: a downed teammate in
// revive range ("[E] Revive"), and an IWorldInteractable under the crosshair (its own prompt
// text, e.g. "Rescue"). Fills a progress bar during either hold. Polls the owning pawn each
// tick (no delegate plumbing), same self-managed pattern as the other HUD widgets.
//
// Revive wins when both are live -- a downed teammate is always the more urgent read.
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
	FText ReviveHint = NSLOCTEXT("RevivePrompt", "Revive", "[E] Revive");

	UPROPERTY(EditAnywhere, Category = "Revive|Hints")
	FText RevivingHint = NSLOCTEXT("RevivePrompt", "Reviving", "Reviving...");

	/** Wraps the interactable's own prompt text as "[E] {0}" — the actor supplies the verb
	 *  ("Rescue"), the widget supplies the key. */
	UPROPERTY(EditAnywhere, Category = "Interaction|Hints")
	FText InteractHintFormat = NSLOCTEXT("RevivePrompt", "InteractFormat", "[E] {0}");

	/** Shown while a hold-to-interact is filling. Empty = keep showing the wrapped prompt. */
	UPROPERTY(EditAnywhere, Category = "Interaction|Hints")
	FText InteractHoldingHint = NSLOCTEXT("RevivePrompt", "InteractHolding", "Hold E...");
};
