// UObjectiveSubsystem — world-scope list of active objective markers for the HUD waypoint layer.
// Level scripting (or the placed AObjectiveMarkerActor) adds/removes objectives by id; the
// UObjectiveMarkerLayer widget rebuilds its markers on OnObjectivesChanged.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ObjectiveSubsystem.generated.h"

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

	/** Resolved marker position this frame. */
	FVector ResolveLocation() const
	{
		const AActor* Target = TargetActor.Get();
		return Target ? Target->GetActorLocation() : WorldLocation;
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnObjectivesChanged);

UCLASS()
class EXTRACTION_API UObjectiveSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Adds (or replaces, by id) an objective marker. Target optional — set to follow a moving actor. */
	UFUNCTION(BlueprintCallable, Category = "Objective")
	void AddObjective(FName Id, FText Label, FVector WorldLocation, AActor* TargetActor = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Objective")
	void RemoveObjective(FName Id);

	UFUNCTION(BlueprintCallable, Category = "Objective")
	void ClearObjectives();

	const TArray<FObjectiveMarker>& GetObjectives() const { return Objectives; }

	/** Broadcast on every add/remove/clear — the HUD layer rebuilds its marker widgets here. */
	UPROPERTY(BlueprintAssignable, Category = "Objective")
	FOnObjectivesChanged OnObjectivesChanged;

private:
	TArray<FObjectiveMarker> Objectives;
};
