// UObjectiveMarkerLayer -- full-screen HUD layer that owns one UObjectiveMarkerWidget per active
// objective and resolves the shared camera/viewport data those markers project against. Subscribes
// to UObjectiveSubsystem and reconciles its canvas children by objective id. Added to the player
// screen by AExtractionPlayerController.
//
// This is a PC-owned top-level widget in absolute screen space, not a HUD module -- a module's
// anchor/offset plus HUD scale would displace every marker a second time.
//
// WBP must contain:
//   UCanvasPanel "MarkerCanvas" -- full-screen canvas the marker widgets are spawned into, left EMPTY

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/ObjectiveSubsystem.h"
#include "UI/ScreenMarkerProjection.h"
#include "ObjectiveMarkerLayer.generated.h"

class UCanvasPanel;
class UObjectiveMarkerWidget;
class UObjectiveSubsystem;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UObjectiveMarkerLayer : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Camera + viewport state for the current frame, rebuilt at most once per frame however many
	 *  markers ask for it. Check IsValid() before use -- it is empty on frames where the viewport
	 *  or the player controller could not be resolved. */
	const FScreenMarkerViewContext& GetFrameViewContext();

	/** Appends the slate-space screen position of every currently on-screen objective marker (resolved
	 *  position, bIsOffScreen false) to OutPositions. An edge-clamped chevron is pinned to the frame
	 *  edge, not something a ping needs to dodge, so off-screen markers are skipped. Appends rather
	 *  than clears — the caller (the ping marker) owns a persistent array across frames and reuses it
	 *  instead of allocating one per tick; reset the array yourself first if you want a clean read.
	 *  A pure read with no side effects, so it is safe to call on any frame, including mid-reconcile. */
	void GetOnScreenMarkerPositions(TArray<FVector2D>& OutPositions) const;

	/** Finds the marker whose resolved world location is within RadiusCm of WorldLocation (the closest
	 *  one, if several match), fires its OnObjectivePingFlash, and returns true. False when nothing is
	 *  near. Skips off-screen markers — an edge-clamped chevron is not what the player is looking at,
	 *  so a ping near a marker that has slid off-screen must not flash it or suppress the ping's own
	 *  locator. This is how a ping resolves "the player pinged the objective itself". A pure read plus
	 *  one Blueprint event fire — it never touches AddObjective/RemoveObjective, so it cannot re-enter
	 *  ReconcileMarkers (see the bReconciling/bReconcilePending latch on RebuildMarkers, below). */
	bool FlashMarkerNearWorldLocation(const FVector& WorldLocation, float RadiusCm);

	/** Pure-read counterpart to FlashMarkerNearWorldLocation, for re-checking an already-suppressed
	 *  ping every frame without re-firing the one-shot flash: true when an on-screen marker's resolved
	 *  world location is currently within RadiusCm of WorldLocation, same off-screen skip as above. */
	bool IsWorldLocationNearAnyMarker(const FVector& WorldLocation, float RadiusCm) const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- Bound widget (designer wires in WBP) ---

	/** Full-screen canvas the marker widgets are spawned into.
	 *  Must be left EMPTY in the WBP: every child is generated at runtime, and the reconcile pass
	 *  removes any child that is not a marker for a currently active objective — designer-authored
	 *  decoration parented here is deleted on the first rebuild. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> MarkerCanvas;

	/** Per-marker widget class (WBP_ObjectiveMarker) — assigned on the WBP layer subclass. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective")
	TSubclassOf<UObjectiveMarkerWidget> MarkerWidgetClass;

private:
	/** Subsystem-facing entry point. Owns the re-entrancy latch; the reconcile itself is below. */
	UFUNCTION()
	void RebuildMarkers();

	/** Reconciles the canvas against the subsystem's objective list, reusing widgets by id. Never
	 *  call directly — it runs Blueprint and must be entered through RebuildMarkers' latch. */
	void ReconcileMarkers();

	/** Targeted label update — avoids the full rebuild, preserving each marker's smoothing state. */
	UFUNCTION()
	void HandleLabelChanged(FName Id, const FText& NewLabel);

	/** Targeted completion update — same reasoning; also the only path by which a marker learns it
	 *  finished, since MarkObjectiveComplete deliberately raises nothing else. */
	UFUNCTION()
	void HandleStateChanged(FName Id, EObjectiveState NewState);

	UObjectiveMarkerWidget* FindMarker(FName Id) const;
	void SpawnMarker(const FObjectiveMarker& Objective);

	/** Shared search behind FlashMarkerNearWorldLocation and IsWorldLocationNearAnyMarker: the
	 *  on-screen marker (bIsOffScreen false) whose resolved world location is closest to WorldLocation,
	 *  among those within RadiusCm, or null if none qualify. Pure read — callers decide what to do
	 *  with the result (fire the flash, or just report true/false). */
	UObjectiveMarkerWidget* FindClosestOnScreenMarker(const FVector& WorldLocation, float RadiusCm) const;

	TWeakObjectPtr<UObjectiveSubsystem> CachedSubsystem;

	/** True while ReconcileMarkers is on the stack — see RebuildMarkers. */
	bool bReconciling = false;

	/** A reconcile was requested from inside one, and still owes a pass. */
	bool bReconcilePending = false;

	FScreenMarkerViewContext CachedViewContext;

	/** Frame the cached context was built on. MAX = never built. */
	uint64 CachedContextFrame = TNumericLimits<uint64>::Max();
};
