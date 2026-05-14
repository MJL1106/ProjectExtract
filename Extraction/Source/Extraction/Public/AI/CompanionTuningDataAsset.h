// Designer-tunable values for the companion AI follow / mirror / warp behaviour.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CompanionTuningDataAsset.generated.h"

UCLASS(BlueprintType)
class EXTRACTION_API UCompanionTuningDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Follow / formation (migrated from BTTask_FollowPlayer) ---

	UPROPERTY(EditAnywhere, Category = "Companion|Formation")
	float FormationOffsetBack = 350.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Formation")
	float FormationOffsetRight = 200.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Formation")
	float AcceptableRadius = 250.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Formation")
	float SprintDistanceThreshold = 1000.f;

	// --- Mirror traversal ---

	// Ignore the player's traversal event if the companion is more than this far from the obstacle.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror")
	float MirrorTriggerRange = 1500.f;

	// Stand this many UU short of the obstacle before invoking TryStartTraversal.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror")
	float MirrorEdgeApproachOffset = 60.f;

	// Close-enough XY distance to the approach point to trigger the companion's own traversal.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror")
	float MirrorReachToleranceXY = 90.f;

	// Abort the mirror task and let the warp safety net handle recovery after this long.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror")
	float CatchUpTimeout = 4.0f;

	// --- Warp safety net ---

	UPROPERTY(EditAnywhere, Category = "Companion|Warp")
	float WarpStuckTimeout = 6.0f;

	UPROPERTY(EditAnywhere, Category = "Companion|Warp")
	float WarpMinDistance = 2500.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Warp")
	float WarpBehindOffset = 300.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Warp")
	float WarpNavProjectExtent = 500.f;

	// Seconds since last render; below this the companion is considered visible (suppresses warp).
	UPROPERTY(EditAnywhere, Category = "Companion|Warp")
	float RecentlyRenderedTolerance = 0.5f;
};
