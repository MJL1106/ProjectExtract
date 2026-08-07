// UPickupToastRowWidget -- one row inside UPickupToastStackWidget's toast stack. Renders a single
// acquisition (category / item name / signed quantity / icon) and owns its own dwell + exit timing;
// the stack decides WHICH entry a row shows and WHEN a row is free to reuse, this widget decides
// HOW LONG it stays on screen once shown.
//
// Timing is split C++/BP on purpose: the stack calls ShowEntry/AddQuantity/HideImmediately and C++
// drives the dwell/exit timers, but the actual slide/fade motion is BP animation (OnToastEnter /
// OnToastExit) so a designer can retime the motion without touching C++. ExitDuration exists so C++
// waits out the WBP's own exit animation before collapsing -- collapsing the instant OnToastExit
// fires would cut that animation off mid-play.
//
// THE ROOT RENDER TRANSFORM IS FREE FOR THE DESIGNER'S ANIMATION. C++ never writes it. Vertical
// stacking position is owned entirely by UPickupToastStackWidget, through this row's CANVAS SLOT
// (UCanvasPanelSlot::SetPosition) -- a channel a root Render Transform -> Translation animation
// composes on top of rather than fights with, unlike the render transform itself. Author the
// ~120ms slide-in-from-right and ~180ms fade/slide-out as a root Render Transform track exactly as
// you would on any other widget; C++ repositioning the row (on a new arrival, or on any row's own
// retirement -- see OwningStack below) will never overwrite it or hold it at a stale value.
//
// WBP must contain:
//   UTextBlock "CategoryText"  -- e.g. "RIFLE AMMUNITION"
//   UTextBlock "ItemNameText"  -- e.g. "5.56 NATO"
//   UTextBlock "QuantityText"  -- e.g. "+60"
//   UImage     "ItemIcon"
// WBP may optionally contain:
//   UImage     "AccentBar"     -- category accent strip; C++ never touches its colour, purely a
//                                WBP-side hook

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PickupToastRowWidget.generated.h"

class UImage;
class UPickupToastStackWidget;
class UTextBlock;
class UTexture2D;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UPickupToastRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Populates a freshly-claimed row and starts its dwell. Called by the stack only -- a
	 *  Blueprint never calls this directly, it reacts to OnToastEnter/OnToastExit instead. Resolves
	 *  and caches OwningStack (via GetTypedOuter -- CreateWidget always parents a row to the stack
	 *  that created it) -- the back-channel FinishExit/HideImmediately use to report this row
	 *  retiring, since a canvas's children are positionally independent and nothing else would ever
	 *  tell the stack to close the gap this row leaves behind. */
	UFUNCTION(BlueprintCallable, Category = "UI|PickupToast")
	void ShowEntry(FName Key, const FText& Category, const FText& ItemName, int32 Quantity, UTexture2D* Icon);

	/** Adds to the running total, rewrites QuantityText, and restarts the dwell -- a repeated
	 *  pickup while the row is already on screen keeps it up for another full dwell rather than
	 *  letting it expire mid-readthrough. Does NOT replay OnToastEnter: the row is already in
	 *  place, and re-sliding it in would read as a second, separate pickup rather than an update to
	 *  this one. */
	UFUNCTION(BlueprintCallable, Category = "UI|PickupToast")
	void AddQuantity(int32 Extra);

	UFUNCTION(BlueprintPure, Category = "UI|PickupToast")
	FName GetConsolidationKey() const { return RowConsolidationKey; }

	/** True from ShowEntry until the row has fully exited and collapsed. The stack's consolidation
	 *  scan and its search for a reusable row both read this -- it is the ONLY source of truth for
	 *  whether a slot is really free, since a row's own dwell/exit timers can retire it with no
	 *  push notification back to the stack. */
	UFUNCTION(BlueprintPure, Category = "UI|PickupToast")
	bool IsActive() const { return bActive; }

	/** Skips straight to collapsed with no exit animation. Used when the stack retires the oldest
	 *  row to make room for a new arrival -- that eviction has no dwell left to finish honestly.
	 *  Also reports retirement through OwningStack, same as a natural expiry -- see ShowEntry. */
	UFUNCTION(BlueprintCallable, Category = "UI|PickupToast")
	void HideImmediately();

protected:
	virtual void NativeDestruct() override;

	// --- Bound widgets (designer wires in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CategoryText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> QuantityText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIcon;

	/** Optional category accent strip -- purely a WBP visual hook, C++ never touches its colour. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> AccentBar;

	// --- Tuning ---

	/** Seconds the row stays visible (before OnToastExit fires) once shown or last bumped by
	 *  AddQuantity. */
	UPROPERTY(EditAnywhere, Category = "UI|PickupToast", meta = (ClampMin = "0.1"))
	float DwellDuration = 2.2f;

	/** How long C++ waits after OnToastExit before collapsing and freeing the row -- long enough
	 *  for the WBP's own exit animation (~180ms) to finish before the row disappears under it. */
	UPROPERTY(EditAnywhere, Category = "UI|PickupToast", meta = (ClampMin = "0.0"))
	float ExitDuration = 0.18f;

	/** WBP plays the ~120ms slide-in-from-right. Fired once per NEW entry claimed via ShowEntry --
	 *  never on AddQuantity, see its comment. */
	UFUNCTION(BlueprintImplementableEvent, Category = "UI|PickupToast")
	void OnToastEnter();

	/** WBP plays the ~180ms fade/slide-out. C++ waits ExitDuration after this fires before
	 *  collapsing the row. */
	UFUNCTION(BlueprintImplementableEvent, Category = "UI|PickupToast")
	void OnToastExit();

private:
	void RestartDwellTimer();
	void BeginExit();
	void FinishExit();
	void RefreshQuantityText();

	/** Shared tail of FinishExit/HideImmediately -- reports retirement to OwningStack so it can
	 *  prune this row out of VisibleOrder and reposition whatever is left. Safe when OwningStack
	 *  never resolved or has since gone (a torn-down stack): both are silent no-ops via IsValid. */
	void NotifyStackOfRetirement();

	/** SetTimer never fires at a rate of zero; ClampMin only constrains the editor field, so a
	 *  value pushed in from code still needs this floor. */
	static constexpr float MinDwellDuration = 0.05f;

	FTimerHandle DwellTimerHandle;
	FTimerHandle ExitTimerHandle;

	/** Keys this row against the stack's consolidation map -- see UPickupToastStackWidget for how
	 *  it's built. NAME_None while the row is free/pooled. */
	FName RowConsolidationKey = NAME_None;

	int32 RunningQuantity = 0;

	/** True from ShowEntry to the row fully collapsing -- see IsActive(). */
	bool bActive = false;

	/** Resolved once in ShowEntry -- see that function's comment. Weak: this row does not own the
	 *  stack, and a torn-down stack must read back as gone rather than as a dangling call target. */
	TWeakObjectPtr<UPickupToastStackWidget> OwningStack;
};
