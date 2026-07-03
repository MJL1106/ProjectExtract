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

	// Cover-commit gate: max distance (cm) to a cover point worth committing to. Candidates farther
	// than this are declined at commit time and the companion stand-fights instead. The old hand-placed
	// slots were sparse, so "no slot in range" implicitly meant stand-fight — the AICS bake coats every
	// wall, so worth-the-trip is an explicit policy now.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "100.0"))
	float CoverCommitMaxDistance = 650.f;

	// Cover-commit gate: only commit to cover while under fire (active near-miss suppression, or damage
	// within CoverCommitUnderFireWindow). False = distance gate only.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover")
	bool bCoverCommitRequiresUnderFire = true;

	// Window (s) for the damage half of the under-fire check. Near-miss suppression has its own
	// hysteresis and is checked independently.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float CoverCommitUnderFireWindow = 4.f;

	// When true, FindBestCoverFor rejects any slot whose hunkered body position is NOT shielded from
	// the target by world geometry. Slots that are off to the side or behind the companion relative
	// to the threat are discarded. Disable to revert to the old unfiltered pick.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover")
	bool bCoverRequiresBodyProtection = true;

	// Chest height (cm) used for the body-shield trace from the behind-cover position.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0"))
	float CoverProtectionChestHeight = 60.f;

	// Arc slack (degrees) added to CoverFlankArcHalfAngleDeg when testing whether the current cover
	// point has been flanked. Prevents freshly-entered points from immediately re-triggering.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0"))
	float CoverCompromiseArcSlackDeg = 15.f;

	// Half-angle (degrees) of the fire arc a cover point protects, measured from GetFireArcForward.
	// Default 60 matches the old per-slot FireArcDegrees=120 default (full arc = 2x half-angle).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0", ClampMax = "180"))
	float CoverFlankArcHalfAngleDeg = 60.f;

	// Consecutive evaluations where the slot is detected compromised before the companion breaks cover.
	// Higher = less reactive to transient flanks; 1 = immediate.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "1"))
	int32 CoverCompromiseDebounce = 2;

	// Target-agnostic starvation backstop: when the combat target keeps flipping faster than the
	// per-target debounce can accrue, this counter accrues across ALL targets whenever the current
	// target is out of the widened arc. When it reaches this threshold, the companion force-invalidates
	// (MarkVacated + BB clear) regardless of target identity. Higher = more tolerant of fast flips.
	// 0 disables the backstop entirely.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0", ClampMax = "16"))
	int32 CoverArcStarvationEvals = 4;

	// --- Multi-threat cover scoring ---

	// Score multiplier applied per EXTRA known threat (beyond the focused CombatTarget) whose clear
	// angle defeats a candidate cover point during the switch-monitor's candidate scoring. 1.0 = no
	// penalty (single-threat behaviour); lower = more strongly prefer covers that shield every threat.
	// Applied multiplicatively per uncovered extra threat, so N uncovered threats scale the score by
	// pow(MultiThreatExposurePenalty, N).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MultiThreatExposurePenalty = 0.5f;

	// Cap on how many of the closest known threats the cover scoring / validity checks test against
	// (bounds the added per-candidate trace cost). The focused CombatTarget is always tested; this caps
	// the total threat set considered. 1 = single-threat behaviour (only the focused target).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxThreatsForCoverScoring = 3;

	// Detection radius (cm) for 360° close-range threat awareness, independent of the sight cone.
	// Any enemy within this sphere with clear LoS is treated as a valid combat target even if
	// outside the forward 180° perception angle. Set to 0 to disable.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float ProximityAwarenessRadius = 700.f;

	// Radius (cm) around the player for alerted enemies that should make the companion ready its weapon.
	// Searching and Combat enemies count; only Combat enemies can become CombatTarget.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float PlayerThreatAwarenessRadius = 3500.f;

	// Hysteresis band for target retention: acquire within MaxEngageRange, drop only beyond
	// MaxEngageRange * this multiplier. Floor of 1.05 guarantees a minimum anti-flicker band
	// (~175cm at default 3500 range); 1.0 would make acquire and drop thresholds identical.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Combat", meta = (ClampMin = "1.05", ClampMax = "1.5"))
	float EngageRangeRetentionMultiplier = 1.15f;

	// When true, enemies that have detected the player and are within PlayerThreatAwarenessRadius
	// keep their combat target status through LoS blocks (the combat task repositions for an angle).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Combat")
	bool bRetainPlayerThreatTargetsWhileLosBlocked = true;

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
