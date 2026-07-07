// AObjectiveMarkerActor — drop-in level actor that registers an objective waypoint at its own
// location. Alternative to calling UObjectiveSubsystem::AddObjective from level scripting.
// Destroy it (or call Deactivate) to remove the marker.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ObjectiveMarkerActor.generated.h"

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking, Collision, Rendering))
class EXTRACTION_API AObjectiveMarkerActor : public AActor
{
	GENERATED_BODY()

public:
	AObjectiveMarkerActor();

	/** Register the objective with the subsystem. Safe to call repeatedly (replace-by-id). */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void Activate();

	/** Remove the objective from the subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void Deactivate();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Unique objective id. Defaults to the actor's FName when left None. */
	UPROPERTY(EditAnywhere, Category = "Objective")
	FName ObjectiveId = NAME_None;

	/** Optional short label shown next to the icon. */
	UPROPERTY(EditAnywhere, Category = "Objective")
	FText Label;

	/** Register automatically on BeginPlay. Off = scripting calls Activate() when the objective goes live. */
	UPROPERTY(EditAnywhere, Category = "Objective")
	bool bActivateOnBeginPlay = true;

private:
	FName GetEffectiveId() const { return ObjectiveId != NAME_None ? ObjectiveId : GetFName(); }
};
