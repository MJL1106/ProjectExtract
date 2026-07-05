// HUD chip showing the companion's player-commanded mode (Normal / Combat / Stealth).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Companion/CompanionTypes.h"
#include "CompanionModeWidget.generated.h"

class UTextBlock;
class UImage;
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

	// --- Bound widgets ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ModeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ModeIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyHintText;

	// --- Designer-tunable per-mode display ---

	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	FText NormalLabel = NSLOCTEXT("CompanionMode", "Normal", "NORMAL");

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

	/** Applies label + tint for the mode. bFromChange also fires the BP flash hook. */
	void ApplyMode(ECompanionMode NewMode, bool bFromChange);

	/** Finds the command component on the owning pawn and subscribes. True once bound. */
	bool TryBindToCommandComponent();

	TWeakObjectPtr<UCompanionCommandComponent> BoundCommandComponent;

	/** Throttle for the lazy bind attempts in NativeTick. */
	float TimeSinceBindAttempt = 0.f;
	static constexpr float BindRetryInterval = 0.25f;
};
