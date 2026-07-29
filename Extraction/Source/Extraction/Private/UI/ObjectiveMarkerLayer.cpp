// UObjectiveMarkerLayer implementation.

#include "UI/ObjectiveMarkerLayer.h"
#include "UI/ObjectiveMarkerWidget.h"
#include "Game/ObjectiveSubsystem.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"

void UObjectiveMarkerLayer::NativeConstruct()
{
	Super::NativeConstruct();

	UWorld* World = GetWorld();
	UObjectiveSubsystem* Subsystem = World ? World->GetSubsystem<UObjectiveSubsystem>() : nullptr;
	if (!IsValid(Subsystem)) return;

	CachedSubsystem = Subsystem;
	if (!Subsystem->OnObjectivesChanged.IsAlreadyBound(this, &UObjectiveMarkerLayer::RebuildMarkers))
		Subsystem->OnObjectivesChanged.AddDynamic(this, &UObjectiveMarkerLayer::RebuildMarkers);
	if (!Subsystem->OnObjectiveLabelChanged.IsAlreadyBound(this, &UObjectiveMarkerLayer::HandleLabelChanged))
		Subsystem->OnObjectiveLabelChanged.AddDynamic(this, &UObjectiveMarkerLayer::HandleLabelChanged);

	RebuildMarkers();
}

void UObjectiveMarkerLayer::NativeDestruct()
{
	if (UObjectiveSubsystem* Subsystem = CachedSubsystem.Get())
	{
		Subsystem->OnObjectivesChanged.RemoveDynamic(this, &UObjectiveMarkerLayer::RebuildMarkers);
		Subsystem->OnObjectiveLabelChanged.RemoveDynamic(this, &UObjectiveMarkerLayer::HandleLabelChanged);
	}

	Super::NativeDestruct();
}

void UObjectiveMarkerLayer::RebuildMarkers()
{
	if (!MarkerCanvas || !MarkerWidgetClass) return;

	MarkerCanvas->ClearChildren();

	const UObjectiveSubsystem* Subsystem = CachedSubsystem.Get();
	if (!Subsystem) return;

	for (const FObjectiveMarker& Objective : Subsystem->GetObjectives())
	{
		if (!Objective.bShowWorldMarker) continue;

		UObjectiveMarkerWidget* Marker = CreateWidget<UObjectiveMarkerWidget>(this, MarkerWidgetClass);
		if (!Marker) continue;

		Marker->SetObjective(Objective);

		// Anchor top-left at (0,0), centred alignment, auto-size — the marker's own
		// RenderTranslation (set per tick) then positions its centre at the projected point.
		UCanvasPanelSlot* CanvasSlot = MarkerCanvas->AddChildToCanvas(Marker);
		if (CanvasSlot)
		{
			CanvasSlot->SetAutoSize(true);
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CanvasSlot->SetPosition(FVector2D::ZeroVector);
		}
	}
}

void UObjectiveMarkerLayer::HandleLabelChanged(FName Id, const FText& NewLabel)
{
	if (!MarkerCanvas) return;

	// Walk the existing children rather than rebuilding. The label is collapsed on the marker
	// widget today, but the stored Objective must stay current so a future un-collapse (or a
	// world-space fallback) reads the right text. Crucially, smoothed screen position and
	// projection state survive — a full rebuild resets both and visibly pops the marker.
	for (int32 Index = 0; Index < MarkerCanvas->GetChildrenCount(); ++Index)
	{
		UObjectiveMarkerWidget* Marker = Cast<UObjectiveMarkerWidget>(MarkerCanvas->GetChildAt(Index));
		if (IsValid(Marker) && Marker->GetObjectiveId() == Id)
		{
			Marker->UpdateLabel(NewLabel);
			return;
		}
	}
}
