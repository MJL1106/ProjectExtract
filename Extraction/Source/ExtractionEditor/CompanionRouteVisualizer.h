// Editor viewport visualizer for ACompanionRoute waypoints. Draws big stance-colored spheres in
// place of the tiny MakeEditWidget diamonds, supports click-select + gizmo drag + Delete, and
// draws an orange arrow to each waypoint's resolved aim target (actor / local override / procedural).

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class ACompanionRoute;

/** Hit-test proxy for a single waypoint sphere. Carries the waypoint index so VisProxyHandleClick
 *  knows which entry in Waypoints was clicked. */
struct HCompanionRouteWaypointProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY();

	HCompanionRouteWaypointProxy(const UActorComponent* InComponent, int32 InWaypointIndex)
		: HComponentVisProxy(InComponent, HPP_Wireframe)
		, WaypointIndex(InWaypointIndex)
	{}

	int32 WaypointIndex;
};

class FCompanionRouteVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	virtual void EndEditing() override;
	virtual bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
	virtual bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale) override;
	virtual bool HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;

private:
	/** Route currently being edited (has a waypoint selected). Null when nothing is selected. */
	TWeakObjectPtr<ACompanionRoute> EditedRoute;

	/** Index into EditedRoute->Waypoints currently selected, or INDEX_NONE. */
	int32 SelectedWaypointIndex = INDEX_NONE;

	static constexpr float WaypointSphereRadius = 40.f;
	static constexpr int32 WaypointSphereSides = 16;
	static constexpr float AimArrowSize = 12.f;
};
