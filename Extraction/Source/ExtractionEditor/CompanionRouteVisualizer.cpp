// FCompanionRouteVisualizer — see header.

#include "CompanionRouteVisualizer.h"
#include "Companion/CompanionRoute.h"
#include "Companion/CompanionRouteVisComponent.h"
#include "Companion/CompanionRouteTypes.h"
#include "SceneManagement.h"
#include "CanvasTypes.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "ScopedTransaction.h"

IMPLEMENT_HIT_PROXY(HCompanionRouteWaypointProxy, HComponentVisProxy)

// ---------------------------------------------------------------------------
// StanceLinearColor — matches ACompanionRoute::DrawDebugRoute's stance palette; white overrides
// when the waypoint is the one currently selected in the viewport.
// ---------------------------------------------------------------------------

static FLinearColor StanceLinearColor(ECompanionRouteStance Stance, bool bSelected)
{
	if (bSelected) return FLinearColor::White;

	switch (Stance)
	{
	case ECompanionRouteStance::Relaxed: return FLinearColor::Green;
	case ECompanionRouteStance::Alert:   return FLinearColor::Yellow;
	case ECompanionRouteStance::Crouch:  return FLinearColor(0.f, 1.f, 1.f);
	default:                             return FLinearColor::White;
	}
}

// ---------------------------------------------------------------------------
// DrawVisualization — spheres, leg lines, aim arrows
// ---------------------------------------------------------------------------

void FCompanionRouteVisualizer::DrawVisualization(
	const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	const UCompanionRouteVisComponent* VisComp = Cast<UCompanionRouteVisComponent>(Component);
	if (!IsValid(VisComp)) return;

	const ACompanionRoute* Route = Cast<ACompanionRoute>(VisComp->GetOwner());
	if (!IsValid(Route)) return;

	const int32 Num = Route->NumPoints();
	for (int32 i = 0; i < Num; ++i)
	{
		const FCompanionRouteWaypoint& WP = Route->GetWaypoint(i);
		const FVector WorldPt = Route->GetWorldPoint(i);
		const bool bSelected = (EditedRoute.Get() == Route) && (SelectedWaypointIndex == i);
		const FLinearColor Color = StanceLinearColor(WP.Stance, bSelected);

		PDI->SetHitProxy(new HCompanionRouteWaypointProxy(Component, i));
		DrawWireSphere(PDI, WorldPt, Color, WaypointSphereRadius, WaypointSphereSides, SDPG_Foreground);
		PDI->SetHitProxy(nullptr);

		if (i < Num - 1)
		{
			PDI->DrawLine(WorldPt, Route->GetWorldPoint(i + 1), Color, SDPG_World);
		}

		// Orange arrow to the resolved aim target (actor > local override > procedural
		// look-ahead), so an authored door focus is visible while placing waypoints.
		const FVector AimWorld = Route->GetWaypointAimWorld(i);
		const FVector ArrowDelta = AimWorld - WorldPt;
		if (!ArrowDelta.IsNearlyZero())
		{
			const FMatrix ArrowToWorld = FRotationMatrix::MakeFromX(ArrowDelta.GetSafeNormal()) * FTranslationMatrix(WorldPt);
			DrawDirectionalArrow(PDI, ArrowToWorld, FLinearColor(1.f, 0.5f, 0.f), ArrowDelta.Size(), AimArrowSize, SDPG_Foreground);
		}
	}
}

// ---------------------------------------------------------------------------
// DrawVisualizationHUD — index labels
// ---------------------------------------------------------------------------

void FCompanionRouteVisualizer::DrawVisualizationHUD(
	const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	const UCompanionRouteVisComponent* VisComp = Cast<UCompanionRouteVisComponent>(Component);
	if (!IsValid(VisComp) || !IsValid(GEngine)) return;

	const ACompanionRoute* Route = Cast<ACompanionRoute>(VisComp->GetOwner());
	if (!IsValid(Route)) return;

	for (int32 i = 0; i < Route->NumPoints(); ++i)
	{
		const FVector4 ScreenPos = View->WorldToScreen(Route->GetWorldPoint(i));
		if (ScreenPos.W <= 0.f) continue;

		FVector2D PixelLocation;
		if (!View->ScreenToPixel(ScreenPos, PixelLocation)) continue;

		const float DPIScale = Canvas->GetDPIScale();
		Canvas->DrawShadowedString(PixelLocation.X / DPIScale, PixelLocation.Y / DPIScale, *FString::FromInt(i), GEngine->GetSmallFont(), FLinearColor::White);
	}
}

// ---------------------------------------------------------------------------
// Selection / editing
// ---------------------------------------------------------------------------

bool FCompanionRouteVisualizer::VisProxyHandleClick(
	FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	if (!VisProxy || VisProxy->GetType() != HCompanionRouteWaypointProxy::StaticGetType()) return false;
	if (!VisProxy->Component.IsValid()) return false;

	const UCompanionRouteVisComponent* VisComp = Cast<UCompanionRouteVisComponent>(VisProxy->Component.Get());
	ACompanionRoute* Route = IsValid(VisComp) ? Cast<ACompanionRoute>(VisComp->GetOwner()) : nullptr;
	if (!IsValid(Route)) return false;

	EditedRoute = Route;
	SelectedWaypointIndex = static_cast<HCompanionRouteWaypointProxy*>(VisProxy)->WaypointIndex;
	return true;
}

void FCompanionRouteVisualizer::EndEditing()
{
	EditedRoute.Reset();
	SelectedWaypointIndex = INDEX_NONE;
}

bool FCompanionRouteVisualizer::GetWidgetLocation(
	const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	const ACompanionRoute* Route = EditedRoute.Get();
	if (!IsValid(Route) || !Route->Waypoints.IsValidIndex(SelectedWaypointIndex)) return false;

	OutLocation = Route->GetWorldPoint(SelectedWaypointIndex);
	return true;
}

bool FCompanionRouteVisualizer::HandleInputDelta(
	FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale)
{
	if (DeltaTranslate.IsNearlyZero()) return false;

	ACompanionRoute* Route = EditedRoute.Get();
	if (!IsValid(Route) || !Route->Waypoints.IsValidIndex(SelectedWaypointIndex)) return false;

	Route->Modify();
	const FVector LocalDelta = Route->GetActorTransform().InverseTransformVector(DeltaTranslate);
	Route->Waypoints[SelectedWaypointIndex].Location += LocalDelta;

	UCompanionRouteVisComponent* VisComp = Route->FindComponentByClass<UCompanionRouteVisComponent>();
	if (IsValid(VisComp))
	{
		FProperty* WaypointsProp = FindFProperty<FProperty>(ACompanionRoute::StaticClass(), GET_MEMBER_NAME_CHECKED(ACompanionRoute, Waypoints));
		NotifyPropertyModified(VisComp, WaypointsProp);
	}
	return true;
}

bool FCompanionRouteVisualizer::HandleInputKey(
	FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (Key != EKeys::Delete || Event != IE_Pressed) return false;

	ACompanionRoute* Route = EditedRoute.Get();
	if (!IsValid(Route) || !Route->Waypoints.IsValidIndex(SelectedWaypointIndex)) return false;

	const FScopedTransaction Transaction(NSLOCTEXT("CompanionRouteVisualizer", "DeleteRouteWaypoint", "Delete Route Waypoint"));
	Route->Modify();
	Route->Waypoints.RemoveAt(SelectedWaypointIndex);

	UCompanionRouteVisComponent* VisComp = Route->FindComponentByClass<UCompanionRouteVisComponent>();
	if (IsValid(VisComp))
	{
		FProperty* WaypointsProp = FindFProperty<FProperty>(ACompanionRoute::StaticClass(), GET_MEMBER_NAME_CHECKED(ACompanionRoute, Waypoints));
		NotifyPropertyModified(VisComp, WaypointsProp);
	}

	SelectedWaypointIndex = INDEX_NONE;
	return true;
}
