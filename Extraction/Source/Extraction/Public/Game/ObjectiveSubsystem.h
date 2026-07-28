// UObjectiveSubsystem — world-scope list of active objective markers for the HUD waypoint layer
// and world-space billboard display actors. Level scripting (or the placed AObjectiveMarkerActor)
// adds/removes objectives by id; the layer/displays rebuild on OnObjectivesChanged.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ObjectiveSubsystem.generated.h"

class AObjectiveMarkerDisplay;

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

	/** Target-based markers resolve at the target's bounds BASE plus this height, so a floor
	 *  crate, a door and an enemy all read at the same marker height. Ignored for static
	 *  (no-target) markers. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	float HeightAboveBase = 170.f;

	/** When false the objective is text-only (HUD objective panel): no world-space billboard
	 *  and no off-screen edge indicator. Used for optional objectives. */
	UPROPERTY(BlueprintReadOnly, Category = "Objective")
	bool bShowWorldMarker = true;

	/** Resolved marker position this frame: target bounds-base + HeightAboveBase (or static
	 *  WorldLocation) plus the per-objective Offset. */
	FVector ResolveLocation() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnObjectivesChanged);

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
		FVector Offset = FVector::ZeroVector, bool bShowWorldMarker = true, float HeightAboveBase = 170.f);

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

private:
	TArray<FObjectiveMarker> Objectives;

	UPROPERTY()
	TSubclassOf<AObjectiveMarkerDisplay> MarkerDisplayClass;

	UPROPERTY()
	TArray<TObjectPtr<AObjectiveMarkerDisplay>> ActiveDisplays;

	void RebuildDisplayActors();
	void DestroyAllDisplays();
};
