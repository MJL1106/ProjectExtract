// UBarkFeedWidget — C++ base for the enemy bark subtitle feed. Visuals live in the BP child.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BarkFeedWidget.generated.h"

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UBarkFeedWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Implement in BP: append/display one subtitle line (speaker may be empty). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Barks")
	void OnBarkReceived(const FText& SpeakerName, const FText& Line);

private:
	UFUNCTION()
	void HandleBarkRequested(FText SpeakerName, FText Line);
};
