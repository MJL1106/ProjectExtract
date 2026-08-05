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

	// Start hidden until a ping fires.
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

	// Sync to current state.
	HandlePingChanged(Comp->GetPendingCommand(), Comp->GetPingedTarget());
}

void UPingMarkerWidget::NativeDestruct()
{
	if (CachedCommandComp.IsValid())
	{
		CachedCommandComp->OnPingChanged.RemoveDynamic(this, &UPingMarkerWidget::HandlePingChanged);
	}

	Super::NativeDestruct();
}

void UPingMarkerWidget::HandlePingChanged(ECompanionCommand PendingCommand, AActor* /*PingedTarget*/)
{
	// Every path below re-arms tracking: a new ping must not inherit the previous one's smoothed
	// position, or the marker slides across the screen from wherever the last target was.
	bHasSmoothedPosition = false;
	bHasBroadcastState = false;
	bPendingAppearEvent = true;
	SetRenderOpacity(1.f);

	PingCommand = PendingCommand;

	if (PendingCommand == ECompanionCommand::None)
	{
		bPendingAppearEvent = false;
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

bool UPingMarkerWidget::GetTrackedLocation(FVector& OutLocation) const
{
	return CachedCommandComp.IsValid() && CachedCommandComp->GetPingWorldLocation(OutLocation);
}

void UPingMarkerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Collapsing is only ever driven by "there is no ping" -- Slate does not tick invisible widgets,
	// so anything this widget collapses itself for must be undone by an external event. Losing the
	// target qualifies; going off-screen does NOT, which is why that case fades instead.
	FVector WorldLocation = FVector::ZeroVector;
	if (!GetTrackedLocation(WorldLocation))
	{
		if (GetVisibility() != ESlateVisibility::Collapsed)
			SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

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
