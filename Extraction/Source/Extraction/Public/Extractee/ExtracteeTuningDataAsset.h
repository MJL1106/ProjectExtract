// UExtracteeTuningDataAsset -- designer tunables for the rescue-and-follow extractee NPC.
// Assigned on the BP_Extractee child; when unassigned the character falls back to this CDO's defaults.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtracteeTuningDataAsset.generated.h"

UCLASS(BlueprintType)
class EXTRACTION_API UExtracteeTuningDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Follow ---

	// Acceptance distance behind the player. Deliberately larger than the companion's follow
	// distance so the extractee trails the whole party.
	UPROPERTY(EditAnywhere, Category = "Extractee|Follow", meta = (ClampMin = "0.0"))
	float FollowDistance = 450.f;

	// Extra slack beyond FollowDistance before a stopped extractee starts moving again
	// (hysteresis so it doesn't stutter-step at the boundary).
	UPROPERTY(EditAnywhere, Category = "Extractee|Follow", meta = (ClampMin = "0.0"))
	float FollowResumeSlack = 150.f;

	// --- Catch-up ---

	// Distance to the player beyond which any state (including cowering) breaks into a sprint catch-up.
	UPROPERTY(EditAnywhere, Category = "Extractee|CatchUp", meta = (ClampMin = "0.0"))
	float CatchUpStartDistance = 1500.f;

	// Catch-up ends once within this distance of the player.
	UPROPERTY(EditAnywhere, Category = "Extractee|CatchUp", meta = (ClampMin = "0.0"))
	float CaughtUpDistance = 600.f;

	// No path progress for this long during catch-up -> nav-projected teleport behind the player.
	UPROPERTY(EditAnywhere, Category = "Extractee|CatchUp", meta = (ClampMin = "0.5"))
	float CatchUpStuckTimeout = 4.f;

	// Minimum distance-to-player improvement (cm) that counts as catch-up progress.
	UPROPERTY(EditAnywhere, Category = "Extractee|CatchUp", meta = (ClampMin = "0.0"))
	float CatchUpProgressEpsilon = 50.f;

	// --- Combat reaction ---

	// A combat signal younger than this counts as "bullets are flying" and sends the extractee to cover/cower.
	UPROPERTY(EditAnywhere, Category = "Extractee|Combat", meta = (ClampMin = "0.0"))
	float CombatActiveWindow = 4.f;

	// Combat signal must be older than this before a cowering extractee resumes following.
	// Keep it larger than CombatActiveWindow (exit hysteresis).
	UPROPERTY(EditAnywhere, Category = "Extractee|Combat", meta = (ClampMin = "0.0"))
	float CombatQuietExit = 6.f;

	// Live enemies further than this from the extractee do not count as an active fight and are
	// never used as a cover threat -- prevents a leftover far-away skirmish pinning it in cower.
	UPROPERTY(EditAnywhere, Category = "Extractee|Combat", meta = (ClampMin = "0.0"))
	float CombatRelevanceRadius = 3000.f;

	// Cover slots are only considered within this radius; none in range -> cower in place.
	UPROPERTY(EditAnywhere, Category = "Extractee|Combat", meta = (ClampMin = "0.0"))
	float CoverSearchRadius = 800.f;

	// Added to the capsule radius for the behind-cover standoff position.
	UPROPERTY(EditAnywhere, Category = "Extractee|Combat", meta = (ClampMin = "0.0"))
	float CoverStandoffPadding = 12.f;

	// Close enough to the behind-cover position to count as arrived and start cowering.
	UPROPERTY(EditAnywhere, Category = "Extractee|Combat", meta = (ClampMin = "10.0"))
	float CoverArriveTolerance = 75.f;

	// --- Movement ---

	UPROPERTY(EditAnywhere, Category = "Extractee|Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 400.f;

	UPROPERTY(EditAnywhere, Category = "Extractee|Movement", meta = (ClampMin = "0.0"))
	float SprintSpeed = 600.f;

	// --- Soft collision (mirrors the companion self-push) ---

	// AddMovementInput scale applied when overlapping the player's or companion's capsule.
	UPROPERTY(EditAnywhere, Category = "Extractee|SoftCollision", meta = (ClampMin = "0.0"))
	float PushStrength = 1.f;

	// Extra personal-space padding (cm) on top of the combined capsule radii before the push kicks in.
	UPROPERTY(EditAnywhere, Category = "Extractee|SoftCollision", meta = (ClampMin = "0.0"))
	float PushPadding = 0.f;

	// --- Interaction ---

	// HUD prompt while looking at the un-rescued extractee.
	UPROPERTY(EditAnywhere, Category = "Extractee|Interaction")
	FText InteractPrompt = NSLOCTEXT("Extractee", "RescuePrompt", "Rescue");
};
