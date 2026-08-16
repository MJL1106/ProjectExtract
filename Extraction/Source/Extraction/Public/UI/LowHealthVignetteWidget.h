// ULowHealthVignetteWidget — full-screen low-health warning: a red edge vignette whose corners
// curve inward, over a slight uniform darken, driven by the player's COMBINED health + shield pool.
//
// Entirely code-built: the widget tree is constructed in Initialize and the vignette mask is
// generated at runtime, so the controller can spawn the raw C++ class with no Blueprint subclass,
// no texture asset and no designer wiring. A BP subclass can still be made to retune the
// thresholds/colors; the controller's class property accepts one.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LowHealthVignetteWidget.generated.h"

class UHealthComponent;
class UImage;
class UTexture2D;

UCLASS()
class EXTRACTION_API ULowHealthVignetteWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- Thresholds (absolute points on health + shield combined) ---

	/** Combined pool at or above this shows nothing. */
	UPROPERTY(EditAnywhere, Category = "Vignette", meta = (ClampMin = "1.0"))
	float WarnThreshold = 70.f;

	/** Where the ramp knees into the intense band. */
	UPROPERTY(EditAnywhere, Category = "Vignette", meta = (ClampMin = "1.0"))
	float IntenseThreshold = 25.f;

	/** At or below this the effect holds at full intensity. */
	UPROPERTY(EditAnywhere, Category = "Vignette", meta = (ClampMin = "0.0"))
	float MaxThreshold = 15.f;

	/** Intensity reached at IntenseThreshold — the WarnThreshold→IntenseThreshold band ramps 0 to
	 *  here, and the short IntenseThreshold→MaxThreshold band ramps here to 1, so the last few
	 *  points of pool read as a distinct step up rather than more of the same slope. */
	UPROPERTY(EditAnywhere, Category = "Vignette", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IntenseKneeIntensity = 0.55f;

	// --- Presentation ---

	UPROPERTY(EditAnywhere, Category = "Vignette", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxRedOpacity = 0.85f;

	/** Uniform whole-screen darken at full intensity. Kept subtle — the HUD stays readable under it. */
	UPROPERTY(EditAnywhere, Category = "Vignette", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxDarkenOpacity = 0.22f;

	UPROPERTY(EditAnywhere, Category = "Vignette")
	FLinearColor EdgeColor = FLinearColor(0.65f, 0.02f, 0.02f);

	/** Exponential fade rate toward the target intensity (FInterpTo), in 1/seconds. */
	UPROPERTY(EditAnywhere, Category = "Vignette", meta = (ClampMin = "0.1"))
	float FadeSpeed = 2.5f;

	/** How far the red intrudes from the border, as a fraction of the half-screen. Also sets the
	 *  corner rounding — the mask is a rounded-rect falloff, so a bigger inset curves the corners
	 *  further in. */
	UPROPERTY(EditAnywhere, Category = "Vignette", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float EdgeInset = 0.55f;

	/** Falloff shaping exponent — higher hugs the border harder before ramping. */
	UPROPERTY(EditAnywhere, Category = "Vignette", meta = (ClampMin = "0.5", ClampMax = "4.0"))
	float EdgeFalloffPower = 1.6f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UImage> DarkenImage;

	UPROPERTY(Transient)
	TObjectPtr<UImage> EdgeImage;

	/** Runtime-generated rounded-rect vignette mask (white, alpha ramps to 1 at the border).
	 *  Reflected so GC sees the only reference keeping it alive. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> VignetteTexture;

	/** Memoised per pawn — invalidated when the owning pawn changes (respawn/possession swap). */
	TWeakObjectPtr<const APawn> CachedPawn;
	TWeakObjectPtr<const UHealthComponent> CachedHealth;

	float DisplayedIntensity = 0.f;

	/** Health + shield of the owning pawn, or -1 when there is nothing readable (no pawn, no
	 *  component, dead) — the caller fades out rather than latching full intensity over death UI. */
	float ResolveCombinedPool();

	/** Piecewise map: 0 at WarnThreshold, IntenseKneeIntensity at IntenseThreshold, 1 at
	 *  MaxThreshold and below. Denominators are clamped so mis-ordered thresholds degrade to a
	 *  step rather than dividing by zero. */
	float MapIntensity(float Combined) const;

	UTexture2D* BuildVignetteTexture() const;
};
