// UAttachmentStatRowWidget -- see header.

#include "UI/AttachmentStatRowWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"

void UAttachmentStatRowWidget::SetStatDelta(const FAttachmentStatDelta& Delta, FLinearColor BetterColour, FLinearColor WorseColour)
{
	if (IsValid(StatLabel))
		StatLabel->SetText(Delta.Label);

	if (IsValid(StatValue))
	{
		if (Delta.bUnboundedChange)
		{
			// The old multiplier was effectively zero (or the new one is) -- a percentage is meaningless.
			// An arrow conveys direction without implying a magnitude the player could compare.
			StatValue->SetText(FText::FromString(Delta.bIsImprovement ? TEXT("\x2191") : TEXT("\x2193")));
		}
		else
		{
			// "%+d%%" -- printf's '+' flag supplies the sign for positives, so "+8%" and "-15%" both
			// read as a CHANGE rather than an absolute value. Whole percent only; sub-percent noise
			// never reaches a row because the panel's epsilon filter drops it first.
			StatValue->SetText(FText::FromString(FString::Printf(TEXT("%+d%%"), FMath::RoundToInt(Delta.PercentDelta))));
		}

		StatValue->SetColorAndOpacity(FSlateColor(Delta.bIsImprovement ? BetterColour : WorseColour));
	}

	// A row is reused across shows, so a delta that has settled to nothing (a fitted item compared
	// to itself) must actively clear the arrow/verdict a PREVIOUS delta left visible -- there is no
	// "unset" state to fall back on. Never both arrows at once: the shape carries the same verdict
	// the colour does, so only one of the two can be showing at a time.
	const bool bNoChange = FMath::IsNearlyZero(Delta.PercentDelta);
	const FSlateColor VerdictColour(Delta.bIsImprovement ? BetterColour : WorseColour);

	if (IsValid(ArrowBetter))
		ArrowBetter->SetVisibility(!bNoChange && Delta.bIsImprovement ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	if (IsValid(ArrowWorse))
		ArrowWorse->SetVisibility(!bNoChange && !Delta.bIsImprovement ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	if (!IsValid(VerdictText)) return;

	if (bNoChange)
	{
		VerdictText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	VerdictText->SetText(Delta.bIsImprovement ? BetterVerdictText : WorseVerdictText);
	VerdictText->SetColorAndOpacity(VerdictColour);
	VerdictText->SetVisibility(ESlateVisibility::HitTestInvisible);
}
