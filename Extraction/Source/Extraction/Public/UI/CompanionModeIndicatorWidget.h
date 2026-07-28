// Overhead companion mode indicator — pops on mode change, holds, then fades out.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Companion/CompanionTypes.h"
#include "CompanionModeIndicatorWidget.generated.h"

class UImage;
class UTextBlock;
class ACompanionCharacter;

/**
 * Lives in the companion's ModeWidgetComponent (screen-space, above the health bar).
 * Invisible at rest; a mode change shows it for HoldDuration then fades over FadeDuration.
 *
 * Requires a Widget Blueprint with:
 *   - UImage named "ModeIcon"            (tinted per mode)
 *   - UTextBlock named "ModeLabel"       (optional short label — BindWidgetOptional)
 *
 * The owning CompanionCharacter calls SetCompanion() once the widget is constructed
 * (TryLinkModeWidget retry pattern, same as the enemy awareness meter).
 */
UCLASS()
class EXTRACTION_API UCompanionModeIndicatorWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Called by ACompanionCharacter once the widget instance exists. */
	void SetCompanion(ACompanionCharacter* InCompanion);

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- Bound widgets ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ModeIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ModeLabel;

	// --- Designer-tunable display ---

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

	/** Seconds fully visible after a mode change before the fade starts. */
	UPROPERTY(EditAnywhere, Category = "Companion|Mode", meta = (ClampMin = "0.0"))
	float HoldDuration = 2.5f;

	/** Fade-out length after the hold. */
	UPROPERTY(EditAnywhere, Category = "Companion|Mode", meta = (ClampMin = "0.05"))
	float FadeDuration = 0.75f;

private:

	UFUNCTION()
	void HandleModeChanged(ECompanionMode NewMode);

	void ApplyMode(ECompanionMode NewMode);

	TWeakObjectPtr<ACompanionCharacter> Companion;

	/** Remaining fully-visible time; fade runs when it hits zero. */
	float HoldRemaining = 0.f;
};
