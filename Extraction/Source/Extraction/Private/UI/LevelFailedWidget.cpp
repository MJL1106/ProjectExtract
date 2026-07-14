// ULevelFailedWidget -- native base for the level-failed overlay.

#include "UI/LevelFailedWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

#include "Game/ExtractionPlayerController.h"

ULevelFailedWidget::ULevelFailedWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void ULevelFailedWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(RestartButton))
		RestartButton->OnClicked.AddUniqueDynamic(this, &ULevelFailedWidget::HandleRestartClicked);
}

void ULevelFailedWidget::NativeDestruct()
{
	if (IsValid(RestartButton))
		RestartButton->OnClicked.RemoveDynamic(this, &ULevelFailedWidget::HandleRestartClicked);

	Super::NativeDestruct();
}

void ULevelFailedWidget::SetFailReason(const FText& Reason)
{
	if (!IsValid(ReasonText)) return;

	if (Reason.IsEmpty())
	{
		ReasonText->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		ReasonText->SetText(Reason);
		ReasonText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void ULevelFailedWidget::HandleRestartClicked()
{
	AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer());
	if (!IsValid(PC)) return;

	PC->RequestRestartLevel();
}
