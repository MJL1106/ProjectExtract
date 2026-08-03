// Battlefield-style per-hit damage numbers in a fixed column left of the crosshair.
// Each hit shows its own number (red for headshots); new hits push older numbers up the stack.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DamageNumberWidget.generated.h"

class UCanvasPanel;
class UTextBlock;

/**
 * One pooled number. Requires a Widget Blueprint with a TextBlock named "TotalText"
 * (styled in the WBP; color is driven per hit from the layer's color properties).
 * Positioned by the owning layer via RenderTranslation.
 */
UCLASS()
class EXTRACTION_API UDamageNumberEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetValue(int32 Value, const FLinearColor& Color);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TotalText;
};

/**
 * Full-screen layer owning a pre-built pool of number entries (no per-hit widget
 * construction). Binds itself to AExtractionPlayerController::OnDamageDealt. Battlefield-style:
 * every hit spawns its own number at a fixed offset left of the crosshair, pushing older
 * numbers one line up; each number lingers then fades independently.
 */
UCLASS()
class EXTRACTION_API UDamageNumberWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> NumberCanvas;

	/** Entry WBP (parent UDamageNumberEntryWidget) — assigned in the layer WBP defaults. */
	UPROPERTY(EditDefaultsOnly, Category = "Damage Numbers")
	TSubclassOf<UDamageNumberEntryWidget> EntryWidgetClass;

	/** Pooled entry count — bounds simultaneous numbers (SMG-rate fire needs ~10). */
	UPROPERTY(EditDefaultsOnly, Category = "Damage Numbers", meta = (ClampMin = "1", ClampMax = "16"))
	int32 PoolSize = 12;

	/** Seconds a number holds at full opacity before fading. */
	UPROPERTY(EditDefaultsOnly, Category = "Damage Numbers", meta = (ClampMin = "0.1"))
	float LingerDuration = 0.6f;

	/** Fade-out seconds after the linger expires. */
	UPROPERTY(EditDefaultsOnly, Category = "Damage Numbers", meta = (ClampMin = "0.05"))
	float FadeDuration = 0.3f;

	/** Vertical spacing between stacked numbers (widget units). */
	UPROPERTY(EditDefaultsOnly, Category = "Damage Numbers", meta = (ClampMin = "8.0"))
	float LineHeight = 20.f;

	/** Screen-space offset of the newest number from the viewport center (Battlefield sits left of the crosshair). */
	UPROPERTY(EditDefaultsOnly, Category = "Damage Numbers")
	FVector2D NumberScreenOffset = FVector2D(-60.f, 0.f);

	/** Body-hit number color. */
	UPROPERTY(EditDefaultsOnly, Category = "Damage Numbers")
	FLinearColor HitNumberColor = FLinearColor::White;

	/** Headshot-hit number color (red-orange, per the Battlefield reference). */
	UPROPERTY(EditDefaultsOnly, Category = "Damage Numbers")
	FLinearColor HeadshotNumberColor = FLinearColor(1.f, 0.25f, 0.1f);

private:
	struct FNumberEntry
	{
		/** Not a UPROPERTY: NumberCanvas's widget tree holds the hard ref; entries are never removed from it. */
		TObjectPtr<UDamageNumberEntryWidget> Widget;
		float TimeSinceHit = 0.f;
		/** Widget units above the anchor — bumped by LineHeight when a newer number lands. */
		float StackOffset = 0.f;
		bool bActive = false;
	};

	UFUNCTION()
	void HandleDamageDealt(AActor* Victim, float Damage, float HeadshotDamage, bool bKilled, FVector WorldLocation);

	void BuildPool();

	TArray<FNumberEntry> Entries;
};
