// AObjectiveMarkerActor — drop-in level actor that registers an objective waypoint at its own
// location. Alternative to calling UObjectiveSubsystem::AddObjective from level scripting.
//
// Completion (optional, either or both may be set — first one wins):
//   - Location: CompletionRadius > 0 — completes when the player enters the radius.
//   - Item:     RequiredKeycardId set — completes when the mission inventory records that card.
// On completion the marker removes itself, raises a HUD toast, and fires OnObjectiveCompleted
// (bindable in BP/level script to chain the next objective). With neither set, removal stays
// manual: destroy the actor, call Deactivate(), or RemoveObjective on the subsystem.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ObjectiveMarkerActor.generated.h"

class USphereComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnObjectiveCompleted);

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking, Collision, Rendering))
class EXTRACTION_API AObjectiveMarkerActor : public AActor
{
	GENERATED_BODY()

public:
	AObjectiveMarkerActor();

	/** Register the objective with the subsystem and arm any completion watchers.
	 *  Safe to call repeatedly (replace-by-id). */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void Activate();

	/** Remove the objective from the subsystem and disarm the completion watchers. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void Deactivate();

	/** Fired once when a completion criterion is met (after the marker has removed itself, so a
	 *  handler can immediately activate the next objective — even under the same id). */
	UPROPERTY(BlueprintAssignable, Category = "Objective")
	FOnObjectiveCompleted OnObjectiveCompleted;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Player-overlap trigger for location completion. Radius = CompletionRadius; collision
	 *  only enabled while active and CompletionRadius > 0. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Objective")
	TObjectPtr<USphereComponent> CompletionSphere;

	/** Unique objective id. Defaults to the actor's FName when left None. */
	UPROPERTY(EditAnywhere, Category = "Objective")
	FName ObjectiveId = NAME_None;

	/** Optional short label shown next to the icon. */
	UPROPERTY(EditAnywhere, Category = "Objective")
	FText Label;

	/** Register automatically on BeginPlay. Off = scripting calls Activate() when the objective goes live. */
	UPROPERTY(EditAnywhere, Category = "Objective")
	bool bActivateOnBeginPlay = true;

	// --- Completion criteria ---

	/** Location completion: player within this distance (cm) completes the objective. 0 = disabled. */
	UPROPERTY(EditAnywhere, Category = "Objective|Completion", meta = (ClampMin = "0.0"))
	float CompletionRadius = 0.f;

	/** Item completion: acquiring this keycard completes the objective. None = disabled. */
	UPROPERTY(EditAnywhere, Category = "Objective|Completion")
	FName RequiredKeycardId = NAME_None;

	/** Show a "Objective complete: <Label>" HUD toast on completion. */
	UPROPERTY(EditAnywhere, Category = "Objective|Completion")
	bool bToastOnComplete = true;

private:
	UFUNCTION()
	void OnCompletionSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnKeycardRecorded(FName KeycardId);

	/** Fires OnObjectiveCompleted + toast, then Deactivate(). Idempotent. */
	void Complete();

	FName GetEffectiveId() const { return ObjectiveId != NAME_None ? ObjectiveId : GetFName(); }

	bool bActive = false;
	bool bCompleted = false;
};
