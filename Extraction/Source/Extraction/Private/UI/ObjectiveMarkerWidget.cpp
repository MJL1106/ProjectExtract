// UObjectiveMarkerWidget implementation.

#include "UI/ObjectiveMarkerWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "UI/ObjectiveMarkerLayer.h"

namespace
{
	/** Movement below these thresholds is not worth a Blueprint call: a player standing still would
	 *  otherwise pay one OnMarkerStateUpdated per marker per frame for a chevron that never moved. */
	constexpr float AngleBroadcastEpsilon = 0.25f;
	constexpr float DistanceBroadcastEpsilon = 0.05f;

	/** Distance normalisation that survives a designer collapsing a range to zero width. */
	float SafeRangePct(float Value, float RangeMin, float RangeMax)
	{
		const float Span = RangeMax - RangeMin;
		if (Span <= UE_KINDA_SMALL_NUMBER) return Value >= RangeMax ? 1.f : 0.f;
		return FMath::Clamp((Value - RangeMin) / Span, 0.f, 1.f);
	}
}

void UObjectiveMarkerWidget::SetObjective(const FObjectiveMarker& InObjective, UObjectiveMarkerLayer* InLayer)
{
	Objective = InObjective;
	State = InObjective.State;
	OwningLayer = InLayer;

	bHasSmoothedPosition = false;
	bHasBroadcastState = false;
	bPendingAppearEvent = true;
	LastDisplayedMetres = -1;
	LastEdgeDirection = FVector2D(0.f, 1.f);

	PushLabelToWidget();
}

void UObjectiveMarkerWidget::RefreshObjective(const FObjectiveMarker& InObjective)
{
	const FText PreviousLabel = Objective.Label;
	const EObjectiveState PreviousState = State;

	Objective = InObjective;
	State = InObjective.State;

	if (!PreviousLabel.EqualTo(InObjective.Label))
	{
		PushLabelToWidget();
		if (IsConstructed()) OnObjectiveLabelUpdated(InObjective.Label);
	}

	if (PreviousState != State && IsConstructed())
		OnObjectiveStateUpdated(State);
}

void UObjectiveMarkerWidget::UpdateLabel(const FText& NewLabel)
{
	if (Objective.Label.EqualTo(NewLabel)) return;

	Objective.Label = NewLabel;
	PushLabelToWidget();
	if (IsConstructed()) OnObjectiveLabelUpdated(NewLabel);
}

void UObjectiveMarkerWidget::UpdateState(EObjectiveState NewState)
{
	if (State == NewState) return;

	State = NewState;
	Objective.State = NewState;
	if (IsConstructed()) OnObjectiveStateUpdated(NewState);
}

void UObjectiveMarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Never collapse or hide this widget from its own code -- Slate does not tick invisible widgets,
	// so a marker that hides itself never gets the frame that would bring it back. Opacity is the
	// only safe "gone" state here.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Initial sync: SetObjective runs before construction, so the BP could not have handled either.
	PushLabelToWidget();
	OnObjectiveLabelUpdated(Objective.Label);
	OnObjectiveStateUpdated(State);
}

void UObjectiveMarkerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const FScreenMarkerViewContext* Context = ResolveViewContext();
	if (!Context) return;

	FScreenMarkerClampParams Params;
	Params.EdgeMargin = EdgeMargin;
	Params.ReentryHysteresis = ReentryHysteresis;
	Params.bWasOffScreen = bIsOffScreen;

	const FScreenMarkerProjection Projection = FScreenMarkerProjection::Project(
		*Context, Objective.ResolveLocation(), Params);
	if (!Projection.bIsValid) return;

	ApplyProjection(Projection, InDeltaTime);
}

const FScreenMarkerViewContext* UObjectiveMarkerWidget::ResolveViewContext()
{
	// The layer resolves the camera and viewport once for the whole marker set; only a standalone
	// marker with no layer pays for its own.
	if (UObjectiveMarkerLayer* Layer = OwningLayer.Get())
	{
		const FScreenMarkerViewContext& Shared = Layer->GetFrameViewContext();
		return Shared.IsValid() ? &Shared : nullptr;
	}

	if (!FScreenMarkerViewContext::Build(this, LocalViewContext)) return nullptr;
	return &LocalViewContext;
}

void UObjectiveMarkerWidget::ApplyProjection(const FScreenMarkerProjection& Projection, float DeltaTime)
{
	// Snap instead of sliding in the three cases where interpolation would drag the marker visibly
	// across the screen: first appearance, an on/off-screen boundary crossing, and the bearing
	// reversal as the target passes exactly behind the player.
	const bool bBoundaryCrossed = bIsOffScreen != Projection.bIsOffScreen;
	const bool bBearingReversed = bIsOffScreen && Projection.bIsOffScreen
		&& FVector2D::DotProduct(LastEdgeDirection, Projection.EdgeDirection) < 0.0;
	const bool bSnap = !bHasSmoothedPosition || bBoundaryCrossed || bBearingReversed;

	SmoothedPosition = bSnap
		? Projection.ScreenPosition
		: FScreenMarkerProjection::InterpolatePosition(SmoothedPosition, Projection.ScreenPosition,
			DeltaTime, PositionInterpSpeed);

	bHasSmoothedPosition = true;
	LastEdgeDirection = Projection.EdgeDirection;

	bIsOffScreen = Projection.bIsOffScreen;
	EdgeAngleDegrees = Projection.EdgeAngleDegrees;
	DistanceMeters = Projection.DistanceMeters;

	// No re-clamp needed: both ends of the interpolation are inside the padded rectangle and the
	// rectangle is convex, so every smoothed point is too.
	SetRenderTranslation(SmoothedPosition);

	ApplyDistanceVisuals(DeltaTime, bSnap);
	PushDistanceToWidget();

	if (bPendingAppearEvent)
	{
		bPendingAppearEvent = false;
		OnMarkerAppeared();
	}

	const bool bChanged = !bHasBroadcastState
		|| bLastBroadcastOffScreen != bIsOffScreen
		|| FMath::Abs(LastBroadcastAngle - EdgeAngleDegrees) > AngleBroadcastEpsilon
		|| FMath::Abs(LastBroadcastDistance - DistanceMeters) > DistanceBroadcastEpsilon;
	if (!bChanged) return;

	bHasBroadcastState = true;
	bLastBroadcastOffScreen = bIsOffScreen;
	LastBroadcastAngle = EdgeAngleDegrees;
	LastBroadcastDistance = DistanceMeters;
	OnMarkerStateUpdated(bIsOffScreen, EdgeAngleDegrees, DistanceMeters);
}

void UObjectiveMarkerWidget::ApplyDistanceVisuals(float DeltaTime, bool bSnap)
{
	float TargetScale = MaxMarkerScale;
	float TargetOpacity = 1.f;

	// Scale and fade read as proximity, so by default they only apply on-screen -- an off-screen
	// chevron 200 m out still has to be legible.
	if (!bDistanceVisualsOnScreenOnly || !bIsOffScreen)
	{
		TargetScale = FMath::Lerp(MaxMarkerScale, MinMarkerScale,
			SafeRangePct(DistanceMeters, ScaleNearDistanceMeters, ScaleFarDistanceMeters));
		TargetOpacity = FMath::Lerp(1.f, MinMarkerOpacity,
			SafeRangePct(DistanceMeters, FadeStartDistanceMeters, FadeEndDistanceMeters));
	}

	if (bHideWhenOnScreen && !bIsOffScreen) TargetOpacity = 0.f;

	MarkerScale = bSnap ? TargetScale
		: FMath::FInterpTo(MarkerScale, TargetScale, DeltaTime, PositionInterpSpeed);
	MarkerOpacity = bSnap ? TargetOpacity
		: FMath::FInterpTo(MarkerOpacity, TargetOpacity, DeltaTime, PositionInterpSpeed);

	if (!bDriveRenderTransform) return;

	SetRenderScale(FVector2D(MarkerScale, MarkerScale));
	SetRenderOpacity(MarkerOpacity);
}

void UObjectiveMarkerWidget::PushLabelToWidget()
{
	if (!LabelText) return;

	LabelText->SetText(Objective.Label);
	LabelText->SetVisibility(Objective.Label.IsEmpty()
		? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

void UObjectiveMarkerWidget::PushDistanceToWidget()
{
	if (!DistanceText) return;

	// Only on the whole metre -- re-formatting an FText 60 times a second for a digit that changes
	// once is the kind of thing that shows up in a UI profile.
	const int32 Metres = FMath::RoundToInt(DistanceMeters);
	if (Metres == LastDisplayedMetres) return;

	LastDisplayedMetres = Metres;
	DistanceText->SetText(FText::FromString(FString::Printf(TEXT("%d m"), Metres)));
}
