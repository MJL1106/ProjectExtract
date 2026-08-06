// UObjectiveSubsystem — world-scope list of active objective markers for the HUD waypoint layer
// and world-space billboard display actors. Level scripting (or the placed AObjectiveMarkerActor)
// adds/removes objectives by id; the layer/displays rebuild on OnObjectivesChanged.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ObjectiveSubsystem.generated.h"

class AObjectiveMarkerDisplay;

/** Lifecycle of one objective line. Objectives used to vanish on completion (RemoveObjective was
 *  the only exit); a completed objective can now stay on the panel wearing its outcome instead. */
UENUM(BlueprintType)
enum class EObjectiveState : uint8
{
	NotTracked,
	Tracked,
	Succeeded,
	Failed
};

USTRUCT(BlueprintType)
struct FObjectiveMarker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	FName Id;

	/** Optional short label shown next to the icon (empty = icon + distance only). */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	FText Label;

	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	FVector WorldLocation = FVector::ZeroVector;

	/** Optional — when set, the marker follows this actor instead of the static location. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	TWeakObjectPtr<AActor> TargetActor;

	/** Additive offset applied after location resolution (e.g. XY nudge or extra Z on top of
	 *  HeightAboveBase for target-based markers). */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	FVector Offset = FVector::ZeroVector;

	/** Target-based markers resolve at the target's BASE plus this height, so a floor crate, a
	 *  door and an enemy all read at the same marker height. The base is the capsule bottom for
	 *  characters, the bounds bottom otherwise. Ignored for static (no-target) markers. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	float HeightAboveBase = 170.f;

	/** When false the objective is text-only (HUD objective panel): no world-space billboard
	 *  and no off-screen edge indicator. Used for optional objectives. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	bool bShowWorldMarker = true;

	/** Side objective — the HUD renders it as secondary. Independent of bShowWorldMarker: an
	 *  optional objective may still want a world billboard, and a text-only mandatory one exists. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	bool bOptional = false;

	/** Tracked vs NotTracked is set once, from bOptional, the first time AddObjective registers this
	 *  id. Past that it is owned by MarkObjectiveComplete alone — re-registering an EXISTING id (a
	 *  label rewrite, a marker move) must not resurrect a finished objective back to Tracked. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	EObjectiveState State = EObjectiveState::Tracked;

	/** The BASE POINT a target-following marker hangs off: the XY the marker sits over, at the Z of
	 *  the target's bottom. Characters take their horizontal from the actor location and their base
	 *  from the capsule bottom -- the capsule is the authority on where a character stands, and it
	 *  ignores held meshes that drag an actor-bounds centroid sideways. Other targets use actor
	 *  bounds. Callers add HeightAboveBase and any offset themselves. Sole writer of this rule --
	 *  anything that needs a target's marker anchor calls here rather than re-deriving it. */
	static FVector ResolveTargetBase(const AActor* Target);

	/** Resolved marker position this frame: ResolveTargetBase + HeightAboveBase (or static
	 *  WorldLocation) plus the per-objective Offset. */
	FVector ResolveLocation() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnObjectivesChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnObjectiveLabelChanged, FName, Id, const FText&, NewLabel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnObjectiveStateChanged, FName, Id, EObjectiveState, NewState);

UCLASS()
class EXTRACTION_API UObjectiveSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	/** Adds (or replaces, by id) an objective marker. Target optional — set to follow a moving actor.
	 *  Offset is additive on the resolved location (default zero). bShowWorldMarker=false makes the
	 *  objective text-only (no billboard, no edge indicator) — used for optional objectives.
	 *  HeightAboveBase applies to target-based markers only (see FObjectiveMarker). */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void AddObjective(FName Id, FText Label, FVector WorldLocation, AActor* TargetActor = nullptr,
		FVector Offset = FVector::ZeroVector, bool bShowWorldMarker = true, float HeightAboveBase = 170.f,
		bool bOptional = false);

	/** Rewrites an existing objective's HUD line and nothing else — no marker move, no display
	 *  respawn. This is the cheap path a live countdown needs: a defend beat re-labels itself once a
	 *  second, and routing that through AddObjective would re-resolve the marker target every tick.
	 *  Silent when the id is not registered or the label already reads the same. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void UpdateObjectiveLabel(FName Id, FText NewLabel);

	/** Marks an objective finished in place. The line stays on the panel wearing Succeeded/Failed
	 *  instead of disappearing — RemoveObjective is still the way to retire one outright.
	 *  Silent when the id is not registered or already holds that outcome. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void MarkObjectiveComplete(FName Id, bool bSucceeded = true);

	UFUNCTION(BlueprintCallable, Category = "Objective")
	void RemoveObjective(FName Id);

	UFUNCTION(BlueprintCallable, Category = "Objective")
	void ClearObjectives();

	const TArray<FObjectiveMarker>& GetObjectives() const { return Objectives; }

	/** Set the display actor class to spawn for world-space markers. Call once from the player
	 *  controller's local setup (the class is a UPROPERTY on the controller BP subclass). */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void SetMarkerDisplayClass(TSubclassOf<AObjectiveMarkerDisplay> InClass);

	/** Broadcast on every add/remove/clear — the HUD layer rebuilds its marker widgets here. */
	UPROPERTY(BlueprintAssignable, Category = "Objective")
	FOnObjectivesChanged OnObjectivesChanged;

	/** Narrow path for label-only mutations. Subscribers that only need to update text (the text
	 *  panel, the marker layer's widget labels) listen here and skip the full ClearChildren +
	 *  CreateWidget rebuild. A 1Hz countdown raises this instead of OnObjectivesChanged, so marker
	 *  smoothing state is never reset. */
	UPROPERTY(BlueprintAssignable, Category = "Objective")
	FOnObjectiveLabelChanged OnObjectiveLabelChanged;

	/** Same narrow-path reasoning as OnObjectiveLabelChanged: a completion recolours one line and
	 *  must not cost the panel a full rebuild. Raised on every genuine state transition. */
	UPROPERTY(BlueprintAssignable, Category = "Objective")
	FOnObjectiveStateChanged OnObjectiveStateChanged;

private:
	TArray<FObjectiveMarker> Objectives;

	UPROPERTY()
	TSubclassOf<AObjectiveMarkerDisplay> MarkerDisplayClass;

	UPROPERTY()
	TArray<TObjectPtr<AObjectiveMarkerDisplay>> ActiveDisplays;

	/** The world-marker ids as of the last rebuild that actually did work. A rebuild is a destroy +
	 *  respawn pass over every billboard in the level, and OnObjectivesChanged fires on every label
	 *  write — a 1Hz defend countdown would otherwise thrash the whole set once a second. */
	TSet<FName> LastDisplayedIds;

	void RebuildDisplayActors();
	void DestroyAllDisplays();

	/** Re-pushes a mutated marker into the display actor already showing it, if there is one. */
	void RefreshDisplayFor(const FObjectiveMarker& Objective);
};
