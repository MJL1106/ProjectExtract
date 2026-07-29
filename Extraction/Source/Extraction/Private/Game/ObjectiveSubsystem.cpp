// UObjectiveSubsystem implementation.

#include "Game/ObjectiveSubsystem.h"
#include "World/ObjectiveMarkerDisplay.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

// --- FObjectiveMarker ---

FVector FObjectiveMarker::ResolveLocation() const
{
	const AActor* Target = TargetActor.Get();
	if (!Target)
		return WorldLocation + Offset;

	// Resolve from the bounds BASE plus a standard height so markers read at one consistent
	// height regardless of target shape (floor crate vs door vs enemy).
	FVector Origin;
	FVector Extents;
	Target->GetActorBounds(false, Origin, Extents);
	return FVector(Origin.X, Origin.Y, Origin.Z - Extents.Z + HeightAboveBase) + Offset;
}

// --- UObjectiveSubsystem ---

void UObjectiveSubsystem::Deinitialize()
{
	DestroyAllDisplays();
	Super::Deinitialize();
}

void UObjectiveSubsystem::AddObjective(FName Id, FText Label, FVector WorldLocation,
	AActor* TargetActor, FVector Offset, bool bShowWorldMarker, float HeightAboveBase)
{
	if (Id == NAME_None) return;

	// Check for existing objective with the same id -- update in-place to avoid destroy+respawn churn.
	FObjectiveMarker* Existing = Objectives.FindByPredicate(
		[Id](const FObjectiveMarker& M) { return M.Id == Id; });
	if (Existing)
	{
		Existing->Label = Label;
		Existing->WorldLocation = WorldLocation;
		Existing->TargetActor = TargetActor;
		Existing->Offset = Offset;
		Existing->bShowWorldMarker = bShowWorldMarker;
		Existing->HeightAboveBase = HeightAboveBase;

		if (bShowWorldMarker)
		{
			// Re-init the existing display actor rather than rebuilding all displays.
			bool bFoundDisplay = false;
			for (AObjectiveMarkerDisplay* Display : ActiveDisplays)
			{
				if (IsValid(Display) && Display->GetObjectiveId() == Id)
				{
					Display->InitObjective(*Existing);
					bFoundDisplay = true;
					break;
				}
			}
			// Flag may have flipped false->true on this id — spawn the missing display.
			if (!bFoundDisplay)
				RebuildDisplayActors();
		}
		else
		{
			// Flag flipped true->false — the stale pass culls the now-unwanted display.
			RebuildDisplayActors();
		}

		OnObjectivesChanged.Broadcast();
		return;
	}

	FObjectiveMarker& Marker = Objectives.AddDefaulted_GetRef();
	Marker.Id = Id;
	Marker.Label = Label;
	Marker.WorldLocation = WorldLocation;
	Marker.TargetActor = TargetActor;
	Marker.Offset = Offset;
	Marker.bShowWorldMarker = bShowWorldMarker;
	Marker.HeightAboveBase = HeightAboveBase;

	OnObjectivesChanged.Broadcast();
	RebuildDisplayActors();
}

void UObjectiveSubsystem::UpdateObjectiveLabel(FName Id, FText NewLabel)
{
	if (Id == NAME_None) return;

	FObjectiveMarker* Existing = Objectives.FindByPredicate(
		[Id](const FObjectiveMarker& Marker) { return Marker.Id == Id; });
	if (!Existing) return;

	// Compared, not blindly written: a countdown re-formats the same clock every tick it lands inside
	// the same second, and a broadcast per re-format is a HUD rebuild the player never sees a change
	// from. Silence is the point of this call existing.
	if (Existing->Label.EqualTo(NewLabel)) return;

	Existing->Label = NewLabel;
	RefreshDisplayFor(*Existing);
	OnObjectiveLabelChanged.Broadcast(Id, NewLabel);
}

void UObjectiveSubsystem::RemoveObjective(FName Id)
{
	const int32 Removed = Objectives.RemoveAll(
		[Id](const FObjectiveMarker& Marker) { return Marker.Id == Id; });
	if (Removed > 0)
	{
		OnObjectivesChanged.Broadcast();
		RebuildDisplayActors();
	}
}

void UObjectiveSubsystem::ClearObjectives()
{
	if (Objectives.Num() == 0) return;
	Objectives.Reset();
	OnObjectivesChanged.Broadcast();
	DestroyAllDisplays();
}

void UObjectiveSubsystem::SetMarkerDisplayClass(TSubclassOf<AObjectiveMarkerDisplay> InClass)
{
	MarkerDisplayClass = InClass;
	// The guard below skips a rebuild when the shown-id set has not moved, and a class swap moves
	// nothing but the class — forget the last set so the next rebuild is unconditional.
	LastDisplayedIds.Reset();
	RebuildDisplayActors();
}

void UObjectiveSubsystem::RebuildDisplayActors()
{
	if (!MarkerDisplayClass) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Skip on dedicated server -- these are client-side visuals only.
	if (World->GetNetMode() == NM_DedicatedServer) return;

	// This whole function is a destroy + respawn pass over every billboard in the level, reached from
	// AddObjective and RemoveObjective on every marker change. The work depends on nothing but WHICH
	// ids want a world marker, so an unchanged set is an unchanged display list. A display destroyed
	// from underneath us still forces a pass through, so the self-healing respawn survives the
	// shortcut.
	TSet<FName> DisplayedIds;
	DisplayedIds.Reserve(Objectives.Num());
	for (const FObjectiveMarker& Objective : Objectives)
		if (Objective.bShowWorldMarker) DisplayedIds.Add(Objective.Id);

	const bool bHasStaleDisplay = ActiveDisplays.ContainsByPredicate(
		[](const AObjectiveMarkerDisplay* Display) { return !IsValid(Display); });
	if (!bHasStaleDisplay && DisplayedIds.Num() == LastDisplayedIds.Num() && DisplayedIds.Includes(LastDisplayedIds))
		return;

	// Destroy stale displays whose objective id is no longer active.
	for (int32 i = ActiveDisplays.Num() - 1; i >= 0; --i)
	{
		AObjectiveMarkerDisplay* Display = ActiveDisplays[i];
		if (!IsValid(Display))
		{
			ActiveDisplays.RemoveAtSwap(i);
			continue;
		}
		const bool bStillActive = Objectives.ContainsByPredicate(
			[Display](const FObjectiveMarker& M)
			{ return M.Id == Display->GetObjectiveId() && M.bShowWorldMarker; });
		if (!bStillActive)
		{
			Display->Destroy();
			ActiveDisplays.RemoveAtSwap(i);
		}
	}

	// Spawn displays for objectives that don't yet have one.
	for (const FObjectiveMarker& Objective : Objectives)
	{
		if (!Objective.bShowWorldMarker) continue;

		const bool bAlreadyDisplayed = ActiveDisplays.ContainsByPredicate(
			[&Objective](const AObjectiveMarkerDisplay* D) {
				return IsValid(D) && D->GetObjectiveId() == Objective.Id;
			});
		if (bAlreadyDisplayed) continue;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AObjectiveMarkerDisplay* Display = World->SpawnActor<AObjectiveMarkerDisplay>(
			MarkerDisplayClass, Objective.ResolveLocation(), FRotator::ZeroRotator, SpawnParams);
		if (IsValid(Display))
		{
			Display->InitObjective(Objective);
			ActiveDisplays.Add(Display);
		}
	}

	// Committed AFTER the loop: if SpawnActor returned null the id is NOT recorded as displayed,
	// so the next call retries the spawn instead of silently accepting the hole.
	LastDisplayedIds.Reset();
	for (const AObjectiveMarkerDisplay* Display : ActiveDisplays)
		if (IsValid(Display)) LastDisplayedIds.Add(Display->GetObjectiveId());
}

void UObjectiveSubsystem::DestroyAllDisplays()
{
	for (AObjectiveMarkerDisplay* Display : ActiveDisplays)
	{
		if (IsValid(Display))
			Display->Destroy();
	}
	ActiveDisplays.Reset();

	// Load-bearing for the rebuild guard: ClearObjectives lands here, and re-registering the SAME id
	// afterwards would otherwise match the stale set and skip the respawn.
	LastDisplayedIds.Reset();
}

void UObjectiveSubsystem::RefreshDisplayFor(const FObjectiveMarker& Objective)
{
	if (!Objective.bShowWorldMarker) return;

	for (AObjectiveMarkerDisplay* Display : ActiveDisplays)
	{
		if (!IsValid(Display) || Display->GetObjectiveId() != Objective.Id) continue;
		Display->InitObjective(Objective);
		return;
	}
}
