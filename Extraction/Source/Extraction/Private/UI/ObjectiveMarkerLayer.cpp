// UObjectiveMarkerLayer implementation.

#include "UI/ObjectiveMarkerLayer.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"
#include "Game/ObjectiveSubsystem.h"
#include "UI/ObjectiveMarkerWidget.h"

namespace
{
	/** Ceiling on re-entrant reconcile passes. A legitimate cascade ("this objective completing adds
	 *  the next one") settles in two; anything deeper is a Blueprint feedback loop. */
	constexpr int32 MaxReconcilePasses = 8;
}

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
	if (!Subsystem->OnObjectiveStateChanged.IsAlreadyBound(this, &UObjectiveMarkerLayer::HandleStateChanged))
		Subsystem->OnObjectiveStateChanged.AddDynamic(this, &UObjectiveMarkerLayer::HandleStateChanged);

	RebuildMarkers();
}

void UObjectiveMarkerLayer::NativeDestruct()
{
	if (UObjectiveSubsystem* Subsystem = CachedSubsystem.Get())
	{
		Subsystem->OnObjectivesChanged.RemoveDynamic(this, &UObjectiveMarkerLayer::RebuildMarkers);
		Subsystem->OnObjectiveLabelChanged.RemoveDynamic(this, &UObjectiveMarkerLayer::HandleLabelChanged);
		Subsystem->OnObjectiveStateChanged.RemoveDynamic(this, &UObjectiveMarkerLayer::HandleStateChanged);
	}

	Super::NativeDestruct();
}

const FScreenMarkerViewContext& UObjectiveMarkerLayer::GetFrameViewContext()
{
	// One camera/viewport resolve per frame for the whole marker set. GetViewportScale re-queries
	// the DPI curve on every call in editor builds, so this is not free per marker.
	if (CachedContextFrame != GFrameCounter)
	{
		CachedContextFrame = GFrameCounter;
		if (!FScreenMarkerViewContext::Build(this, CachedViewContext))
			CachedViewContext = FScreenMarkerViewContext();
	}

	return CachedViewContext;
}

void UObjectiveMarkerLayer::GetOnScreenMarkerPositions(TArray<FVector2D>& OutPositions) const
{
	if (!MarkerCanvas) return;

	for (int32 Index = 0; Index < MarkerCanvas->GetChildrenCount(); ++Index)
	{
		const UObjectiveMarkerWidget* Marker = Cast<UObjectiveMarkerWidget>(MarkerCanvas->GetChildAt(Index));
		if (!IsValid(Marker) || Marker->bIsOffScreen) continue;

		FVector2D Position;
		if (Marker->GetScreenPosition(Position)) OutPositions.Add(Position);
	}
}

UObjectiveMarkerWidget* UObjectiveMarkerLayer::FindClosestOnScreenMarker(const FVector& WorldLocation, float RadiusCm) const
{
	if (!MarkerCanvas) return nullptr;

	const float RadiusSquared = FMath::Square(RadiusCm);
	UObjectiveMarkerWidget* Closest = nullptr;
	float ClosestDistSquared = 0.f;

	for (int32 Index = 0; Index < MarkerCanvas->GetChildrenCount(); ++Index)
	{
		UObjectiveMarkerWidget* Marker = Cast<UObjectiveMarkerWidget>(MarkerCanvas->GetChildAt(Index));
		// Off-screen (edge-clamped chevron) is not "the objective the player is looking at" -- a ping
		// near an objective whose marker has slid off-screen must place its own locator normally, not
		// suppress it in favour of a flash on a chevron the player cannot see land.
		if (!IsValid(Marker) || Marker->bIsOffScreen) continue;

		const float DistSquared = FVector::DistSquared(Marker->GetObjectiveWorldLocation(), WorldLocation);
		if (DistSquared > RadiusSquared) continue;
		// Strictly-less, not <=, so an exact tie keeps the first (lowest canvas index) match instead
		// of the last -- deterministic regardless of iteration order.
		if (Closest && DistSquared >= ClosestDistSquared) continue;

		Closest = Marker;
		ClosestDistSquared = DistSquared;
	}

	return Closest;
}

bool UObjectiveMarkerLayer::FlashMarkerNearWorldLocation(const FVector& WorldLocation, float RadiusCm)
{
	UObjectiveMarkerWidget* Closest = FindClosestOnScreenMarker(WorldLocation, RadiusCm);
	if (!Closest) return false;

	// Fired only after the search above has finished, and only once -- if a Blueprint implementation
	// of OnObjectivePingFlash mutated the canvas (it should not, but nothing stops it), that must not
	// happen while this function is still walking MarkerCanvas's children.
	Closest->PlayPingFlash();
	return true;
}

bool UObjectiveMarkerLayer::IsWorldLocationNearAnyMarker(const FVector& WorldLocation, float RadiusCm) const
{
	return FindClosestOnScreenMarker(WorldLocation, RadiusCm) != nullptr;
}

void UObjectiveMarkerLayer::RebuildMarkers()
{
	// A reconcile runs Blueprint -- RefreshObjective raises label/state events, and AddChildToCanvas
	// runs NativeConstruct which raises them again -- so a handler that adds or removes an objective
	// re-enters here. Letting the nested pass run is worse than dropping it: the nested pass would
	// reconcile to the new truth, then the outer loop would resume against its stale snapshot and
	// resurrect a marker for the objective that was just removed. Latch instead, and re-run once on
	// unwind so the nested request is honoured rather than lost.
	if (bReconciling)
	{
		bReconcilePending = true;
		return;
	}

	bReconciling = true;

	// Bounded: a handler that mutates the objective list on every pass would otherwise spin here
	// forever, and the HUD layer is not the place to discover a designer's feedback loop.
	int32 PassesRemaining = MaxReconcilePasses;
	do
	{
		bReconcilePending = false;
		ReconcileMarkers();
	}
	while (bReconcilePending && --PassesRemaining > 0);

	if (bReconcilePending)
	{
		UE_LOG(LogTemp, Warning, TEXT("UObjectiveMarkerLayer: objective list still changing after %d "
			"reconcile passes -- a Blueprint handler is mutating objectives from an objective event. "
			"Markers are one change behind until the next update."), MaxReconcilePasses);
	}

	bReconcilePending = false;
	bReconciling = false;
}

void UObjectiveMarkerLayer::ReconcileMarkers()
{
	if (!MarkerCanvas || !MarkerWidgetClass) return;

	const UObjectiveSubsystem* Subsystem = CachedSubsystem.Get();
	if (!Subsystem) return;

	// Snapshot, not a reference to the subsystem's live array. The latch above stops a nested
	// RECONCILE, but it does not stop a Blueprint handler calling AddObjective/RemoveObjective --
	// that still reallocates or shrinks the array underneath the loops below. Both guards are
	// needed. A handful of structs copied on a designer-driven event is not a tick.
	const TArray<FObjectiveMarker> Objectives = Subsystem->GetObjectives();

	// Reconcile by id rather than ClearChildren + CreateWidget. OnObjectivesChanged fires on every
	// in-place AddObjective rewrite, and a full rebuild would reset every marker's smoothed position
	// and replay every intro pulse each time ANY objective moved.
	TSet<FName> WantedIds;
	WantedIds.Reserve(Objectives.Num());
	for (const FObjectiveMarker& Objective : Objectives)
		if (Objective.bShowWorldMarker) WantedIds.Add(Objective.Id);

	for (int32 Index = MarkerCanvas->GetChildrenCount() - 1; Index >= 0; --Index)
	{
		const UObjectiveMarkerWidget* Marker = Cast<UObjectiveMarkerWidget>(MarkerCanvas->GetChildAt(Index));
		if (!IsValid(Marker) || !WantedIds.Contains(Marker->GetObjectiveId()))
			MarkerCanvas->RemoveChildAt(Index);
	}

	for (const FObjectiveMarker& Objective : Objectives)
	{
		if (!Objective.bShowWorldMarker) continue;

		if (UObjectiveMarkerWidget* Existing = FindMarker(Objective.Id))
		{
			Existing->RefreshObjective(Objective);
			continue;
		}

		SpawnMarker(Objective);
	}
}

void UObjectiveMarkerLayer::SpawnMarker(const FObjectiveMarker& Objective)
{
	UObjectiveMarkerWidget* Marker = CreateWidget<UObjectiveMarkerWidget>(this, MarkerWidgetClass);
	if (!IsValid(Marker)) return;

	Marker->SetObjective(Objective, this);

	// Anchor top-left at (0,0), centred alignment, auto-size — the marker's own RenderTranslation
	// (set per tick) then places its centre on the projected point.
	UCanvasPanelSlot* CanvasSlot = MarkerCanvas->AddChildToCanvas(Marker);
	if (!CanvasSlot) return;

	CanvasSlot->SetAnchors(FAnchors(0.f, 0.f));
	CanvasSlot->SetAutoSize(true);
	CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	CanvasSlot->SetPosition(FVector2D::ZeroVector);
}

UObjectiveMarkerWidget* UObjectiveMarkerLayer::FindMarker(FName Id) const
{
	if (!MarkerCanvas) return nullptr;

	for (int32 Index = 0; Index < MarkerCanvas->GetChildrenCount(); ++Index)
	{
		UObjectiveMarkerWidget* Marker = Cast<UObjectiveMarkerWidget>(MarkerCanvas->GetChildAt(Index));
		if (IsValid(Marker) && Marker->GetObjectiveId() == Id) return Marker;
	}

	return nullptr;
}

void UObjectiveMarkerLayer::HandleLabelChanged(FName Id, const FText& NewLabel)
{
	if (UObjectiveMarkerWidget* Marker = FindMarker(Id))
		Marker->UpdateLabel(NewLabel);
}

void UObjectiveMarkerLayer::HandleStateChanged(FName Id, EObjectiveState NewState)
{
	if (UObjectiveMarkerWidget* Marker = FindMarker(Id))
		Marker->UpdateState(NewState);
}
