// UObjectiveSubsystem implementation.

#include "Game/ObjectiveSubsystem.h"

void UObjectiveSubsystem::AddObjective(FName Id, FText Label, FVector WorldLocation, AActor* TargetActor)
{
	if (Id == NAME_None) return;

	RemoveObjective(Id); // replace-by-id (RemoveObjective broadcasts only when something was removed)

	FObjectiveMarker& Marker = Objectives.AddDefaulted_GetRef();
	Marker.Id = Id;
	Marker.Label = Label;
	Marker.WorldLocation = WorldLocation;
	Marker.TargetActor = TargetActor;

	OnObjectivesChanged.Broadcast();
}

void UObjectiveSubsystem::RemoveObjective(FName Id)
{
	const int32 Removed = Objectives.RemoveAll(
		[Id](const FObjectiveMarker& Marker) { return Marker.Id == Id; });
	if (Removed > 0)
		OnObjectivesChanged.Broadcast();
}

void UObjectiveSubsystem::ClearObjectives()
{
	if (Objectives.Num() == 0) return;
	Objectives.Reset();
	OnObjectivesChanged.Broadcast();
}
