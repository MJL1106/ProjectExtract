// URevivePromptWidget -- revive hint + hold progress, polled off the owning AExtractionPlayer.

#include "UI/RevivePromptWidget.h"
#include "Character/ExtractionPlayer.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

void URevivePromptWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (PromptContainer)
		PromptContainer->SetVisibility(ESlateVisibility::Collapsed);
}

void URevivePromptWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!PromptContainer) return;

	const APlayerController* PC = GetOwningPlayer();
	const AExtractionPlayer* Player = PC ? Cast<AExtractionPlayer>(PC->GetPawn()) : nullptr;
	const bool bShow = Player && IsValid(Player->GetReviveCandidate());
	PromptContainer->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bShow) return;

	const bool bHolding = Player->IsRevivingTarget();
	if (PromptText)
		PromptText->SetText(bHolding ? RevivingHint : ReviveHint);
	if (HoldProgressBar)
	{
		HoldProgressBar->SetVisibility(bHolding ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		HoldProgressBar->SetPercent(Player->GetReviveProgress());
	}
}
