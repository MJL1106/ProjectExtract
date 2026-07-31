// Battlefield-style hitmarker X at the crosshair. White = body hit, deep red = headshot hit,
// blue = headshot on an armoured-head enemy (heavies) that did not kill,
// kill = expand + fade (bright red, or deep red on a headshot kill).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HitmarkerWidget.generated.h"

class UImage;
class UWidget;

/**
 * Requires a Widget Blueprint with a centered container named "MarkerRoot" holding four
 * Images named "TickTL", "TickTR", "TickBL", "TickBR" (rotated ticks forming an X with a
 * center gap). Animation is driven entirely from NativeTick — no UMG animations.
 * Binds itself to AExtractionPlayerController::OnDamageDealt.
 */
UCLASS()
class EXTRACTION_API UHitmarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Plays the marker for a hit. Kill overrides headshot styling for the expand animation.
	 *  bArmoredHead = true renders blue for a non-lethal headshot on heavies. */
	UFUNCTION(BlueprintCallable, Category = "Hitmarker")
	void ShowHit(bool bHeadshot, bool bKilled, bool bArmoredHead = false);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> MarkerRoot;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> TickTL;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> TickTR;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> TickBL;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> TickBR;

	// Second parallel tick per arm — shown only on headshot hits/kills (Battlefield style).
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> DoubleTL;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> DoubleTR;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> DoubleBL;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> DoubleBR;

	/** Body-hit marker color. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Colors")
	FLinearColor HitColor = FLinearColor::White;

	/** Headshot-hit (no kill) marker color — Battlefield keeps this white; doubled ticks carry the meaning. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Colors")
	FLinearColor HeadshotColor = FLinearColor::White;

	/** Kill-confirm marker color (Battlefield coral red). */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Colors")
	FLinearColor KillColor = FLinearColor(0.9f, 0.3f, 0.22f);

	/** Kill-confirm marker color when the killing hit was a headshot. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Colors")
	FLinearColor HeadshotKillColor = FLinearColor(0.9f, 0.3f, 0.22f);

	/** Headshot on an enemy whose head does not one-tap (heavies), when the hit did not kill. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Colors")
	FLinearColor ArmoredHeadshotColor = FLinearColor(0.15f, 0.55f, 1.0f);

	/** Seconds a normal hit marker stays up (including fade). */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Timing", meta = (ClampMin = "0.05"))
	float HitDuration = 0.3f;

	/** Scale a hit marker punches in at before settling to 1.0 — the per-hit impact pop. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Timing", meta = (ClampMin = "1.0"))
	float HitPopScale = 1.35f;

	/** Seconds the pop takes to settle from HitPopScale to 1.0. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Timing", meta = (ClampMin = "0.02"))
	float PopSettleSeconds = 0.08f;

	/** Fraction of HitDuration held at full opacity before the fade starts. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Timing", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float HitHoldFraction = 0.35f;

	/** Seconds the kill marker takes to expand and fade. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Timing", meta = (ClampMin = "0.05"))
	float KillDuration = 0.45f;

	/** Render scale the kill marker starts at — Battlefield's kill X is larger than the hit X. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Timing", meta = (ClampMin = "1.0"))
	float KillStartScale = 1.25f;

	/** Render scale the kill marker expands to as it fades. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitmarker|Timing", meta = (ClampMin = "1.0"))
	float KillEndScale = 1.9f;

private:
	UFUNCTION()
	void HandleDamageDealt(AActor* Victim, float Damage, float HeadshotDamage, bool bKilled, FVector WorldLocation);

	void SetTickColor(const FLinearColor& Color);

	/** Seconds left on the current marker; <= 0 means idle (root at opacity 0). */
	float TimeRemaining = 0.f;
	float ActiveDuration = 0.f;
	bool bKillAnim = false;
};
