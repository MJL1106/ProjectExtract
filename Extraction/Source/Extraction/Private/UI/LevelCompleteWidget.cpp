// ULevelCompleteWidget -- native base for the level-complete overlay.

#include "UI/LevelCompleteWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

#include "Game/ExtractionPlayerController.h"

ULevelCompleteWidget::ULevelCompleteWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The controller focuses the widget root for gamepad/keyboard; an unfocusable root declines it.
	SetIsFocusable(true);
}

void ULevelCompleteWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(RestartButton))
		RestartButton->OnClicked.AddUniqueDynamic(this, &ULevelCompleteWidget::HandleRestartClicked);
}

void ULevelCompleteWidget::NativeDestruct()
{
	if (IsValid(RestartButton))
		RestartButton->OnClicked.RemoveDynamic(this, &ULevelCompleteWidget::HandleRestartClicked);

	Super::NativeDestruct();
}

void ULevelCompleteWidget::HandleRestartClicked()
{
	AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer());
	if (!IsValid(PC)) return;

	PC->RequestRestartLevel();
}
