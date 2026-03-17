// Simple crosshair HUD widget — draws a small dot at screen center.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrosshairWidget.generated.h"

class UImage;

/**
 * Minimal crosshair widget. Requires a Widget Blueprint with an Image named "CrosshairImage".
 * The image should be centered on screen via anchors.
 */
UCLASS()
class EXTRACTION_API UCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

protected:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> CrosshairImage;
};
