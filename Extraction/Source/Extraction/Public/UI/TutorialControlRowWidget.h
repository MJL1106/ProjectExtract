// UTutorialControlRowWidget -- one line of the controls briefing: key, label, detail.
// The WBP child owns all layout and styling; this native base only fills the three text blocks and
// collapses the ones with nothing to show.
//
// The WBP MUST contain UTextBlocks named exactly "KeyText", "LabelText" and "DetailText".

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TutorialControlRowWidget.generated.h"

class UTextBlock;

UCLASS()
class EXTRACTION_API UTutorialControlRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fills the row. An empty InDetail collapses the detail block, an empty InKeyText collapses the
	 *  key block — so a heading row and a bare "Move / WASD" row both render without holes. */
	UFUNCTION(BlueprintCallable, Category = "UI|Tutorial")
	void SetRowContent(const FText& InKeyText, const FText& InLabel, const FText& InDetail, bool bInIsHeading);

protected:
	/** Hook for the WBP to restyle itself once content is applied (headings read differently). */
	UFUNCTION(BlueprintImplementableEvent, Category = "UI|Tutorial")
	void OnRowContentApplied(bool bInIsHeading);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> KeyText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DetailText;
};
