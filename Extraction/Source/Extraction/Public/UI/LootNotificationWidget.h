// ULootNotificationWidget -- transient HUD toast for loot acquisitions (ammo pickups,
// container loot, keycards). Subscribes to UMissionInventorySubsystem::OnLootNotify;
// shows the latest message for DisplayDuration seconds, then hides. Fade/slide styling
// can be layered in the WBP animation — C++ only drives text + visibility.
//
// WBP must contain:
//   UTextBlock "MessageText" -- displays the notification line

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LootNotificationWidget.generated.h"

class UTextBlock;
class UMissionInventorySubsystem;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API ULootNotificationWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- Bound widget (designer wires in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;

	// --- Tuning ---

	/** Seconds the toast stays visible after the latest message. */
	UPROPERTY(EditAnywhere, Category = "Loot|Toast", meta = (ClampMin = "0.1"))
	float DisplayDuration = 2.5f;

	/** Hook for BP show styling (play fade-in animation etc). Fired on every new message. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot|Toast")
	void OnMessageShown();

private:
	UFUNCTION()
	void HandleLootNotify(const FText& Message);

	void HideNotification();

	TWeakObjectPtr<UMissionInventorySubsystem> CachedSubsystem;
	FTimerHandle HideTimerHandle;
};
