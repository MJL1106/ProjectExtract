// UAttachmentStatRowWidget -- one line of the attachment stat preview: the measured quantity on the
// left, the signed percentage on the right, tinted by whether taking the attachment helps or hurts.
// The WBP child owns all layout and styling; this native base only fills and tints the two texts.
//
// WBP must contain:
//   UTextBlock "StatLabel" -- the measured quantity, e.g. "Vertical Recoil"
//   UTextBlock "StatValue" -- the signed percentage, e.g. "-15%"

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/AttachmentStatTypes.h"
#include "AttachmentStatRowWidget.generated.h"

class UTextBlock;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UAttachmentStatRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Fills the row from a computed delta. The colours are passed in rather than owned here so the
	 *  whole panel restyles from one place (the preview widget's better/worse UPROPERTYs). */
	UFUNCTION(BlueprintCallable, Category = "UI|Attachments")
	void SetStatDelta(const FAttachmentStatDelta& Delta, FLinearColor BetterColour, FLinearColor WorseColour);

protected:

	// --- Bound widgets (designer wires these in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatLabel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatValue;
};
