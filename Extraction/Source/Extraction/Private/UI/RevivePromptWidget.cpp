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

	// Revive outranks a world interactable when both are under the crosshair.
	const bool bRevive = IsValid(Player->GetReviveCandidate());
	const bool bInteract = !bRevive && IsValid(Player->GetInteractCandidate());
	if (!bRevive && !bInteract)
	{
		PromptContainer->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	PromptContainer->SetVisibility(ESlateVisibility::HitTestInvisible);

	bool bHolding;
	float Progress;
	FText Text;
	if (bRevive)
	{
		bHolding = Player->IsRevivingTarget();
		Progress = Player->GetReviveProgress();
		Text = bHolding ? RevivingHint : ReviveHint;
	}
	else
	{
		bHolding = Player->IsInteractHolding();
		Progress = Player->GetInteractHoldProgress();
		Text = (bHolding && !InteractHoldingHint.IsEmpty())
			? InteractHoldingHint
			: FText::Format(InteractHintFormat, Player->GetInteractPrompt());
	}

	if (PromptText)
		PromptText->SetText(Text);
	if (HoldProgressBar)
	{
		HoldProgressBar->SetVisibility(bHolding ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		HoldProgressBar->SetPercent(Progress);
	}
}
