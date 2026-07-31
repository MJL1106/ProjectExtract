// UTutorialControlRowWidget -- native base for a single briefing row.

#include "UI/TutorialControlRowWidget.h"

#include "Components/TextBlock.h"

namespace
{
	/** Fills a text block, or collapses it when there is nothing to show, so an empty field leaves no
	 *  gap in the column instead of an invisible-but-spaced row. */
	void ApplyOptionalText(UTextBlock* Block, const FText& Value)
	{
		if (!IsValid(Block)) return;

		if (Value.IsEmptyOrWhitespace())
		{
			Block->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}

		Block->SetText(Value);
		Block->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UTutorialControlRowWidget::SetRowContent(const FText& InKeyText, const FText& InLabel, const FText& InDetail, bool bInIsHeading)
{
	ApplyOptionalText(KeyText, InKeyText);
	ApplyOptionalText(DetailText, InDetail);

	if (IsValid(LabelText)) LabelText->SetText(InLabel);

	OnRowContentApplied(bInIsHeading);
}
