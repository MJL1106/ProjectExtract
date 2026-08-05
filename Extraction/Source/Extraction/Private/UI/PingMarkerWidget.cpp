// UPingMarkerWidget implementation.

#include "UI/PingMarkerWidget.h"
#include "Components/Image.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Components/CompanionCommandComponent.h"
#include "Game/ExtractionPlayerController.h"
#include "UI/ObjectiveMarkerLayer.h"

namespace
{
	/** Below these thresholds the WBP has nothing new to react to -- see the objective marker.
	 *  Ping-prefixed because the objective marker declares the same two constants in its own
	 *  anonymous namespace: under a unity build both files share one translation unit, so the
	 *  two anonymous namespaces are the same namespace and unprefixed names collide. */
	constexpr float PingAngleBroadcastEpsilon = 0.25f;
	constexpr float PingDistanceBroadcastEpsilon = 0.05f;

	/** Below this, the avoidance shift is not worth reporting as "displaced" -- it is close enough to
	 *  the true point that a leader line would be a 1px flicker rather than a readable cue. Ping-
	 *  prefixed for the same unity-build reason as the two constants above. */
	constexpr float PingDisplacementEpsilon = 0.5f;
}

void UPingMarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Start hidden until a ping fires or the candidate probe finds something.
	SetVisibility(ESlateVisibility::Collapsed);

	APawn* Pawn = GetOwningPlayerPawn();
	if (!IsValid(Pawn)) return;

	UCompanionCommandComponent* Comp = Pawn->FindComponentByClass<UCompanionCommandComponent>();
	if (!IsValid(Comp)) return;

	CachedCommandComp = Comp;
	if (!Comp->OnPingChanged.IsAlreadyBound(this, &UPingMarkerWidget::HandlePingChanged))
	{
		Comp->OnPingChanged.AddDynamic(this, &UPingMarkerWidget::HandlePingChanged);
	}
	if (!Comp->OnPingCandidateChanged.IsAlreadyBound(this, &UPingMarkerWidget::HandlePingCandidateChanged))
	{
		Comp->OnPingCandidateChanged.AddDynamic(this, &UPingMarkerWidget::HandlePingCandidateChanged);
	}

	// Sync to current state -- covers a widget constructed after the probe already found something,
	// or after a ping was already committed, same reasoning as the HUD bridge's RefreshAll.
	HandlePingChanged(Comp->GetPendingCommand(), Comp->GetPingedTarget());
	HandlePingCandidateChanged(Comp->HasPingCandidate(), Comp->GetPingCandidateLocation());
}

void UPingMarkerWidget::NativeDestruct()
{
	if (CachedCommandComp.IsValid())
	{
		CachedCommandComp->OnPingChanged.RemoveDynamic(this, &UPingMarkerWidget::HandlePingChanged);
		CachedCommandComp->OnPingCandidateChanged.RemoveDynamic(this, &UPingMarkerWidget::HandlePingCandidateChanged);
	}

	Super::NativeDestruct();
}

void UPingMarkerWidget::HandlePingChanged(ECompanionCommand PendingCommand, AActor* /*PingedTarget*/)
{
	PingCommand = PendingCommand;

	// Un-collapse only when a ping actually committed -- clearing (None) falls through to
	// NativeTick's own per-frame check, which resolves the fallback candidate before deciding
	// whether anything is left to track. Force-collapsing here on None would kill the marker for a
	// player who cleared a ping while still aiming at a pingable target.
	if (PendingCommand == ECompanionCommand::None) return;

	if (GetVisibility() == ESlateVisibility::Collapsed)
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UPingMarkerWidget::HandlePingCandidateChanged(bool bHasCandidate, FVector /*WorldLocation*/)
{
	if (!bHasCandidate) return;

	if (GetVisibility() == ESlateVisibility::Collapsed)
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

bool UPingMarkerWidget::GetTrackedLocation(FVector& OutLocation, bool& bOutAiming) const
{
	UCompanionCommandComponent* Comp = CachedCommandComp.Get();
	if (!IsValid(Comp))
	{
		bOutAiming = false;
		return false;
	}

	// A committed ping always wins: EvaluatePingCandidate itself refuses to report a candidate while
	// one is pending, so the two never legitimately disagree, but resolving committed first keeps
	// this function correct even a frame ahead of that guarantee.
	if (Comp->GetPingWorldLocation(OutLocation))
	{
		bOutAiming = false;
		return true;
	}

	if (Comp->HasPingCandidate())
	{
		OutLocation = Comp->GetPingCandidateLocation();
		bOutAiming = true;
		return true;
	}

	bOutAiming = false;
	return false;
}

void UPingMarkerWidget::TrackLocation(const FVector& NewLocation, bool bHasLocation)
{
	if (!bHasLocation)
	{
		bHasTrackedWorldLocation = false;
		// The ping cleared -- EvaluatePlacedEdge's dedup must not carry over: a re-ping of the same
		// target placed right after this one clears has to see a false->true PLACED transition, not a
		// stale "already placed" left over from the ping that just ended.
		bLastPlaced = false;
		return;
	}

	const bool bSameTarget = bHasTrackedWorldLocation
		&& FVector::DistSquared(NewLocation, LastTrackedWorldLocation) <= FMath::Square(TargetContinuityEpsilonCm);
	if (!bSameTarget)
	{
		bHasSmoothedPosition = false;
		bHasBroadcastState = false;
		bPendingAppearEvent = true;
		// Unlike the other resets removed from this branch last round, this one IS load-bearing:
		// IssueCommand lets a fresh ping replace an in-flight one directly (target A -> target B with
		// no frame in between where bHasLocation is false), so TrackLocation's ping-cleared branch
		// never runs and never resets this latch on its own. Without the reset here, EvaluatePlacedEdge
		// sees bLastPlaced already true from ping A and never fires B's confirm pulse or objective check.
		bLastPlaced = false;
		SetRenderOpacity(1.f);
	}

	LastTrackedWorldLocation = NewLocation;
	bHasTrackedWorldLocation = true;
}

void UPingMarkerWidget::BroadcastAimingStateIfChanged(bool bAiming)
{
	if (bHasBroadcastAiming && bLastAiming == bAiming) return;

	bHasBroadcastAiming = true;
	bLastAiming = bAiming;
	OnPingAimingStateChanged(bAiming);
}

void UPingMarkerWidget::EvaluatePlacedEdge(bool bPlaced, const FVector& WorldLocation)
{
	const bool bJustPlaced = !bLastPlaced && bPlaced;
	bLastPlaced = bPlaced;

	if (!bJustPlaced) return;

	// A one-shot decision, made only here on the commit frame -- the flash itself must not re-fire
	// every tick. Whether the locator STAYS suppressed for the rest of this ping's life is a separate,
	// continuously re-evaluated question (see bIsObjectivePing's own comment / ApplyObjectiveAvoidance).
	UObjectiveMarkerLayer* Layer = ResolveObjectiveLayer();
	const bool bPingedObjective = IsValid(Layer) && Layer->FlashMarkerNearWorldLocation(WorldLocation, ObjectivePingRadius);

	// Suppressed entirely means entirely: the objective's own flash already told the player their
	// ping landed, so this widget's separate confirmation pulse would be a second, redundant cue.
	if (!bPingedObjective) OnPingConfirmed();
}

UObjectiveMarkerLayer* UPingMarkerWidget::ResolveObjectiveLayer()
{
	if (UObjectiveMarkerLayer* Cached = CachedObjectiveLayer.Get()) return Cached;

	const AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer());
	if (!IsValid(PC)) return nullptr;

	UObjectiveMarkerLayer* Layer = PC->GetObjectiveMarkerLayer();
	CachedObjectiveLayer = Layer;
	return Layer;
}

void UPingMarkerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Collapsing is only ever driven by "there is nothing to track" -- Slate does not tick invisible
	// widgets, so anything this widget collapses itself for must be undone by an external event
	// (HandlePingChanged / HandlePingCandidateChanged un-collapsing it). Losing the target qualifies;
	// going off-screen does NOT, which is why that case fades instead.
	FVector WorldLocation = FVector::ZeroVector;
	bool bAiming = false;
	const bool bHasLocation = GetTrackedLocation(WorldLocation, bAiming);

	// Ahead of the collapse branch: a lost target must reset the re-arm baseline too, or the next
	// thing this marker tracks -- even a genuinely different target -- could land within the
	// continuity epsilon of wherever this one was and wrongly skip its appear pulse.
	TrackLocation(WorldLocation, bHasLocation);

	if (!bHasLocation)
	{
		if (GetVisibility() != ESlateVisibility::Collapsed)
			SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	BroadcastAimingStateIfChanged(bAiming);
	EvaluatePlacedEdge(!bAiming, WorldLocation);

	if (!FScreenMarkerViewContext::Build(this, ViewContext)) return;

	FScreenMarkerClampParams Params;
	Params.EdgeMargin = EdgeMargin;
	Params.ReentryHysteresis = ReentryHysteresis;
	Params.bWasOffScreen = bIsOffScreen;

	const FScreenMarkerProjection Projection = FScreenMarkerProjection::Project(
		ViewContext, WorldLocation, Params);
	if (!Projection.bIsValid) return;

	ApplyProjection(Projection, WorldLocation, InDeltaTime);
}

void UPingMarkerWidget::ApplyProjection(const FScreenMarkerProjection& Projection, const FVector& WorldLocation, float DeltaTime)
{
	const bool bBoundaryCrossed = bIsOffScreen != Projection.bIsOffScreen;
	const bool bBearingReversed = bIsOffScreen && Projection.bIsOffScreen
		&& FVector2D::DotProduct(LastEdgeDirection, Projection.EdgeDirection) < 0.0;
	// On-screen the marker always snaps to the projected point -- interpolating it lags the world
	// during turns. Smoothing is reserved for off-screen edge sliding, where it still helps.
	const bool bSnap = !bHasSmoothedPosition || bBoundaryCrossed || bBearingReversed || !Projection.bIsOffScreen;

	// The head offset is an on-screen affordance: an edge-clamped marker has no head to sit above,
	// and applying it there would push the marker back outside the padded rectangle.
	FVector2D TargetPosition = Projection.ScreenPosition;
	if (!Projection.bIsOffScreen) TargetPosition.Y += VerticalScreenOffset;

	SmoothedPosition = bSnap
		? TargetPosition
		: FScreenMarkerProjection::InterpolatePosition(SmoothedPosition, TargetPosition,
			DeltaTime, PositionInterpSpeed);

	bHasSmoothedPosition = true;
	LastEdgeDirection = Projection.EdgeDirection;

	bIsOffScreen = Projection.bIsOffScreen;
	EdgeAngleDegrees = Projection.EdgeAngleDegrees;
	DistanceMeters = Projection.DistanceMeters;

	ApplyObjectiveAvoidance(SmoothedPosition, WorldLocation, DeltaTime, bSnap);

	SetRenderTranslation(DisplayedPosition);
	SetRenderOpacity(bIsOffScreen && !bClampWhenOffScreen ? 0.f : 1.f);

	if (bPendingAppearEvent)
	{
		bPendingAppearEvent = false;
		OnPingMarkerAppeared();
	}

	const bool bChanged = !bHasBroadcastState
		|| bLastBroadcastOffScreen != bIsOffScreen
		|| FMath::Abs(LastBroadcastAngle - EdgeAngleDegrees) > PingAngleBroadcastEpsilon
		|| FMath::Abs(LastBroadcastDistance - DistanceMeters) > PingDistanceBroadcastEpsilon;
	if (!bChanged) return;

	bHasBroadcastState = true;
	bLastBroadcastOffScreen = bIsOffScreen;
	LastBroadcastAngle = EdgeAngleDegrees;
	LastBroadcastDistance = DistanceMeters;
	OnPingMarkerUpdated(bIsOffScreen, EdgeAngleDegrees, DistanceMeters);
}

void UPingMarkerWidget::ApplyObjectiveAvoidance(const FVector2D& TruePosition, const FVector& WorldLocation, float DeltaTime, bool bSnap)
{
	UObjectiveMarkerLayer* Layer = ResolveObjectiveLayer();

	// Re-evaluated every frame against the CURRENT distance, not latched at commit -- see
	// bIsObjectivePing's own declaration. A target-following objective (escort/VIP) that walks away
	// from the ping stops suppressing the locator instead of staying stuck suppressed for the rest of
	// the ping's life. The one-shot flash decision (EvaluatePlacedEdge, fired once on commit) is
	// independent of this and must stay that way -- this call never fires anything.
	//
	// Asymmetric radius, same convention as ReentryHysteresis's on/off-screen test: ENTERING
	// suppression uses the narrow ObjectivePingRadius, LEAVING it requires clearing the wider
	// ObjectivePingRadius * ObjectivePingExitRadiusMultiplier. Both sides of this proximity check move
	// every tick, so without the gap an objective sitting near the boundary would flip the flag (and
	// the WBP's show/hide of the locator with it) every single frame on bounds-query noise alone.
	const float SuppressionRadius = bIsObjectivePing
		? ObjectivePingRadius * ObjectivePingExitRadiusMultiplier
		: ObjectivePingRadius;
	bIsObjectivePing = IsValid(Layer) && Layer->IsWorldLocationNearAnyMarker(WorldLocation, SuppressionRadius);

	// An objective-pinged ping shows no locator of its own -- the objective marker's flash is the
	// feedback -- so there is nothing here to keep clear of anything else.
	if (bIsObjectivePing)
	{
		AvoidanceOffset = FVector2D::ZeroVector;
		bIsDisplaced = false;
		LeaderLineOffset = FVector2D::ZeroVector;
		DisplayedPosition = TruePosition;
		return;
	}

	ScratchMarkerPositions.Reset();
	if (IsValid(Layer)) Layer->GetOnScreenMarkerPositions(ScratchMarkerPositions);

	// Same padded rectangle ApplyProjection's own clamp uses. A displaced position is TruePosition
	// (already inside this rect) PLUS an offset added afterward, so it needs its own pass through the
	// same bounds -- otherwise a locator displaced outward near an edge ends up rendered past it.
	const FVector2D RectMin(EdgeMargin, EdgeMargin);
	const FVector2D RectMax = ViewContext.ViewportSize - FVector2D(EdgeMargin, EdgeMargin);

	const FVector2D ClearPosition = ScratchMarkerPositions.IsEmpty()
		? TruePosition
		: ResolveClearPosition(TruePosition, ScratchMarkerPositions, RectMin, RectMax);

	// See AvoidanceOffset's declaration for why this eases the OFFSET rather than the absolute
	// position. bSnap forces the offset to jump instead of ease, matching every other reset case
	// ApplyProjection already defines (first frame, boundary crossed, bearing reversed, on-screen).
	const FVector2D TargetOffset = ClearPosition - TruePosition;
	AvoidanceOffset = bSnap
		? TargetOffset
		: FScreenMarkerProjection::InterpolatePosition(AvoidanceOffset, TargetOffset, DeltaTime, AvoidanceInterpSpeed);

	// Final safety clamp: ClearPosition already stays inside the rect (see ResolveClearPosition), but
	// TruePosition itself moves every frame while AvoidanceOffset eases toward its target from a PRIOR
	// frame's value, so the sum of the two can still land outside the rect mid-ease even though both
	// endpoints of that ease are inside it.
	const FVector2D UnclampedDisplayed = TruePosition + AvoidanceOffset;
	DisplayedPosition.X = FMath::Clamp(UnclampedDisplayed.X, RectMin.X, RectMax.X);
	DisplayedPosition.Y = FMath::Clamp(UnclampedDisplayed.Y, RectMin.Y, RectMax.Y);

	bIsDisplaced = !AvoidanceOffset.IsNearlyZero(PingDisplacementEpsilon);
	// From the FINAL clamped position, not the pre-clamp one -- a leader line computed from
	// UnclampedDisplayed would aim at empty space just past the edge instead of back at the locator
	// that is actually rendered on screen.
	LeaderLineOffset = TruePosition - DisplayedPosition;
}

FVector2D UPingMarkerWidget::ResolveClearPosition(const FVector2D& TruePosition, const TArray<FVector2D>& MarkerPositions,
	const FVector2D& RectMin, const FVector2D& RectMax) const
{
	const float ClearanceSquared = FMath::Square(ObjectiveClearanceRadius);
	auto IsClearOfAll = [&MarkerPositions, ClearanceSquared](const FVector2D& Candidate)
	{
		for (const FVector2D& MarkerPos : MarkerPositions)
			if (FVector2D::DistSquared(Candidate, MarkerPos) < ClearanceSquared) return false;
		return true;
	};

	auto IsInsideRect = [&RectMin, &RectMax](const FVector2D& Point)
	{
		return Point.X >= RectMin.X && Point.X <= RectMax.X && Point.Y >= RectMin.Y && Point.Y <= RectMax.Y;
	};

	if (IsClearOfAll(TruePosition)) return TruePosition;

	// Fixed order (left, right, up, down): every candidate sits exactly ObjectiveClearanceRadius from
	// TruePosition, so whenever more than one clears every marker, "shortest displacement" is a tie
	// among all of them -- resolving that tie by always preferring the earliest entry in a fixed list
	// is what keeps the locator from hopping between two equally-valid sides frame to frame. A
	// candidate that clears every marker but pokes outside the padded rect is skipped, not returned --
	// clamping it back in here could reintroduce the very overlap it was chosen to avoid.
	const FVector2D Candidates[] =
	{
		TruePosition + FVector2D(-ObjectiveClearanceRadius, 0.f),
		TruePosition + FVector2D(ObjectiveClearanceRadius, 0.f),
		TruePosition + FVector2D(0.f, -ObjectiveClearanceRadius),
		TruePosition + FVector2D(0.f, ObjectiveClearanceRadius),
	};

	for (const FVector2D& Candidate : Candidates)
		if (IsInsideRect(Candidate) && IsClearOfAll(Candidate)) return Candidate;

	// None of the four axis probes qualifies (boxed in by more than one marker, or every clear
	// direction runs off the rect) -- fall back to stepping directly away from the single nearest
	// marker. That is the one direction guaranteed to increase separation from the worst offender,
	// even if it cannot promise to clear the rest.
	const FVector2D* NearestMarker = nullptr;
	float NearestDistSquared = 0.f;
	for (const FVector2D& MarkerPos : MarkerPositions)
	{
		const float DistSquared = FVector2D::DistSquared(TruePosition, MarkerPos);
		if (NearestMarker && DistSquared >= NearestDistSquared) continue;

		NearestMarker = &MarkerPos;
		NearestDistSquared = DistSquared;
	}

	if (!NearestMarker) return TruePosition;

	FVector2D AwayDirection = TruePosition - *NearestMarker;
	if (!AwayDirection.Normalize()) AwayDirection = FVector2D(0.f, -1.f);

	// Arbitrary direction (away from whichever marker happens to be nearest), so unlike the four axis
	// candidates above this one is clamped into the rect rather than skipped -- there is nothing left
	// to fall back to.
	FVector2D FallbackPosition = TruePosition + AwayDirection * ObjectiveClearanceRadius;
	FallbackPosition.X = FMath::Clamp(FallbackPosition.X, RectMin.X, RectMax.X);
	FallbackPosition.Y = FMath::Clamp(FallbackPosition.Y, RectMin.Y, RectMax.Y);
	return FallbackPosition;
}
