// ULootNotificationWidget -- transient HUD toast for two channels:
//   OnLootNotify  -- plain acquisitions (ammo pickups, container loot, keycards), Info severity.
//   OnToastNotify -- severity-carrying messages that need emphasis (stealth discipline etc).
// Both funnel into ShowToast, which shows the latest message for a severity-dependent
// duration, tints text (and Backing, if present) toward the severity colour, then hides.
// Fade/slide styling can be layered in the WBP animation -- C++ only drives text, tint,
// visibility and the optional sting sound.
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

	void ShowToast(const FText& Message, EToastSeverity Severity);
	void HideNotification();

	float GetDisplayDuration(EToastSeverity Severity) const;
	FSlateColor GetSeverityTextColor(EToastSeverity Severity) const;
	FLinearColor GetSeverityBackingColor(EToastSeverity Severity) const;
	USoundBase* GetSeveritySound(EToastSeverity Severity) const;

	TWeakObjectPtr<UMissionInventorySubsystem> CachedSubsystem;
	FTimerHandle HideTimerHandle;

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
