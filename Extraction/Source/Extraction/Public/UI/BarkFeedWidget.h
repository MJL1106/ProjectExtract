// UBarkFeedWidget — C++ base for the enemy bark subtitle feed. Owns the single-slot display
// lifetime (duration, expiry, authoritative clear); the BP child only styles and animates.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BarkFeedWidget.generated.h"

class UTextBlock;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UBarkFeedWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Implement in BP: display one subtitle line (speaker may be empty) — play the fade-in here.
	 *  MUST StopAnimation on the fade-out first: a replacement bark arriving mid-fade otherwise has
	 *  the still-running fade-out win the opacity write and the new line displays invisible.
	 *  Text/visibility are already set by C++ when the optional bindings below exist. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Barks")
	void OnBarkReceived(const FText& SpeakerName, const FText& Line);

	/** Implement in BP: display window ended — play the fade-out. C++ clears the line
	 *  InFadeOutSeconds later regardless, so a missing animation can never strand the subtitle. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Barks")
	void OnBarkExpiring(float InFadeOutSeconds);

	/** Implement in BP: line fully cleared (post-fade) — reset any leftover animation state. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Barks")
	void OnBarkCleared();

	// Display duration = BaseSeconds + Line length x PerCharacterSeconds, clamped to [Min, Max].
	UPROPERTY(EditDefaultsOnly, Category = "Barks", meta = (ClampMin = "0.0"))
	float BaseSeconds = 1.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Barks", meta = (ClampMin = "0.0"))
	float PerCharacterSeconds = 0.045f;

	UPROPERTY(EditDefaultsOnly, Category = "Barks", meta = (ClampMin = "0.5"))
	float MinDisplaySeconds = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Barks", meta = (ClampMin = "0.5"))
	float MaxDisplaySeconds = 4.5f;

	/** Gap between OnBarkExpiring and the authoritative clear — match the BP fade-out length. */
	UPROPERTY(EditDefaultsOnly, Category = "Barks", meta = (ClampMin = "0.0"))
	float FadeOutSeconds = 0.35f;

	// Authoritative display targets. Optional so a BP child with different names still compiles —
	// but only a conformed child gets the guaranteed C++ clear.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BarkLineText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BarkSpeakerText;

private:
	UFUNCTION()
	void HandleBarkRequested(FText SpeakerName, FText Line);

	void HandleBarkExpired();
	void ClearBarkLine();

	FTimerHandle DisplayTimerHandle;
	FTimerHandle ClearTimerHandle;
};
