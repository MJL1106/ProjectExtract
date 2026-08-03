// HUD widget -- two consumable slots (stim + grenade) with item icons,
// count badges, and keybind labels.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ConsumableWidget.generated.h"

class UImage;
class UTextBlock;
class UConsumableInventoryComponent;

/**
 * Displays two consumable slots on the HUD: stim and grenade.
 * Each slot has an item icon, a count badge, and a keybind label.
 *
 * Requires a Widget Blueprint with:
 *   - UImage  named "StimIcon"          (item visual inside the stim box)
 *   - UTextBlock named "StimCountText"  (count badge at bottom-left of stim box)
 *   - UTextBlock named "StimKeyText"    (keybind label under the stim box)
 *   - UImage  named "GrenadeIcon"       (item visual inside the grenade box)
 *   - UTextBlock named "GrenadeCountText" (count badge at bottom-left of grenade box)
 *   - UTextBlock named "GrenadeKeyText" (keybind label under the grenade box)
 */
UCLASS()
class EXTRACTION_API UConsumableWidget : public UUserWidget
{
	GENERATED_BODY()

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ---- BindWidget properties ----
	// Icons and key labels are optional -- the widget degrades gracefully if missing.
	// Count texts are mandatory because the widget is pointless without them.

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> StimIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StimCountText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StimKeyText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> GrenadeIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GrenadeCountText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> GrenadeKeyText;

	// ---- Designer-assigned keybind labels (data-driven, not hardcoded) ----

	UPROPERTY(EditDefaultsOnly, Category = "Consumables|Keys")
	FText StimKeyLabel = NSLOCTEXT("ConsumableWidget", "StimKey", "4");

	UPROPERTY(EditDefaultsOnly, Category = "Consumables|Keys")
	FText GrenadeKeyLabel = NSLOCTEXT("ConsumableWidget", "GrenadeKey", "3");

	// ---- Blueprint events for visual states ----

	/** Called when the stim count changes. Blueprint should update visual "empty" state
	 *  (e.g. greying out the icon, dimming the box) based on the new count. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Consumables|Events")
	void OnStimCountUpdated(int32 NewCount);

	/** Called when the grenade count changes. Blueprint should update visual "empty" state. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Consumables|Events")
	void OnGrenadeCountUpdated(int32 NewCount);

	/** Blueprint supplies the current grenade count by reading the kit chain.
	 *  C++ calls this on a timer since no C++ delegate exists for grenade count changes.
	 *  Return -1 if the count cannot be determined (e.g. no grenade actor exists). */
	UFUNCTION(BlueprintNativeEvent, Category = "Consumables|Events")
	int32 GetCurrentGrenadeCount() const;

private:

	bool BindConsumableComponent();
	void InitializePlayerBinding();
	void RetryBindConsumableComponent();

	UFUNCTION()
	void OnPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void OnStimCountChanged(int32 NewStimCount);

	void PollGrenadeCount();
	void UpdateStimDisplay(int32 NewCount);
	void UpdateGrenadeDisplay(int32 NewCount);

	TWeakObjectPtr<UConsumableInventoryComponent> CachedConsumableComponent;

	FTimerHandle GrenadePollingTimerHandle;
	FTimerHandle BindRetryTimerHandle;

	/** Cached grenade count for change-detection -- only update UI when value differs. */
	int32 LastKnownGrenadeCount = -1;

	/** Remaining bind-retry attempts before the timer gives up. */
	int32 BindRetryAttemptsRemaining = 0;
};
