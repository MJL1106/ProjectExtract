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

	// How far dead astern of the player the follow anchor sits. Must clear BOTH companions'
	// formation lines (primary 350 back / 200 right, armed extractee 450 back / 200 left) or the
	// extractee comes to rest on one of their slots; 450 sat inside that line.
	UPROPERTY(EditAnywhere, Category = "Extractee|Follow", meta = (ClampMin = "0.0"))
	float FollowDistance = 700.f;

	// Extra slack beyond the follow anchor's acceptance ring before a stopped extractee starts
	// moving again (hysteresis so it doesn't stutter-step at the boundary). The resume threshold is
	// FollowDistance * 0.25 + this, so 75 means ~250cm of player travel -- about half a second --
	// before step-off. 150 pushed that past 300cm and read as the civilian ignoring the player.
	UPROPERTY(EditAnywhere, Category = "Extractee|Follow", meta = (ClampMin = "0.0"))
	float FollowResumeSlack = 75.f;

	// --- Catch-up ---

	// Distance to the player beyond which any state (including cowering) breaks into a sprint catch-up.
	UPROPERTY(EditAnywhere, Category = "Extractee|CatchUp", meta = (ClampMin = "0.0"))
	float CatchUpStartDistance = 1500.f;

	// Catch-up ends once within this distance of the player. Match it to FollowDistance -- exiting
	// closer just overshoots inward past the follow anchor and walks straight back out.
	UPROPERTY(EditAnywhere, Category = "Extractee|CatchUp", meta = (ClampMin = "0.0"))
	float CaughtUpDistance = 700.f;

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

	// Bounded on BOTH sides by the kit's player speeds (jog 410, hold-sprint 550):
	// * Must clear 410 with real margin or the follow gate is unreachable -- the anchor recedes faster
	//   than the extractee closes, so the gap only grows until CatchUpStartDistance fires a sprint, a
	//   permanent sawtooth with a ~10m visible trail. A thin margin (430) only closed on a long,
	//   straight, uncontested run: a 90-degree AI corner adds 20-40% local path length, and
	//   SoftPushAwayFrom steals from the forward component whenever the extractee overlaps anyone --
	//   both capped by this same MaxWalkSpeed, so the headroom has to absorb them.
	// * Must stay under 550 or a genuinely sprinting player never opens the gap that fires catch-up.
	UPROPERTY(EditAnywhere, Category = "Extractee|Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 500.f;

	// Bounded below by the catch-up needing to actually close, above by the squad's own sprint:
	// * 600 closed on a 550 hold-sprinting player at only 50cm/s -- CatchUpStartDistance 1500 down to
	//   CaughtUpDistance 700 took ~16s, and nothing broke the grind: 50cm/s still beats
	//   CatchUpProgressEpsilon roughly once a second, so LastCatchUpProgressTime keeps refreshing
	//   inside CatchUpStuckTimeout and the stuck-teleport never fires. 700 gives 150cm/s (5.3s) there,
	//   290cm/s against the 410 jog (2.8s), and stays positive up to ~27% corner path inflation.
	// * Must stay under the companion's SprintSpeed (850) or the civilian outruns the squad it trails.
	UPROPERTY(EditAnywhere, Category = "Extractee|Movement", meta = (ClampMin = "0.0"))
	float SprintSpeed = 700.f;

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
