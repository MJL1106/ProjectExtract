// HUD chip showing the companion's player-commanded mode (Normal / Combat / Stealth).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Companion/CompanionTypes.h"
#include "CompanionModeWidget.generated.h"

class UTextBlock;
class UImage;
class UPanelWidget;
class UCompanionCommandComponent;

/**
 * Persistent HUD element. Subscribes to UCompanionCommandComponent::OnCompanionModeChanged on the
 * owning player pawn (resolved lazily — the pawn may possess after widget construction).
 *
 * Requires a Widget Blueprint with:
 *   - UTextBlock named "ModeText"          (mode name, tinted per mode)
 *   - UImage named "ModeIcon"              (optional icon, tinted per mode — BindWidgetOptional)
 *   - UTextBlock named "KeyHintText"       (optional "[X]" hint — BindWidgetOptional)
 */
UCLASS()
class EXTRACTION_API UCompanionModeWidget : public UUserWidget
{
	GENERATED_BODY()

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Designer hook for the on-switch flash/pulse animation. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Companion|Mode")
	void OnModeChangedBP(ECompanionMode NewMode);

	/** Designer hook — expand the chip into the numbered picker list (bOpen=true) or collapse back to
	 *  the single chip (false). ActiveMode is the currently-commanded mode, for highlighting its row. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Companion|Mode")
	void OnModeMenuOpenChangedBP(bool bOpen, ECompanionMode ActiveMode);

	/** Designer hook — covering-fire row 4 availability changed. bAvailable reflects whether the
	 *  companion currently has a live combat target and the cooldown has expired. CooldownRemaining
	 *  is >0 while the post-use cooldown is active. Called every NativeTick while the picker is open. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Companion|CoveringFire")
	void OnCoverMeAvailabilityChangedBP(bool bAvailable, float CooldownRemaining);

	/** Designer hook — covering-fire countdown tick. Remaining is the seconds left in the window;
	 *  0 means the window just ended. bPaused is true while the companion is reloading and the
	 *  clock is held. Fires on pause edges even when the number has not changed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Companion|CoveringFire")
	void OnCoveringFireCountdownBP(float Remaining, bool bPaused);

	// --- Bound widgets ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ModeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ModeIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyHintText;

	/** The numbered picker list (1/2/3 rows). Shown while the picker is open, collapsed otherwise. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ModeListPanel;

	/** Optional wrapper around the single-mode chip, hidden while the picker list is shown. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ChipContainer;

	// --- Designer-tunable per-mode display ---

	/** Label for ECompanionMode::Normal — presented as "Defensive"; the enumerator keeps its name. */
	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	FText NormalLabel = NSLOCTEXT("CompanionMode", "Defensive", "DEFENSIVE");

	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	FText CombatLabel = NSLOCTEXT("CompanionMode", "Combat", "COMBAT");

	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	FText StealthLabel = NSLOCTEXT("CompanionMode", "Stealth", "STEALTH");

	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	FLinearColor NormalColor = FLinearColor(0.85f, 0.85f, 0.85f, 1.f);

	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	FLinearColor CombatColor = FLinearColor(1.f, 0.35f, 0.1f, 1.f);

	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	FLinearColor StealthColor = FLinearColor(0.3f, 0.6f, 1.f, 1.f);

	/** Shown in KeyHintText when bound. Matches the IMC binding for IA_CompanionModeToggle. */
	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	FText KeyHint = NSLOCTEXT("CompanionMode", "KeyHint", "[X]");

private:

	UFUNCTION()
	void HandleModeChanged(ECompanionMode NewMode);

	UFUNCTION()
	void HandleModeMenuChanged(bool bOpen);

	/** Applies label + tint for the mode. bFromChange also fires the BP flash hook. */
	void ApplyMode(ECompanionMode NewMode, bool bFromChange);

	/** Finds the command component on the owning pawn and subscribes. True once bound. */
	bool TryBindToCommandComponent();

	UFUNCTION()
	void HandleCoveringFireTick(float Remaining, bool bPaused);

	TWeakObjectPtr<UCompanionCommandComponent> BoundCommandComponent;

	/** Latest commanded mode, cached so the picker-open event can highlight the active row. */
	ECompanionMode CurrentMode = ECompanionMode::Normal;

	/** Cached availability to detect edges for the BP hook. */
	bool bLastCoverMeAvailable = false;

	/** Throttle for the lazy bind attempts in NativeTick. */
	float TimeSinceBindAttempt = 0.f;
	static constexpr float BindRetryInterval = 0.25f;
};
