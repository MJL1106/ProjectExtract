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
	if (!Player)
	{
		PromptContainer->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// Revive only. The world-interact prompt is owned by the HUD's own prompt container now, and
	// this widget drawing it too put the same hint on screen twice.
	if (!IsValid(Player->GetReviveCandidate()))
	{
		PromptContainer->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	PromptContainer->SetVisibility(ESlateVisibility::HitTestInvisible);

	const bool bHolding = Player->IsRevivingTarget();

	if (PromptText)
		PromptText->SetText(bHolding ? RevivingHint : ReviveHint);
	if (HoldProgressBar)
	{
		HoldProgressBar->SetVisibility(bHolding ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		HoldProgressBar->SetPercent(Player->GetReviveProgress());
	}
}
