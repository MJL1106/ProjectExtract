// Designer-tunable values for the companion AI follow / mirror / warp behaviour.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Companion/CompanionTypes.h"
#include "CompanionTuningDataAsset.generated.h"

USTRUCT(BlueprintType)
struct FCompanionPostureProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Formation")
	float FormationOffsetBack = 350.f;

	UPROPERTY(EditAnywhere, Category="Formation")
	float FormationOffsetRight = 200.f;

	UPROPERTY(EditAnywhere, Category="Formation")
	float AcceptableRadius = 250.f;

	UPROPERTY(EditAnywhere, Category="Formation")
	float SprintDistanceThreshold = 1000.f;

	UPROPERTY(EditAnywhere, Category="Formation", meta=(ClampMin="0.1", ClampMax="2.0"))
	float MaxFollowSpeedMultiplier = 1.f;

	UPROPERTY(EditAnywhere, Category="Scoring")
	float ScoringWeight_LoSPlayer = 1.f;

	UPROPERTY(EditAnywhere, Category="Scoring")
	float ScoringWeight_AvoidEnemy = 1.f;

	UPROPERTY(EditAnywhere, Category="Scoring")
	float ScoringWeight_CoverFromTarget = 1.f;
};

UCLASS(BlueprintType)
class EXTRACTION_API UCompanionTuningDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Companion|Posture")
	TMap<ECompanionPosture, FCompanionPostureProfile> PostureProfiles;

	// --- Follow / formation (migrated from BTTask_FollowPlayer) ---

	UPROPERTY(EditAnywhere, Category = "Companion|Formation")
	float FormationOffsetBack = 350.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Formation")
	float FormationOffsetRight = 200.f;

	// Floored at 50 so the derived follow min-separation can't collapse and silently disable the back-out.
	UPROPERTY(EditAnywhere, Category = "Companion|Formation", meta = (ClampMin = "50"))
	float AcceptableRadius = 250.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Formation")
	float SprintDistanceThreshold = 1000.f;

	// --- Mirror traversal ---

	// Master on/off for mirroring the player's climb/vault traversal. When false the companion
	// ignores the player's OnTraversalStarted events and relies solely on nav-link traversal.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror")
	bool bMirrorPlayerTraversalEnabled = false;

	// Ignore the player's traversal event if the companion is more than this far from the obstacle.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror")
	float MirrorTriggerRange = 1500.f;

	// Stand this many UU short of the obstacle before invoking TryStartTraversal.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror")
	float MirrorEdgeApproachOffset = 60.f;

	// Lateral (sideways) offset so the companion lands beside the player, not inside them.
	// Companion stays on whichever side it was already on relative to the traversal axis.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror", meta = (ClampMin = "0.0"))
	float MirrorLandingLateralOffset = 95.f;

	// Minimum centre-to-centre separation between companion and player after a mirrored traversal.
	// Overridden by the sum of both capsule radii if that is larger.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror", meta = (ClampMin = "0.0"))
	float MirrorPlayerClearance = 70.f;

	// Close-enough XY distance to the approach point to trigger the companion's own traversal.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror")
	float MirrorReachToleranceXY = 90.f;

	// Abort the mirror task and let the warp safety net handle recovery after this long.
	UPROPERTY(EditAnywhere, Category = "Companion|Mirror")
	float CatchUpTimeout = 4.0f;

	// --- Warp safety net ---

	// Master on/off for the teleport-to-player safety net. When false the companion never warps to the player.
	UPROPERTY(EditAnywhere, Category = "Companion|Warp")
	bool bWarpToPlayerEnabled = true;

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

	// --- Cover switching ---

	// Minimum time the companion must be at a slot before a switch can commit. (G1)
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.1"))
	float CoverSwitchMinDwell = 1.0f;

	// How often (seconds) the service re-evaluates cover candidates. (T4)
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.25"))
	float CoverSwitchReEvalInterval = 1.0f;

	// New slot must beat current slot score by this multiplier to commit a switch. (P6)
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float CoverSwitchScoreMargin = 1.2f;

	// (P4) seconds a just-vacated slot is excluded from re-selection by the pawn that left it.
	// 0.5 s is the practical floor for the anti-snap-back guard; below that the guard is effectively disabled.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.5"))
	float CoverSwitchPostVacateCooldown = 3.0f;

	// (G2) consecutive agreeing re-evals before a switch commits; 1 = most responsive, higher = more stable (anti-oscillation).
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "1"))
	int32 CoverSwitchRequiredAgreeingReEvals = 2;

	// Radius (cm) for companion cover searches — shared by the MoveToCover picker and the open-engage
	// re-seek so the two can't desync into a re-seek/can't-reach thrash.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "100.0"))
	float CoverSearchRadius = 1200.f;

	// Detection radius (cm) for 360° close-range threat awareness, independent of the sight cone.
	// Any enemy within this sphere with clear LoS is treated as a valid combat target even if
	// outside the forward 180° perception angle. Set to 0 to disable.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float ProximityAwarenessRadius = 700.f;

	// Companion avoids standing in the player's ADS firing line during combat.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Combat")
	bool bAvoidPlayerADSCone = true;

	// Half-angle (degrees) of the ADS firing cone the companion must stay out of. Minimum 1° — use bAvoidPlayerADSCone to fully disable.
	// Capped at 80° so the push target (clamped to 85°) always exceeds the entry angle; above that the keep-out would invert.
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "1", ClampMax = "80", EditCondition = "bAvoidPlayerADSCone"))
	float ADSConeHalfAngleDegrees = 20.f;

	// Extra clearance (degrees) past the cone edge added on top of the half-angle.
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "0", ClampMax = "45", EditCondition = "bAvoidPlayerADSCone"))
	float ADSConeClearanceMarginDegrees = 5.f;

	// Cone depth (cm); positions beyond this distance from the player are unconstrained.
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "0", EditCondition = "bAvoidPlayerADSCone"))
	float ADSConeRange = 1500.f;

	// --- Follow / formation min-separation ---

	// Hard floor (cm) — companion never rests closer; inside this distance it backs out.
	UPROPERTY(EditAnywhere, Category = "Companion|Formation", meta = (ClampMin = "0"))
	float FollowMinSeparation = 200.f;

	// Distance (cm) to back out to when inside FollowMinSeparation.
	UPROPERTY(EditAnywhere, Category = "Companion|Formation", meta = (ClampMin = "0"))
	float FollowIdleStandoff = 300.f;

	// Acceptance radius for the back-out move (cm). Kept small so the companion actually reaches the standoff.
	UPROPERTY(EditAnywhere, Category = "Companion|Formation", meta = (ClampMin = "10"))
	float FollowBackoutAcceptRadius = 50.f;
};
