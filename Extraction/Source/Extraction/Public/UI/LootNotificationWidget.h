// ULootNotificationWidget -- transient HUD toast for two channels:
//   OnLootNotify  -- plain acquisitions (ammo pickups, container loot, keycards), Info severity.
//   OnToastNotify -- severity-carrying messages that need emphasis (stealth discipline etc).
// Both funnel into ShowToast, which shows the latest message for a severity-dependent
// duration, tints text (and Backing, if present) toward the severity colour, then hides.
// Fade/slide styling can be layered in the WBP animation -- C++ only drives text, tint,
// visibility and the optional sting sound.
//
// The HUD's pickup toast stack (UPickupToastStackWidget) now renders every successful acquisition
// off OnLootGranted too, so an OnLootNotify message that PAIRS with a grant (the subsystem raises
// both, same FText, for every ammo/stim/keycard grant) must NOT also render here or the player sees
// the same pickup twice. This widget subscribes to OnLootGranted purely to record which texts were
// granted this frame -- see FlushPendingLootMessages for why that dedup has to happen at an
// end-of-frame flush rather than on arrival. Refusals ("Stims full", "no compatible weapon"),
// door/lift feedback and objective toasts have no matching grant and always render normally.
//
// WBP must contain:
//   UTextBlock "MessageText" -- displays the notification line
// WBP may optionally contain:
//   UBorder "Backing" -- wraps the text; tinted toward the severity colour when present

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/MissionInventorySubsystem.h"
#include "LootNotificationWidget.generated.h"

class UTextBlock;
class UBorder;
class UMissionInventorySubsystem;
class USoundBase;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API ULootNotificationWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- Bound widgets (designer wires in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;

	/** Optional -- tinted toward the severity colour when present. WBP without one still works. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Backing;

	// --- Tuning ---

	/** Seconds the toast stays visible after the latest Info message. */
	UPROPERTY(EditAnywhere, Category = "Loot|Toast", meta = (ClampMin = "0.1"))
	float DisplayDuration = 2.5f;

	/** Seconds the toast stays visible after the latest Warning message. */
	UPROPERTY(EditAnywhere, Category = "Loot|Toast", meta = (ClampMin = "0.1"))
	float WarningDisplayDuration = 4.f;

	/** Seconds the toast stays visible after the latest Alert message. */
	UPROPERTY(EditAnywhere, Category = "Loot|Toast", meta = (ClampMin = "0.1"))
	float AlertDisplayDuration = 6.f;

	// --- Style ---

	// Info deliberately has no designer-facing colour here: it renders with whatever the WBP
	// already authored on MessageText/Backing (captured in NativeConstruct), so existing loot
	// toasts stay byte-identical. Only Warning/Alert get an explicit override colour.

	UPROPERTY(EditAnywhere, Category = "Loot|Toast|Style")
	FLinearColor WarningColor = FLinearColor(1.f, 0.65f, 0.1f);

	UPROPERTY(EditAnywhere, Category = "Loot|Toast|Style")
	FLinearColor AlertColor = FLinearColor(1.f, 0.15f, 0.1f);

	/** Designer-assigned in the WBP -- C++ never carries /Game/ paths. Null plays nothing. */
	UPROPERTY(EditAnywhere, Category = "Loot|Toast|Style")
	TObjectPtr<USoundBase> WarningSound;

	/** Designer-assigned in the WBP -- C++ never carries /Game/ paths. Null plays nothing. */
	UPROPERTY(EditAnywhere, Category = "Loot|Toast|Style")
	TObjectPtr<USoundBase> AlertSound;

	/** Hook for BP show styling (play fade-in animation etc). Fired on every new message. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot|Toast")
	void OnMessageShown(EToastSeverity Severity);

private:
	UFUNCTION()
	void HandleLootNotify(const FText& Message);

	UFUNCTION()
	void HandleToastNotify(const FText& Message, EToastSeverity Severity);

	/** Records this frame's granted item text; the flush below is what actually dedups against it.
	 *  Only Label matters -- Type/Amount/AmmoCategory ride along to match the delegate signature. */
	UFUNCTION()
	void HandleLootGranted(ELootType Type, int32 Amount, const FText& Label, EEnemyWeaponAnimType AmmoCategory);

	void ShowToast(const FText& Message, EToastSeverity Severity);
	void HideNotification();

	/** Shows every queued OnLootNotify message that did NOT show up in GrantedTextsThisFrame, then
	 *  clears both arrays. Deferred to next tick rather than shown from HandleLootNotify directly:
	 *  the subsystem raises OnLootNotify FIRST and OnLootGranted immediately after with the SAME
	 *  FText (see UExtractionHudBridgeComponent::FlushPendingLootNotifies for the same fact), so an
	 *  on-arrival filter can never see the grant that would identify it as a duplicate. Checking
	 *  membership here rather than removing-on-arrival makes the dedup ordering-independent by
	 *  construction: both arrays are already fully populated for the frame by the time this runs,
	 *  regardless of which of the two broadcasts happened to fire first, so a future edit that
	 *  swaps their order in the subsystem cannot silently reinstate the double render. */
	void FlushPendingLootMessages();

	float GetDisplayDuration(EToastSeverity Severity) const;
	FSlateColor GetSeverityTextColor(EToastSeverity Severity) const;
	FLinearColor GetSeverityBackingColor(EToastSeverity Severity) const;
	USoundBase* GetSeveritySound(EToastSeverity Severity) const;

	TWeakObjectPtr<UMissionInventorySubsystem> CachedSubsystem;
	FTimerHandle HideTimerHandle;

	/** OnLootNotify messages queued this frame, awaiting FlushPendingLootMessages. */
	TArray<FText> PendingLootMessages;

	/** OnLootGranted item texts recorded this frame -- cross-referenced against
	 *  PendingLootMessages at flush time, then reset every flush alongside it. */
	TArray<FText> GrantedTextsThisFrame;

	/** Next-tick flush timer. IsTimerActive on this is the schedule-once guard, so N loot messages
	 *  in one frame (a looted container) cost one flush rather than N. */
	FTimerHandle PendingFlushTimerHandle;

	/** Info restore target, captured once in NativeConstruct before any toast can touch the
	 *  widgets -- so an Info toast that arrives after a Warning/Alert renders exactly as the
	 *  WBP authored it, not some hardcoded default. */
	FSlateColor AuthoredTextColor;
	FLinearColor AuthoredBackingColor = FLinearColor::White;
	bool bCapturedAuthoredStyle = false;

	/** Alert holds priority over a live toast: a lower-severity message arriving while a
	 *  higher one is still showing is dropped rather than truncating/repainting it. Reset to
	 *  Info when the toast hides so the next message always gets a fair shot. */
	EToastSeverity ActiveSeverity = EToastSeverity::Info;
};
