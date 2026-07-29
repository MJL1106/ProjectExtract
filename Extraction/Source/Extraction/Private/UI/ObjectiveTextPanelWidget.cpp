// UObjectiveTextPanelWidget implementation.

#include "UI/ObjectiveTextPanelWidget.h"
#include "Game/ObjectiveSubsystem.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"

void UObjectiveTextPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UWorld* World = GetWorld();
	UObjectiveSubsystem* Subsystem = World ? World->GetSubsystem<UObjectiveSubsystem>() : nullptr;
	if (!IsValid(Subsystem)) return;

	CachedSubsystem = Subsystem;
	if (!Subsystem->OnObjectivesChanged.IsAlreadyBound(this, &UObjectiveTextPanelWidget::Rebuild))
		Subsystem->OnObjectivesChanged.AddDynamic(this, &UObjectiveTextPanelWidget::Rebuild);
	if (!Subsystem->OnObjectiveLabelChanged.IsAlreadyBound(this, &UObjectiveTextPanelWidget::HandleLabelChanged))
		Subsystem->OnObjectiveLabelChanged.AddDynamic(this, &UObjectiveTextPanelWidget::HandleLabelChanged);

	Rebuild();
}

void UObjectiveTextPanelWidget::NativeDestruct()
{
	if (UObjectiveSubsystem* Subsystem = CachedSubsystem.Get())
	{
		Subsystem->OnObjectivesChanged.RemoveDynamic(this, &UObjectiveTextPanelWidget::Rebuild);
		Subsystem->OnObjectiveLabelChanged.RemoveDynamic(this, &UObjectiveTextPanelWidget::HandleLabelChanged);
	}

	Super::NativeDestruct();
}

void UObjectiveTextPanelWidget::Rebuild()
{
	if (!ObjectiveListText) return;

	const UObjectiveSubsystem* Subsystem = CachedSubsystem.Get();
	if (!Subsystem)
	{
		ObjectiveListText->SetText(FText::GetEmpty());
		return;
	}

	TArray<FText> Lines;
	Lines.Reserve(Subsystem->GetObjectives().Num());
	for (const FObjectiveMarker& Objective : Subsystem->GetObjectives())
	{
		if (!Objective.Label.IsEmpty())
			Lines.Add(Objective.Label);
	}

	ObjectiveListText->SetText(FText::Join(FText::FromString(LINE_TERMINATOR), Lines));
	ObjectiveListText->SetVisibility(Lines.Num() > 0
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UObjectiveTextPanelWidget::HandleLabelChanged(FName /*Id*/, const FText& /*NewLabel*/)
{
	// A label mutation is cheap enough to just re-join the text. Tracking per-id text blocks
	// would be more code for no visible gain on a single SetText call.
	Rebuild();
}
