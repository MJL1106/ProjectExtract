// UPingMarkerWidget implementation.

#include "UI/PingMarkerWidget.h"
#include "Components/Image.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Components/CompanionCommandComponent.h"

namespace
{
	/** Below these thresholds the WBP has nothing new to react to -- see the objective marker.
	 *  Ping-prefixed because the objective marker declares the same two constants in its own
	 *  anonymous namespace: under a unity build both files share one translation unit, so the
	 *  two anonymous namespaces are the same namespace and unprefixed names collide. */
	constexpr float PingAngleBroadcastEpsilon = 0.25f;
	constexpr float PingDistanceBroadcastEpsilon = 0.05f;
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
		return;
	}

	const bool bSameTarget = bHasTrackedWorldLocation
		&& FVector::DistSquared(NewLocation, LastTrackedWorldLocation) <= FMath::Square(TargetContinuityEpsilonCm);
	if (!bSameTarget)
	{
		bHasSmoothedPosition = false;
		bHasBroadcastState = false;
		bPendingAppearEvent = true;
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

	if (!FScreenMarkerViewContext::Build(this, ViewContext)) return;

	FScreenMarkerClampParams Params;
	Params.EdgeMargin = EdgeMargin;
	Params.ReentryHysteresis = ReentryHysteresis;
	Params.bWasOffScreen = bIsOffScreen;

	const FScreenMarkerProjection Projection = FScreenMarkerProjection::Project(
		ViewContext, WorldLocation, Params);
	if (!Projection.bIsValid) return;

	ApplyProjection(Projection, InDeltaTime);
}

void UPingMarkerWidget::ApplyProjection(const FScreenMarkerProjection& Projection, float DeltaTime)
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

	SetRenderTranslation(SmoothedPosition);
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
