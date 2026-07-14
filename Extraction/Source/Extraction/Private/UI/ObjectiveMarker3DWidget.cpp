// UObjectiveMarker3DWidget implementation.

#include "UI/ObjectiveMarker3DWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UObjectiveMarker3DWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UObjectiveMarker3DWidget::SetLabel(const FText& InLabel)
{
	if (!LabelText) return;
	LabelText->SetText(InLabel);
	LabelText->SetVisibility(InLabel.IsEmpty()
		? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

void UObjectiveMarker3DWidget::SetDistance(int32 DistanceMetres)
{
	if (!DistanceText) return;
	DistanceText->SetText(FText::FromString(FString::Printf(TEXT("%d m"), DistanceMetres)));
}
