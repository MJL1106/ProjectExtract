// UAttachmentStatRowWidget -- see header.

#include "UI/AttachmentStatRowWidget.h"

#include "Components/TextBlock.h"

void UAttachmentStatRowWidget::SetStatDelta(const FAttachmentStatDelta& Delta, FLinearColor BetterColour, FLinearColor WorseColour)
{
	if (IsValid(StatLabel))
		StatLabel->SetText(Delta.Label);

	if (!IsValid(StatValue)) return;

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
