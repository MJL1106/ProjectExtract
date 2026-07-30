// Designer-tunable values for the companion AI follow / mirror / warp behaviour.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Companion/CompanionTypes.h"
#include "Companion/CompanionCommandTypes.h"
#include "CompanionTuningDataAsset.generated.h"

class AEnemyGrenadeProjectile;
class UAnimMontage;

/** Hearing-noise emitted when the companion breaches a door with a given breach type.
 *  Loudness <= 0 or MaxRange <= 0 (or a missing map entry) = silent — Quiet's default. */
USTRUCT(BlueprintType)
struct FBreachNoiseProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (ClampMin = "0.0"))
	float Loudness = 1.f;

	/** Max range (cm) the noise reaches — passed to ReportNoiseEvent. */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (ClampMin = "0.0"))
	float MaxRange = 3000.f;
};

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

	/** Per-breach-type hearing noise (Loud = big radius, Tactical = small, Quiet = omit for silent).
	 *  Read by BTTask_CompanionBreach when the door swings. */
	UPROPERTY(EditAnywhere, Category="Companion|Breach")
	TMap<EBreachType, FBreachNoiseProfile> BreachNoise;

	/** Seconds from breach-montage start until the door actually swings (and the noise fires) —
	 *  tune to the montage's contact frame (kick lands / hand pushes). Missing entry = 0.4s. */
	UPROPERTY(EditAnywhere, Category="Companion|Breach", meta = (ClampMin = "0.0"))
	TMap<EBreachType, float> BreachOpenDelay;

	// --- Post-breach explore/loot/engage chain (skipped entirely in unbroken Stealth) ---

	/** Radius (cm) around the post-breach interior point scanned for lootable containers and, via
	 *  the engagement grant, within which unaware enemies become engageable. 0 disables the chain. */
	UPROPERTY(EditAnywhere, Category="Companion|PostBreach", meta = (ClampMin = "0.0"))
	float PostBreachExploreRadius = 900.f;

	/** Seconds the unaware-enemy engagement grant stays live after a commanded breach completes. */
	UPROPERTY(EditAnywhere, Category="Companion|PostBreach", meta = (ClampMin = "0.0"))
	float PostBreachEngagementDuration = 8.f;

	/** Radius (cm) around the searched room's interior point scanned for lootable containers and,
	 *  via the engagement grant, within which unaware enemies become engageable. */
	UPROPERTY(EditAnywhere, Category="Companion|Explore", meta = (ClampMin = "0.0"))
	float ExploreLootRadius = 1200.f;

	/** Seconds the unaware-enemy engagement grant stays live around a searched room. */
	UPROPERTY(EditAnywhere, Category="Companion|Explore", meta = (ClampMin = "0.0"))
	float ExploreEngagementDuration = 8.f;

	/** Seconds (min) the companion stands in a searched room that turned up nothing before
	 *  returning to follow. The actual dwell is rolled per search between min and max. */
	UPROPERTY(EditAnywhere, Category="Companion|Explore", meta = (ClampMin = "0.0"))
	float SearchDwellMin = 2.f;

	/** Seconds (max) the companion stands in an empty searched room before returning to follow. */
	UPROPERTY(EditAnywhere, Category="Companion|Explore", meta = (ClampMin = "0.0"))
	float SearchDwellMax = 4.f;

	// --- Movement speeds (applied by ACompanionCharacter; its EditDefaultsOnly members are the
	// fallback when no tuning asset is assigned) ---

	UPROPERTY(EditAnywhere, Category = "Companion|Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 550.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Movement", meta = (ClampMin = "0.0"))
	float SprintSpeed = 850.f;

	// Follow-task catch-up sprint cap — the full SprintSpeed read as too fast for closing formation
	// gaps. Rescue sprint-to-target and the stealth catch-up ladder keep full SprintSpeed.
	// 0 = no reduction (use SprintSpeed).
	UPROPERTY(EditAnywhere, Category = "Companion|Movement", meta = (ClampMin = "0.0"))
	float FollowCatchupSprintSpeed = 650.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Movement", meta = (ClampMin = "0.0"))
	float CrouchedWalkSpeed = 250.f;

	// Speed cap (cm/s) applied whenever the companion is STRAFING — moving while a Gameplay focus
	// holds the body facing something other than its direction of travel. 275 is not a feel number:
	// it is the Y value of the TOP DIRECTIONAL ROW of BS_Companion_Rifle02_Locomotion, whose Y axis is
	// raw cm/s (0 / 100 / 275 / 850). The 850 row has ONE sample and it is forward-only, so any speed
	// above 275 blends the legs into a forward sprint clip while the body faces the focus — the
	// companion appears to run forwards while side-stepping. Do NOT raise this without re-authoring
	// that blendspace's top row with a full directional set. Enemy parity: UEnemyArchetypeData's
	// StrafeWalkSpeed sits at the same tier (250).
	UPROPERTY(EditAnywhere, Category = "Companion|Movement", meta = (ClampMin = "0.0"))
	float StrafeMaxSpeed = 275.f;

	// Fast-crouch catch-up tier while stealth-active and falling behind: keeps the low silhouette
	// but hustles. Applied to MaxWalkSpeedCrouched while the follow task reports FastCrouch stage.
	UPROPERTY(EditAnywhere, Category = "Companion|Movement", meta = (ClampMin = "0.0"))
	float StealthCrouchCatchupSpeed = 400.f;

	// Standing-channel stealth speeds (F4a: Stealth no longer force-crouches — this is now the
	// default stealth stance). Applied to MaxWalkSpeed while stealth-active and standing.
	UPROPERTY(EditAnywhere, Category = "Companion|Movement", meta = (ClampMin = "0.0"))
	float StealthWalkSpeed = 300.f;

	// Standing-channel FastCrouch catch-up tier — the crouched-channel equivalent is
	// StealthCrouchCatchupSpeed above. Applied to MaxWalkSpeed while stealth-active, standing, and
	// the follow task reports FastCrouch stage.
	UPROPERTY(EditAnywhere, Category = "Companion|Movement", meta = (ClampMin = "0.0"))
	float StealthCatchupSpeed = 450.f;

	// Distance (cm) past which a stealth-active companion breaks crouch entirely and sprints to
	// catch up (the player is clearly sprinting off). Below it the fast-crouch tier applies.
	UPROPERTY(EditAnywhere, Category = "Companion|Movement", meta = (ClampMin = "0.0"))
	float StealthSprintBreakDistance = 1500.f;

	// Master switch for the stealth sprint-break stage. Off = fast-crouch is the only catch-up.
	UPROPERTY(EditAnywhere, Category = "Companion|Movement")
	bool bStealthAllowSprintCatchup = true;

	// Player 2D speed (cm/s) above which the follow task treats the player as sprinting and mirrors
	// sprint. The kit BP owns player sprint state, so it is inferred from velocity, and it must sit
	// above the kit's jog base (410) and below its hold-sprint (550 since the sprint/slide rework).
	// KNOWN INERT at 700: the player can never reach it, so the player-side sprint mirror never goes
	// true and the companion sprints on the raw distance threshold alone. Left as-is deliberately —
	// the current follow read is the shipped feel; changing it is tracked as its own task, not a
	// drive-by. Only consulted when the follow leader IS the player: a companion trailing the primary
	// companion reads its leader's sprint flag directly instead of inferring it from velocity.
	// FootstepNoiseComponent::SprintSpeedThreshold infers sprint from owner speed the same way and
	// sits at 480; that is the value to match if this is ever switched on. Do NOT re-derive from
	// AExtractionCharacter's 600/900 defaults — the live pawn is the kit player, and those stale
	// figures are where 700 came from.
	UPROPERTY(EditAnywhere, Category = "Companion|Movement", meta = (ClampMin = "0.0"))
	float PlayerSprintSpeedThreshold = 700.f;

	// --- Rescue-approach urgency (BTTask_FollowPlayer's sprint-to-target branch) ---
	// The approach used to sprint unconditionally, which read as panic even with 80 seconds of
	// bleedout left and the room empty. Pace is now re-decided every tick: sprint when ANY of the
	// three below trips, jog otherwise. All three are hysteretic in the task so a companion running
	// a boundary can't flicker the sprint flag (which drives both speed AND the anim's sprint state).

	// Bleedout seconds remaining at/below which the approach sprints. Sits well above
	// ACompanionCharacter::DesperationBleedoutThreshold (12s, the "commit through threats" line) —
	// this is "hurry up", that one is "go anyway".
	UPROPERTY(EditAnywhere, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveSprintBleedoutThreshold = 25.f;

	// Distance (cm) to the downed player above which the approach sprints regardless of the clock —
	// a long run is worth sprinting even on a full bleedout timer.
	UPROPERTY(EditAnywhere, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveSprintDistanceThreshold = 1200.f;

	// Distance (cm) from the DOWNED PLAYER to the nearest living hostile at/below which the approach
	// sprints. Fed by the state service's existing revive threat sweep (published on the companion as
	// GetNearestThreatToDownedPlayer) — never re-scanned here. Deliberately tighter than
	// ACompanionCharacter::ReviveThreatRadius: that radius gates whether reviving is SAFE, this one
	// only asks whether it is worth running.
	UPROPERTY(EditAnywhere, Category = "Companion|Revive", meta = (ClampMin = "0.0"))
	float ReviveSprintThreatRadius = 1000.f;

	// --- Stealth crouch-mirror (F4a: companion mirrors the player's own crouch/uncrouch, with a
	// randomized human-reaction delay, instead of Stealth force-crouching) ---

	UPROPERTY(EditAnywhere, Category = "Companion|StealthMirror", meta = (ClampMin = "0.0"))
	float StealthCrouchMirrorDelayMin = 0.35f;

	UPROPERTY(EditAnywhere, Category = "Companion|StealthMirror", meta = (ClampMin = "0.0"))
	float StealthCrouchMirrorDelayMax = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Companion|StealthMirror", meta = (ClampMin = "0.0"))
	float StealthUncrouchMirrorDelayMin = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Companion|StealthMirror", meta = (ClampMin = "0.0"))
	float StealthUncrouchMirrorDelayMax = 1.2f;

	// --- Follow / formation (migrated from BTTask_FollowPlayer) ---

	UPROPERTY(EditAnywhere, Category = "Companion|Formation")
	float FormationOffsetBack = 350.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Formation")
	float FormationOffsetRight = 200.f;

	// Extra back-offset applied to a non-primary companion (the armed extractee). The lateral offset
	// is already mirrored, so this only staggers the pair front-to-back — without it they sit as a
	// symmetric wall across the player's rear.
	UPROPERTY(EditAnywhere, Category = "Companion|Formation", meta = (ClampMin = "0.0"))
	float SecondaryFormationBackBias = 100.f;

	// Master switch for the follow CHAIN: a non-primary companion (the armed extractee VIP) trails
	// the PRIMARY companion using the same formation logic the primary runs against the player —
	// player <- companion <- VIP. Off restores the mirrored player-follow, where both companions
	// anchor on one shared player-keyed target (same input, same maths, same timing) and converge
	// into each other. No effect whatsoever on the primary, which always anchors on the player.
	UPROPERTY(EditAnywhere, Category = "Companion|Formation")
	bool bSecondaryFollowsPrimaryCompanion = true;

	// Leader-to-player 2D distance (cm) past which a secondary stops trailing the primary and follows
	// the player directly. Latched: re-acquired only once back inside this by
	// UBTTask_FollowPlayer::LeaderLeashReleaseHysteresis, or by half this distance when that band
	// would not fit. Must sit ABOVE SprintDistanceThreshold so ordinary stretchy following never
	// trips the handover, and BELOW WarpMinDistance so a primary commanded across the level hands the
	// secondary back to the player before the warp net teleports it. 0 disables the leash: the
	// secondary then trails the primary at any separation.
	UPROPERTY(EditAnywhere, Category = "Companion|Formation", meta = (ClampMin = "0.0"))
	float SecondaryLeaderLeashDistance = 2000.f;

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

	// Absolute score improvement required for a switch when the current cover scores <= 0. The
	// multiplicative margin INVERTS for non-positive scores (cur -0.55 x 1.2 = -0.66 lowers the
	// bar), which let nearly any candidate win every re-eval — constant cover churn.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.05"))
	float CoverSwitchMinScoreGain = 0.4f;

	// --- Compromise break (enemy flank-break parity) ---
	// When the held point no longer protects — threat outside the widened fire arc, body-shield
	// trace fails, or a known extra threat has the body exposed — the switch monitor bypasses the
	// dwell/margin/peek-cycle gates and relocates, or vacates to open-engage when no candidate
	// protects against the new threat picture.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch")
	bool bCoverCompromiseBreakEnabled = true;

	// Min seconds at the point before compromise evals run — a fresh arrival gets a beat to settle
	// before the monitor may declare it flanked.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.1"))
	float CoverCompromiseMinDwell = 0.75f;

	// Seconds between compromise evals — faster than CoverSwitchReEvalInterval so a flank is
	// noticed mid-dwell. Two consecutive positive evals confirm (anti single-frame flicker).
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.1"))
	float CoverCompromiseEvalInterval = 0.4f;

	// Seconds after taking a hit during which the shooter counts as an active flanker: a hit from
	// an enemy the current cover doesn't body-shield against breaks cover instantly (no debounce),
	// and that shooter steals target focus from a stickier/closer target. 0 disables both.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float FlankerResponseWindow = 2.5f;

	// Min seconds between body-charger target steals while the player is DBNO. Converging enemies
	// flap in/out of the revive threat ring — without hysteresis the steal fires several times a
	// second and the target thrashes (cover/aim/arc re-validate on every swap). 0 disables the cooldown.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float BodyChargerStealCooldown = 2.0f;

	// Min seconds between debounced (geometric) compromise breaks — a no-better-cover crossfire
	// churns at worst once per cooldown instead of hopping indefinitely. The exposed-side-hit
	// instant path bypasses this: fresh damage is fresh evidence (enemy fast-path parity).
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.0"))
	float CoverCompromiseBreakCooldown = 4.0f;

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

	// DEPRECATED — replaced by the cover-trigger model below (under fire is now ONE of the triggers,
	// not the sole gate). Field kept so existing assets don't lose data; no longer read.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover")
	bool bCoverCommitRequiresUnderFire = true;

	// Window (s) for the damage half of the under-fire trigger. Near-miss suppression has its own
	// hysteresis and is checked independently. Max 10: the companion's damage-timestamp ring prunes
	// at 20s and the release check doubles this window — larger values silently undercount.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float CoverCommitUnderFireWindow = 4.f;

	// --- Cover triggers (situational commit — replaces the under-fire-only gate) ---
	// The companion commits to the real cover system (cover anims) when ANY trigger is active:
	// under fire, low health, reloading/low ammo, or outnumbered. Otherwise it open-engages with
	// a loose cover bias (fights NEAR cover without locking into the cover animation loop).

	// Commit when health fraction drops below this.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverTriggerHealthFrac = 0.5f;

	// Release (leave cover) only once health recovers above this — hysteresis against pop-out thrash.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverTriggerHealthReleaseFrac = 0.65f;

	// Commit when magazine fraction drops below this (or while actively reloading).
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverTriggerLowAmmoFrac = 0.3f;

	// Release only once the magazine is back above this.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverTriggerAmmoReleaseFrac = 0.6f;

	// Commit when this many threats are known (sight-perceived, alive). 1-2 remaining enemies never
	// trigger the hide loop — the companion's accuracy is better used in the open.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "2", ClampMax = "8"))
	int32 CoverTriggerOutnumberedCount = 3;

	// Minimum time committed to cover before a cleared trigger allows exit.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0"))
	float CoverExitMinDwell = 2.0f;

	// --- Real-pressure under-fire (replaces the any-1-damage window check) ---

	// Suppression01 at/above this counts as under fire on its own — sustained near-miss pressure,
	// not a single graze. The suppression component's binary IsSuppressed latch is no longer consulted
	// for the commit trigger.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnderFireSuppressionFrac = 0.5f;

	// Real damage hits within CoverCommitUnderFireWindow required to count as under fire. The old
	// check tripped on ANY single hit in the window, which made the commit gate pass near-permanently
	// in a live firefight. Release side requires one fewer (hysteresis).
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "1", ClampMax = "8"))
	int32 UnderFireDamageHits = 2;

	// Release-side scale on the suppression fraction (hysteresis): in cover the trigger stays
	// active down to Frac x this, so a mid-fight decay dip can't pop the companion out and hop it
	// to a new point on the next burst.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float UnderFireSuppressionReleaseScale = 0.6f;

	// When low health is the ONLY active trigger, only commit if an available cover is within this
	// distance (cm) — never a long dash across open ground while low. 0 disables the gate.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0"))
	float LowHealthCoverMaxDash = 500.f;

	// --- Natural cycling (committed-time release back to mobile fighting) ---

	// After this long committed to one stretch of cover fighting, release back to open move-shoot
	// if pressure is only low-grade (see CoverNaturalReleasePressureFrac). 0 disables.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0"))
	float CoverMaxCommitTime = 12.f;

	// Minimum dwell after arriving at cover before the triggers-cleared exit may vacate. The commit
	// triggers are momentary (an under-fire window can lapse during the walk in), and ducking into
	// cover only to stand straight back up reads as broken. 0 restores the instant exit.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0"))
	float CoverMinCommitTime = 2.f;

	// Suppression01 at/above this counts as strong pressure and blocks the natural release (as do
	// low health and outnumbered-at-commit-count).
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverNaturalReleasePressureFrac = 0.75f;

	// After a natural release, block re-commit for this long unless fresh strong pressure arrives —
	// layered on CoverSwitchPostVacateCooldown so cycling can't degrade into cover-hop churn.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0"))
	float NaturalReleaseRecommitCooldown = 4.f;

	// --- Combat-mode stricter commit (Combat = mobile/aggressive, cover is the exception) ---

	// Master switch: in COMBAT mode the commit triggers require heavy pressure (below), reload/ammo
	// never commits, and outnumbered uses CombatOutnumberedCount.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers")
	bool bCombatModeStricterCommit = true;

	// Combat-mode under-fire needs suppression01 AND damage hits together (not either/or).
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CombatUnderFireSuppressionFrac = 0.7f;

	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "1", ClampMax = "8"))
	int32 CombatUnderFireDamageHits = 3;

	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "2", ClampMax = "8"))
	int32 CombatOutnumberedCount = 4;

	// Combat-mode natural release override: cover touches stay brief (touch, peek, push on) even
	// when a mid-grade release trigger is still holding. Strong pressure still holds it in place.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverTriggers", meta = (ClampMin = "0.0"))
	float CombatCoverMaxCommitTime = 4.f;

	// --- Combat-mode advance hops (cover-to-cover pushes — mobile but cover-clever) ---

	// Master switch: in COMBAT mode, periodically bound to a cover point that gains ground toward
	// the threat — brief touch (peek/fire), quick release, back to move-shoot. Cover as movement,
	// not as a campsite.
	UPROPERTY(EditAnywhere, Category = "Companion|CombatHops")
	bool bCombatAdvanceHops = true;

	// Cooldown (s) between hops.
	UPROPERTY(EditAnywhere, Category = "Companion|CombatHops", meta = (ClampMin = "1.0"))
	float CombatAdvanceHopInterval = 3.5f;

	// A hop candidate must be at least this much closer (cm) to the threat than the companion is.
	UPROPERTY(EditAnywhere, Category = "Companion|CombatHops", meta = (ClampMin = "50.0"))
	float CombatAdvanceHopMinGain = 250.f;

	// Max dash length (cm) for one hop — short bounds, never a cross-map sprint.
	UPROPERTY(EditAnywhere, Category = "Companion|CombatHops", meta = (ClampMin = "100.0"))
	float CombatAdvanceHopMaxDistance = 800.f;

	// Hop leash: the companion may not bound to a point further than this (cm) from the player. Sits
	// UNDER CombatLeashDistance and only ever tightens it (the hop takes the min of the two), so a
	// Combat companion pushing hard still ends up roughly one room from the player rather than the
	// leash's full 2250. Raising this above CombatLeashDistance does nothing.
	UPROPERTY(EditAnywhere, Category = "Companion|CombatHops", meta = (ClampMin = "300.0", EditCondition = "bCombatAdvanceHops"))
	float CombatAdvanceHopMaxPlayerDistance = 1400.f;

	// Seconds after a confirmed companion kill during which ONE hop may skip the interval cooldown —
	// the free repositioning bound that reads as "took the shot, took the ground". Self-consuming:
	// the bypass fires once per kill, so a kill cannot chain bounds and the hop timer keeps doubling
	// as the back-off for failed candidate scans. 0 disables the bypass.
	UPROPERTY(EditAnywhere, Category = "Companion|CombatHops", meta = (ClampMin = "0.0", EditCondition = "bCombatAdvanceHops"))
	float CombatPostKillAdvanceWindow = 3.f;

	// Combat-mode override of LooseCoverBiasWeight: the move-shoot jiggle hugs obstacles harder —
	// fighting BEHIND things while mobile instead of wandering open ground.
	UPROPERTY(EditAnywhere, Category = "Companion|CombatHops", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float CombatLooseCoverBiasWeight = 2.0f;

	// --- Combat-mode in-cover confidence (peek more often, expose longer) ---

	// Scales the whole between-peek wait (idle dwell + rolled cooldown) while in COMBAT mode.
	// 0.6 = peeks come ~40% sooner. Applied at the single wait-gate, so every cooldown source
	// (holds, stay-downs, arrivals) shortens uniformly.
	UPROPERTY(EditAnywhere, Category = "Companion|CombatConfidence", meta = (ClampMin = "0.2", ClampMax = "1.0"))
	float CombatPeekCooldownMultiplier = 0.6f;

	// Stretches cover peek burst clocks (stand/quick/corner) while in COMBAT mode — longer
	// exposure per peek. Applied as a slower countdown at the burst-clock decrements.
	UPROPERTY(EditAnywhere, Category = "Companion|CombatConfidence", meta = (ClampMin = "1.0", ClampMax = "2.5"))
	float CombatBurstDurationMultiplier = 1.6f;

	// --- Defensive-mode in-cover confidence (the middle setting between Combat and Stealth) ---
	// Defensive is the ECompanionMode::Normal enumerator; only its designer-facing label changed.
	// Stealth is deliberately absent from both scales below and always runs them at 1.0.

	// Defensive's counterpart to CombatPeekCooldownMultiplier — a small shortening of the between-peek
	// wait so a Defensive companion under fire answers within about a second instead of holding
	// through a full cooldown. 1.0 restores the pre-mode-personality behaviour exactly.
	UPROPERTY(EditAnywhere, Category = "Companion|DefensiveConfidence", meta = (ClampMin = "0.2", ClampMax = "1.0"))
	float DefensivePeekCooldownMultiplier = 0.8f;

	// Defensive's counterpart to CombatBurstDurationMultiplier. Defaults to 1.0 (unchanged exposure) —
	// Defensive gets its responsiveness from the cooldown scale above, not from longer exposure.
	UPROPERTY(EditAnywhere, Category = "Companion|DefensiveConfidence", meta = (ClampMin = "1.0", ClampMax = "2.5"))
	float DefensiveBurstDurationMultiplier = 1.0f;

	// --- Loose cover bias (default open-engage positioning) ---

	// When picking open-engage micro-positions, prefer points with a cover point within this radius
	// (so a trigger firing mid-fight has a duck spot nearby). 0 disables the bias.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float LooseCoverBiasRadius = 500.f;

	// Strength of the cover-proximity preference among LoS-valid candidates: higher = strictly
	// prefers the cover-nearest point, lower = approaches a random LoS pick. 0 disables the bias
	// entirely (including the pull-back cover snap).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float LooseCoverBiasWeight = 1.0f;

	// Max |Z delta| (cm) between the companion and a cover candidate for the pick to count — cover
	// only shields on the floor you stand on, and the 3D bounds scans (and the EQS pick) span
	// floors: a slot one storey up scored "nearest" and sent the companion crouch-walking into the
	// open toward an unreachable point. Applied at the commit gate, the re-rank, the open-engage
	// re-seek, the switch monitor, and the downed retreat. 250 tolerates ramps/half-landings.
	// 0 disables.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float CoverPickMaxZDelta = 250.f;

	// --- Combat leash (decoupled from the follow-task SprintDistanceThreshold) ---

	// Max distance from the player the companion positions freely during a fight in COMBAT mode
	// before being pulled back. Bolder than Normal — it advances and takes space.
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "300.0"))
	float CombatLeashDistance = 2250.f;

	// Same leash for fights while in NORMAL (or Stealth-broken) mode — tighter, stays in your fight.
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "300.0"))
	float NormalCombatLeashDistance = 1500.f;

	// --- Cover-to-cover advance ("move up and take space") ---

	// Allow the switch monitor to advance to a nearer-to-threat cover. Combat mode advances freely;
	// Normal/Stealth require the player-advance gate below.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch")
	bool bCombatAllowAdvance = true;

	// Required score margin for an ADVANCE switch (candidate meaningfully nearer the threat and
	// body-protected). 1.0 = a sidegrade is enough to take ground; CoverSwitchScoreMargin still
	// applies to ordinary (non-advancing) switches.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float AdvanceScoreMargin = 1.0f;

	// Combat-mode advance margin override — Combat previously advanced on a sidegrade (1.0), which
	// with the force-commit bug read as cover-to-cover churn with no firing. Sidegrades no longer hop.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float CombatAdvanceScoreMargin = 1.35f;

	// Normal/Stealth advance gate: accumulated player ground-gain toward the threat (uu, decays
	// between monitor re-evals) required before a non-Combat-mode companion may take an ADVANCE
	// switch — the companion takes space with the player, not ahead of them.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.0"))
	float PlayerAdvanceGateDistance = 250.f;

	// Hard reject for relocate candidates within this 2D distance (cm) of a cover an ENEMY has
	// declared intent on (enemy MinHostileCoverDistance parity). 0 = off.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.0"))
	float MinHostileCoverDistance = 250.f;

	// Hard reject for relocate candidates within this 2D distance (cm) of a living enemy pawn —
	// never set up on a wall an enemy is standing at (enemy MinHostilePawnDistance parity). 0 = off.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.0"))
	float MinHostilePawnDistance = 300.f;

	// Hard reject for relocate candidates whose straight 2D line from the companion passes within
	// this distance (cm) of a known threat — a compromised companion must route AROUND the enemy
	// group, never charge through it to reach a scoring winner. Straight-line approximation of the
	// nav path (cheap, per-candidate). 0 = off.
	UPROPERTY(EditAnywhere, Category = "Companion|CoverSwitch", meta = (ClampMin = "0.0"))
	float RelocatePathThreatClearance = 350.f;

	// When true, FindBestCoverFor rejects any slot whose hunkered body position is NOT shielded from
	// the target by world geometry. Slots that are off to the side or behind the companion relative
	// to the threat are discarded. Disable to revert to the old unfiltered pick.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover")
	bool bCoverRequiresBodyProtection = true;

	// Chest height (cm) used for the body-shield trace from the behind-cover position.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0"))
	float CoverProtectionChestHeight = 60.f;

	// Lateral corner gap (cm) for edge-aligned cover arrival — the same alignment enemies get from
	// their archetype DA. Without it companions sit mid-wall, endpoint covers resolve Front
	// (over-top only: no corner peeks, coin-flip idle side).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float CoverCornerGap = 5.f;

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

	// Half-angle (degrees) of the peek fire cone about the cover's wall-forward, origin at the pawn.
	// Gates burst start AND continuing fire — the companion never fires at bearings its cover pose
	// can't reach (mirrors the enemy's CoverPeekConeHalfAngleDeg; keep the two in step with the
	// anim-side CoverAimTrackLimitDeg so "torso looks" ⇔ "fire allowed"). 180 disables.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "10", ClampMax = "180"))
	float CoverPeekConeHalfAngleDeg = 75.f;

	// DEPRECATED: subsumed by the crouch-peek wallhack scorer (bCrouchPeekWallhackEnabled).
	// The scorer's per-option weighting replaces this flat chance. Field kept so existing data
	// assets don't lose the value; no longer read when the wallhack is enabled. Enemy usage untouched.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverEndpointStandPeekChance = 0.3f;

	// --- Crouch-peek wallhack scorer (Part A) ---

	// Master toggle: when false the legacy crouch roll runs (flat weighted-random). When true the
	// scorer evaluates CornerL/R and OverTop by LOS and extra-threat exposure.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover")
	bool bCrouchPeekWallhackEnabled = true;

	// Base score for a corner-peek option at normal health.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float CrouchPeekCornerBaseScore = 50.f;

	// Base score for an over-top option at normal health.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float CrouchPeekOverTopBaseScore = 50.f;

	// Bonus added to corner options so corners win ties with over-top.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float CrouchPeekCornerTieBonus = 20.f;

	// Per-extra-threat penalty when that threat has clear LOS to the candidate eye.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float CrouchPeekExtraThreatPenalty = 8.f;

	// Floor so any viable option stays rollable.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "1.0"))
	float CrouchPeekMinViableWeight = 5.f;

	// Max distance (cm) from the hunker position to the corner for it to count as reachable.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "50.0"))
	float CrouchPeekMaxCornerReachCm = 175.f;

	// Low-health corner base score.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float LowHpCrouchPeekCornerBaseScore = 25.f;

	// Low-health over-top base score.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float LowHpCrouchPeekOverTopBaseScore = 15.f;

	// --- Pressure-responsive cover (Part B) ---

	// Master toggle: when false all Part-B effects are identity (scale 1, no impulse).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover")
	bool bPressureResponsiveCover = true;

	// Distance (cm) at which Pressure01 reaches 0 (fully calm).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "100.0"))
	float PressureFarDistance = 2000.f;

	// Distance (cm) at which Pressure01 reaches 1 (maximum pressure).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "50.0"))
	float PressureNearDistance = 700.f;

	// Cooldown scale at maximum pressure (composed with Combat's CombatPeekCooldownMultiplier).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float PressureCooldownScaleAtMax = 0.5f;

	// Hold-weight scale at maximum pressure (pushed companion stops choosing to do nothing).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PressureHoldWeightScaleAtMax = 0.25f;

	// Burst-duration multiplier at maximum pressure (composed with Combat's CombatBurstDurationMultiplier).
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float PressureBurstDurationMultiplierAtMax = 1.3f;

	// A closing threat crossing inside this distance fires the peek-now impulse.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "100.0"))
	float PeekImpulseDistance = 900.f;

	// Seconds before the impulse can fire again after a band crossing.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.5"))
	float PeekImpulseRearmSeconds = 3.f;

	// --- Pressure fire term (incoming-fire contributes to Pressure01 alongside distance) ---
	// Being shot at raises pressure even when the nearest threat is far away — a firefight at 18m
	// no longer reads as calm just because nobody has closed. Gated by bPressureResponsiveCover.

	// Weight [0,1] for the suppression01 half of the fire term. 0 disables suppression's
	// contribution; 1 means full suppression01 equals full pressure on its own.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PressureSuppressionWeight = 1.f;

	// Recent damage hits (within CoverCommitUnderFireWindow) that equal maximum fire-term
	// pressure. Fewer hits contribute proportionally. Clamped >= 1 to prevent division by zero.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "1", ClampMax = "8"))
	int32 PressureDamageHitsForMax = 3;

	// A VISIBLE threat inside this range defeats cover logic entirely: in-cover releases to
	// open-engage (which move-shoots) and new cover commits are declined — no cat-and-mouse
	// laps around the same rock with a chaser on the companion's tail. 0 disables.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.0"))
	float PointBlankAbandonDistance = 450.f;

	// Fire at the visible combat target while walking to a committed cover point (muzzle-gated,
	// 10 Hz). Off = the old silent run-to-cover.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover")
	bool bCoverApproachFireWhileMoving = true;

	// Completed peek cycles required at a cover point before the companion may reposition/shuffle
	// away from it (enemy parity — commit to a point before wandering). The starvation backstop and
	// genuine invalidates (occupied, blind) ignore this.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MinPeekCyclesBeforeRelocate = 1;

	// Scales the between-peek wait for the FIRST peek only (no peek cycles completed at this point
	// yet). Taking fresh cover under fire and then standing behind it through a full rolled cooldown
	// is the single loudest "the companion does nothing" tell; the first answer wants to land in
	// about a second. Composes with the mode and pressure cooldown scales. 1.0 disables.
	// STEALTH IS EXCLUDED — it is ring-fenced from every mode-personality lever, so a broken-stealth
	// firefight keeps its historic peek cadence. Applies to Defensive and Combat only.
	UPROPERTY(EditAnywhere, Category = "Companion|Cover", meta = (ClampMin = "0.25", ClampMax = "1.0"))
	float FirstPeekWaitScale = 0.5f;

	// Suppression resistance divisor for near-miss buildup (enemy-DA parity: higher = harder to
	// suppress). The component default (1.0) pins the companion after just two near-misses — under
	// several shooters it stays suppressed permanently, never fires, and reads as cover churn.
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float SuppressionResistance = 2.5f;

	// While true, suppression NEVER gates the companion's combat task — in cover it just peeks
	// (peek gate, strike freeze, reposition aborts all read unsuppressed). Near-miss FX and the
	// suppression component itself are unaffected. User-directed test lever, default ON.
	UPROPERTY(EditAnywhere, Category = "Companion|Combat")
	bool bIgnoreSuppressionInCover = true;

	// --- Angle-seek (get an angle on enemies hard-focusing the player) ---

	// Master switch. When 2+ enemies are focused on the PLAYER and the companion is unpressured,
	// it actively seeks a firing angle on the attackers instead of holding its current spot.
	UPROPERTY(EditAnywhere, Category = "Companion|AngleSeek")
	bool bEnableAngleSeek = true;

	// Enemies simultaneously targeting the player required to arm angle-seek.
	UPROPERTY(EditAnywhere, Category = "Companion|AngleSeek", meta = (ClampMin = "1", ClampMax = "8"))
	int32 AngleSeekMinFocusedEnemies = 2;

	// "Not being shot at" window (s): no damage within this AND suppression01 below
	// AngleSeekPressureFrac = the companion is free to work an angle.
	UPROPERTY(EditAnywhere, Category = "Companion|AngleSeek", meta = (ClampMin = "0.0"))
	float AngleSeekSmallWindow = 1.0f;

	// Suppression01 ceiling to arm angle-seek; rising above it mid-seek aborts (the flank drew fire —
	// pressure is off the player, mission accomplished).
	UPROPERTY(EditAnywhere, Category = "Companion|AngleSeek", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AngleSeekPressureFrac = 0.3f;

	// Cooldown (s) between angle-seek activations — prevents kill-one-attacker re-arm ping-pong.
	UPROPERTY(EditAnywhere, Category = "Companion|AngleSeek", meta = (ClampMin = "0.0"))
	float AngleSeekCooldown = 5.f;

	// Lateral ORBIT RATE: cm added to the move-shoot drift anchor per drift cycle (~1.5s),
	// perpendicular to the target axis away from the player's firing line — the companion keeps
	// circling for the crossfire angle for as long as the seek stays active (up to AngleSeekMaxTime).
	// Combat mode scales this up (see multiplier below).
	UPROPERTY(EditAnywhere, Category = "Companion|AngleSeek", meta = (ClampMin = "0.0"))
	float AngleSeekLateralBias = 300.f;

	// Combat-mode multiplier on AngleSeekLateralBias — the aggressive open flank.
	UPROPERTY(EditAnywhere, Category = "Companion|AngleSeek", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float AngleSeekCombatBiasMultiplier = 1.5f;

	// Max duration (s) of one angle-seek before it stands down (also exits when the focus count
	// drops below AngleSeekMinFocusedEnemies).
	UPROPERTY(EditAnywhere, Category = "Companion|AngleSeek", meta = (ClampMin = "0.0"))
	float AngleSeekMaxTime = 6.f;

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

	// How long (s) an enemy stays engageable on the strength of having engaged the COMPANION —
	// damaged it, or holds live suspicion/search knowledge of it. Widens acquisition past the old
	// "has this enemy detected the PLAYER" test, so a Searching enemy that shot the companion and
	// lost it is answered instead of ignored. Never applied in Stealth (the acquisition call site
	// forces the clause false there), and it does not widen the player-threat scan, whose contract
	// is specifically "enemies pressuring the player". 0 = only a live target or a current sighting.
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "0.0"))
	float EngagedCompanionMemorySeconds = 4.f;

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

	// --- Post-combat overwatch (weapon held ready on the threat chokepoint after the last kill) ---

	// Master switch. Off = legacy behavior: weapon lowers the frame the last target is gone.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|PostCombatOverwatch")
	bool bEnablePostCombatOverwatch = true;

	// Restrict the hold to Director waves. Between squads it reads as covering the door the next one
	// comes through; after an ordinary room clear the same pose reads as staring at a corpse, and it
	// also holds the route facing off for up to PostCombatOverwatchMaxTime. Off restores the old
	// always-on hold without a rebuild. Only gates ENTRY: a hold already running when the wave ends
	// finishes on its own terms rather than snapping the weapon down on the last-kill frame.
	UPROPERTY(EditAnywhere, Category = "Companion|PostCombatOverwatch", meta = (EditCondition = "bEnablePostCombatOverwatch"))
	bool bOverwatchWaveOnly = true;

	// Ceiling (s) on the post-fight hold. The hold normally ends earlier — the player walking off,
	// a command, a new contact, or the companion itself starting a move all break it immediately.
	UPROPERTY(EditAnywhere, Category = "Companion|PostCombatOverwatch", meta = (ClampMin = "1.0", ClampMax = "20.0", EditCondition = "bEnablePostCombatOverwatch"))
	float PostCombatOverwatchMaxTime = 6.f;

	// Player distance (cm) beyond which the hold breaks — the player has moved on, follow wins.
	UPROPERTY(EditAnywhere, Category = "Companion|PostCombatOverwatch", meta = (ClampMin = "200.0", EditCondition = "bEnablePostCombatOverwatch"))
	float OverwatchBreakDistance = 800.f;

	// Radius (cm, 2D) around the companion searched for recent enemy corpses. Their centroid is the
	// first threat reference the hold tries: the pile of enemies that just died marks where the fight
	// physically happened, and a body cannot have walked somewhere the ally has no sight of. Only
	// corpses killed during the CURRENT fight are counted (pre-fight bodies are filtered out), and
	// a separate Z-delta gate stops a 3D radius spanning floors. 0 disables the corpse source and
	// falls straight through to the last-threat/first-contact chain.
	UPROPERTY(EditAnywhere, Category = "Companion|PostCombatOverwatch", meta = (ClampMin = "0.0", UIMin = "500.0", EditCondition = "bEnablePostCombatOverwatch"))
	float OverwatchCorpseSearchRadius = 2000.f;

	// Max |Z delta| (cm) between the companion and a corpse for it to count. DemoMap is a stacked
	// skyscraper, so a pure 3D radius spans several storeys -- 4 enemies killed on floor 2 plus 2
	// on floor 1 drag the centroid Z into the slab. Same lesson as FollowMaxZDelta, CoverPickMaxZDelta
	// and RouteFacingMaxZDelta. 0 disables the storey gate.
	UPROPERTY(EditAnywhere, Category = "Companion|PostCombatOverwatch", meta = (ClampMin = "0.0", EditCondition = "bEnablePostCombatOverwatch"))
	float OverwatchCorpseMaxZDelta = 250.f;

	// Max pairwise 2D separation (cm) among qualifying corpses before the centroid is rejected in
	// favour of the single NEAREST corpse. A centroid of two separate clusters always lands in the
	// wall between them -- e.g. 3 corpses in Room A and 2 in Room B produce a centroid inside the
	// partition. When any pair exceeds this threshold the nearest corpse alone is used, which always
	// sits in the room the companion is actually standing in. 0 disables the spread reject.
	UPROPERTY(EditAnywhere, Category = "Companion|PostCombatOverwatch", meta = (ClampMin = "0.0", EditCondition = "bEnablePostCombatOverwatch"))
	float OverwatchCorpseMaxSpread = 1500.f;

	// Threat-memory points closer than this (cm) are skipped (the fight ended at the companion's
	// feet); the aim falls back to first-contact, then to the pure bearing.
	UPROPERTY(EditAnywhere, Category = "Companion|PostCombatOverwatch", meta = (ClampMin = "100.0", EditCondition = "bEnablePostCombatOverwatch"))
	float OverwatchMinThreatDist = 400.f;

	// Aim distance (cm) along the threat bearing when no usable door or threat point exists.
	UPROPERTY(EditAnywhere, Category = "Companion|PostCombatOverwatch", meta = (ClampMin = "300.0", EditCondition = "bEnablePostCombatOverwatch"))
	float OverwatchBearingDistance = 1200.f;

	// Seconds between re-resolutions of the held aim point. The point was previously computed once on
	// entry and re-asserted verbatim, so it went stale as soon as the ally or the fight moved — and
	// under a wave hold, where the max-time cap is waived, it never expired to correct itself. A
	// refresh that repeatedly fails to resolve a point ends the hold outright. 0 disables the refresh.
	// ClampMin stays 0 so the disable still works, but the slider floors at 0.25 (the service's own
	// tick interval): anything finer just re-runs the door sort and its traces every single tick.
	UPROPERTY(EditAnywhere, Category = "Companion|PostCombatOverwatch", meta = (ClampMin = "0.0", UIMin = "0.25", EditCondition = "bEnablePostCombatOverwatch"))
	float OverwatchAimRefreshInterval = 1.f;

	// --- Wave hold (stay combat-ready at cover through a finite Director wave) ---

	// Master switch. Off = legacy behavior: allies return to formation in the gaps between squad
	// spawns, which reads as them wandering off mid-defence.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|WaveHold")
	bool bEnableWaveHold = true;

	// Player distance (cm) beyond which the wave hold breaks so the ally catches up and re-settles
	// at fresh cover. Deliberately far more generous than OverwatchBreakDistance: the whole point of
	// the hold is to stay put while the player repositions inside the defence area. Without a leash
	// at all an ally is stranded a room behind the moment the player pushes on.
	UPROPERTY(EditAnywhere, Category = "Companion|WaveHold", meta = (ClampMin = "500.0", EditCondition = "bEnableWaveHold"))
	float WaveHoldLeashDistance = 2500.f;

	// Seconds with NOTHING known (no combat target, no alerted threat) before the hold drops mid-wave.
	// Without it an ally that cleared its corner stands facing the last fight until the next squad
	// physically walks into its perception, which reads as frozen. 0 disables the release.
	UPROPERTY(EditAnywhere, Category = "Companion|WaveHold", meta = (ClampMin = "0.0", EditCondition = "bEnableWaveHold"))
	float WaveHoldQuietReleaseSeconds = 6.f;

	// Seconds of SUSTAINED knowledge needed to re-arm the hold after the quiet release above fired.
	// The quiet timer unwinds in real time rather than snapping to zero on the first knowing tick, and
	// saturates at WaveHoldQuietReleaseSeconds + this the moment the release trips. Without the
	// hysteresis a single tick of eye-line re-armed the hold, and re-arming fires StopMovement()
	// wherever the ally happens to be standing — an enemy flickering in and out of sight (an alerted
	// Searching enemy is accepted as a threat but skipped for acquisition in Normal mode) produced a
	// stop-go stutter across the whole defence, with an UnCrouch() on every release edge.
	// 0 = no hysteresis: the release re-arms on the first knowing tick.
	UPROPERTY(EditAnywhere, Category = "Companion|WaveHold", meta = (ClampMin = "0.0", EditCondition = "bEnableWaveHold"))
	float WaveHoldQuietRearmSeconds = 3.f;

	// Stale-combat backstop: release the wave hold when no combat has been reported to the director
	// for this long, regardless of whether the wave is still technically active. Covers both stuck
	// enemies and blocked-spawn soft-locks. Must exceed the squad spawn cadence (8s default) so a
	// healthy wave never trips it. 0 disables.
	UPROPERTY(EditAnywhere, Category = "Companion|WaveHold", meta = (ClampMin = "0.0", EditCondition = "bEnableWaveHold"))
	float WaveHoldStaleCombatReleaseSeconds = 20.f;

	// Hard ceiling (s) on time held BLIND during a single wave -- held with no combat target and no
	// LoS-verified threat. Counts only that: a healthy 4-squad defence runs 40-90s wall clock and is
	// held for nearly all of it, so a ceiling on total held time tripped mid-wave and disabled the hold
	// for the rest of the defence (allies abandoning cover in every squad gap). The blind gate makes
	// this an unreachable backstop when the quiet release above is active (WaveHoldQuietReleaseSeconds
	// > 0 trips first), and the sole blind-time release when a designer zeroes the quiet release.
	// Once tripped the hold stays released until the wave changes. Must comfortably exceed the quiet
	// release (6s) so it only fires on a genuine soft-lock. 0 disables.
	UPROPERTY(EditAnywhere, Category = "Companion|WaveHold", meta = (ClampMin = "0.0", EditCondition = "bEnableWaveHold"))
	float WaveHoldMaxBlindHoldSeconds = 45.f;

	// --- Mode (player-commanded Defensive / Combat / Stealth) ---
	// Defensive is the ECompanionMode::Normal enumerator — the label changed, the code name did not.

	// Combat mode: how far AHEAD of the player (along their facing / move direction) the companion
	// holds formation when out of contact. This is also the lead cap — the formation point never
	// projects farther than this past the player. Must exceed AcceptableRadius or the follow task's
	// idle gate stops the companion at the player's side before it reaches the lead point.
	UPROPERTY(EditAnywhere, Category = "Companion|Mode", meta = (ClampMin = "0.0"))
	float CombatModeLeadDistance = 550.f;

	// Combat mode: lateral offset of the lead formation point.
	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	float CombatModeLeadOffsetRight = 150.f;

	// Combat mode: shift applied to the cover-scoring ideal-distance band floor (cm, negative =
	// prefer covers nearer the threat). Mirrors the enemy posture Press mechanic: the band term is
	// also flipped to penalise distance, so the companion gains ground cover-to-cover.
	UPROPERTY(EditAnywhere, Category = "Companion|Mode", meta = (ClampMax = "0.0"))
	float CombatModeAdvanceDistMinDelta = -300.f;

	// Stealth mode: formation tucks in tighter behind the player than the normal follow offsets.
	UPROPERTY(EditAnywhere, Category = "Companion|Mode", meta = (ClampMin = "0.0"))
	float StealthFormationOffsetBack = 180.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	float StealthFormationOffsetRight = 100.f;

	// Stealth mode: seconds with no combat target and no alerted threats before broken stealth
	// re-pins (companion returns to crouched, hold-fire stealth rules).
	UPROPERTY(EditAnywhere, Category = "Companion|Mode", meta = (ClampMin = "0.5"))
	float StealthRepinDelay = 4.f;

	// Seconds the weapon stays up after the last target is gone, in COMBAT mode — the fight might not
	// be over. Posture only: it never holds a facing, so it does not fight the post-combat overwatch
	// hold or the route facing reference. 0 = lower on the frame the target is lost (legacy).
	//
	// SILENTLY CEILINGED BY UBTService_UpdateCompanionState::ExploreReturnDelay (3s). The anim raise
	// clause requires ECompanionPosture::Combat, and posture flips to Exploration once
	// OutOfCombatTimer reaches ExploreReturnDelay — a timer that now starts accruing on the same tick
	// this hold arms, because overwatch no longer enters outside a Director wave. Raising this above
	// ExploreReturnDelay does nothing at all; raise that first. 2.5 leaves half a second of slack so
	// which one expires first is not a coin flip at the service's 0.25s tick granularity.
	UPROPERTY(EditAnywhere, Category = "Companion|Mode", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float CombatWeaponUpHoldSeconds = 2.5f;

	// The same hold in DEFENSIVE mode — a beat, not a stance. Stealth has no entry here and never
	// raises on target loss at all.
	UPROPERTY(EditAnywhere, Category = "Companion|Mode", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DefensiveWeaponUpHoldSeconds = 1.f;

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

	// Max vertical gap (cm) at which the companion counts as level with the player. Straight-line
	// distance lies through floors — a stairwell player one floor down reads as ~400cm "close" in 3D.
	// Off-level: idle/hysteresis never latch, wrong-floor EQS follow slots are discarded, and
	// non-stealth follow sprints to close the gap. Must clear steps/ramps but stay under a floor
	// height (~400). 0 disables all three gates.
	UPROPERTY(EditAnywhere, Category = "Companion|Formation", meta = (ClampMin = "0"))
	float FollowMaxZDelta = 150.f;

	// Sustained player vertical rate (cm/s) that flags the player as changing floors. While
	// flagged, follow skips the EQS slot and formation anchor and pursues the player's location
	// directly — mid-flight the player's Z sits within FollowMaxZDelta of the floor ABOVE, so
	// upper-landing slots pass the wrong-floor gate and yo-yo the companion on the stairs.
	// Must sit above flat-ground noise but under stair descent rate (~100-200). 0 disables.
	UPROPERTY(EditAnywhere, Category = "Companion|Formation", meta = (ClampMin = "0"))
	float FollowPursuitZRate = 50.f;

	// --- Non-combat facing (F3 watch-threats + F1 ambient facing) ---
	// Out-of-combat presentation only — never flips posture to Combat, never fires, never touches
	// the actual combat-target key. See UBTService_UpdateCompanionState::UpdateNonCombatFacing.

	// Range (cm) within which the nearest perceived, alive, LoS-visible enemy — including one that
	// hasn't detected the player — becomes a "watch threat" (Normal: weapon raised; Stealth: rotate
	// only, stays low).
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.0"))
	float WatchThreatRange = 3500.f;

	// Seconds the watch facing/stance holds after the watched enemy drops out of LoS/range before
	// releasing. Doubles as last-known-position memory: long enough that a player backing off from
	// an unaware enemy keeps the companion covering the retreat instead of shrugging it off, while
	// still absorbing doorway flicker.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.0"))
	float WatchThreatLingerSeconds = 6.f;

	// Fight-aware follow: max age (seconds) of the controller's alerted-threat signal for a
	// no-eye-line companion to act on it — watch stance toward the fight bearing while following,
	// and follow-slot picks biased to slots with eye-line toward the threat. Covers the trailing
	// extractee behind a wall who otherwise strolls through a live firefight. 0 disables both.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.0"))
	float FightSignalMaxAge = 4.f;

	// Master kill-switch for ambient facing (path look-ahead while moving, attention-yaw while idle,
	// idle scan glances). Off = the companion holds whatever yaw it last had out of combat, and
	// watch-threat facing is the only out-of-combat facing source. Defaults off: the 2026-07-11
	// playtest read ambient facing as erratic (fought commanded moves); watch facing alone landed.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing")
	bool bAmbientFacingEnabled = false;

	// Distance (cm) ahead along the current path the companion's ambient facing looks while moving.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.0"))
	float AmbientLookAheadDistance = 300.f;

	// Yaw error (degrees) the idle companion must exceed before it turns to face the player's
	// attention yaw — a dead zone so it doesn't hunt every tiny direction change.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float AmbientYawDeadzoneDeg = 35.f;

	// Half-angle (degrees) of the cone, centered on the player→companion direction, within which
	// the player's own view counts as "looking at the companion" — while true, idle facing does not
	// adopt a new attention yaw (don't turn away while being looked at).
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float AttentionDontTurnAwayConeDeg = 35.f;

	// Idle scan-glance re-roll interval bounds (seconds) — a small random offset added to the
	// idle attention yaw, re-rolled on this cadence for a subtle "looking around" tell.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.5"))
	float AmbientScanIntervalMin = 4.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.5"))
	float AmbientScanIntervalMax = 8.f;

	// Max magnitude (degrees) of the idle scan-glance yaw offset.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float AmbientScanOffsetMaxDeg = 20.f;

	// --- Route facing reference (face the way the level runs while following) ---
	// A designer flags an existing ACompanionRoute as a facing reference and installs it with an
	// overlap trigger. The companion never walks it — it projects onto the route line and faces a
	// fixed distance further along, so a corner sweeps instead of snapping. Sits BELOW combat,
	// threats, revive, takedowns and commanded route walks in the facing tier order.

	// Master switch for the whole tier. Off = no route ever installs a facing, exactly as today.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing")
	bool bRouteFacingReferenceEnabled = true;

	// Distance (cm) walked along the route line past the companion's projection to find the point it
	// faces. This IS the corner sweep: the turn begins once the look-ahead crosses onto the next leg,
	// so a larger value starts the turn earlier and turns more gently. Per-route override lives on
	// ACompanionRoute::FacingReferenceLookAheadOverride for sections with unusually short legs.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "100.0", EditCondition = "bRouteFacingReferenceEnabled"))
	float RouteFacingLookAheadDistance = 800.f;

	// Max 2D distance (cm) from the companion to its projection on the route for the facing to hold —
	// wander far enough off the section's spine and the direction stops meaning anything. 0 = unlimited.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.0", EditCondition = "bRouteFacingReferenceEnabled"))
	float RouteFacingMaxDistance = 2000.f;

	// Max |Z delta| (cm) to the projection, tested separately from the 2D range above. DemoMap stacks
	// floors, so a purely 3D range test keeps a reference installed one storey up live on the floor
	// below and faces the companion at a corridor overhead (same lesson as FollowMaxZDelta and the
	// overwatch door Z gate). 0 = unlimited.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "0.0", EditCondition = "bRouteFacingReferenceEnabled"))
	float RouteFacingMaxZDelta = 250.f;

	// Companion 2D speed (cm/s) above which its own travel direction is trusted for the backtrack
	// test below. Below it the companion is shuffling in formation and its velocity is noise.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "1.0", EditCondition = "bRouteFacingReferenceEnabled"))
	float RouteFacingMinHeadingSpeed = 120.f;

	// Backtrack latch ENTER: dot(travel direction, route heading) at or below this means the companion
	// is running back down the section to catch the player up, and it faces its own direction of
	// travel instead of the route. Negative — only a genuine reversal counts.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "-1.0", ClampMax = "1.0", EditCondition = "bRouteFacingReferenceEnabled"))
	float RouteFacingBacktrackEnterDot = -0.15f;

	// Backtrack latch EXIT: the dot must recover to at least this before the route facing resumes.
	// Must sit above the enter dot — the gap is the hysteresis that stops a companion moving roughly
	// sideways to the route from flickering between the two facings once per tick.
	UPROPERTY(EditAnywhere, Category = "Companion|Facing", meta = (ClampMin = "-1.0", ClampMax = "1.0", EditCondition = "bRouteFacingReferenceEnabled"))
	float RouteFacingBacktrackExitDot = 0.35f;

	// --- Grenade lob (Grenadier-pattern reuse: UEnemyGrenadierComponent attached to the companion;
	// the projectile is team-blind radial damage, so companion grenades hurt everyone in the blast) ---

	// Master switch. When false (or GrenadeProjectileClass unset) the companion never throws.
	UPROPERTY(EditAnywhere, Category = "Companion|Grenade")
	bool bGrenadeEnabled = true;

	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "1"))
	int32 GrenadeSupply = 4;

	// Minimum seconds between throws.
	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "0.5"))
	float GrenadeCooldown = 18.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "0.5"))
	float GrenadeFuseTime = 2.5f;

	// Wind-up seconds before the fallback spawn fires. Keep >= the throw montage's release-notify
	// time — the notify releases first and clears this timer.
	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "0.1"))
	float GrenadeTelegraphTime = 1.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "0.0"))
	float GrenadeMinRange = 500.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "0.0"))
	float GrenadeMaxRange = 2000.f;

	// Damage at the blast centre.
	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "1.0"))
	float GrenadeDamage = 120.f;

	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "1.0"))
	float GrenadeDamageRadius = 350.f;

	// Seconds the combat target must stay LOS-blocked before the companion lobs at last-known.
	// Must stay below the combat task's LosBlockedAbandonSeconds (3.0) or the task exits first.
	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "0.5"))
	float GrenadeLobTriggerLOSBlockedTime = 2.f;

	// Scales the landing point toward the thrower (1.0 = exact last-known spot).
	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float GrenadeLandingDistanceScale = 1.0f;

	// Player-safety gate: refuse the throw when the player stands within
	// GrenadeDamageRadius + this buffer (cm) of the predicted landing point.
	UPROPERTY(EditAnywhere, Category = "Companion|Grenade", meta = (ClampMin = "0.0"))
	float GrenadePlayerSafetyBuffer = 100.f;

	// Projectile spawned on throw (BP_EnemyGrenade — indicator + explosion FX come with it).
	UPROPERTY(EditAnywhere, Category = "Companion|Grenade")
	TSubclassOf<AEnemyGrenadeProjectile> GrenadeProjectileClass;

	// Socket on the companion body mesh the grenade launches from.
	UPROPERTY(EditAnywhere, Category = "Companion|Grenade")
	FName GrenadeThrowSocket = TEXT("GrenadeSocket");

	// Stand throw montages (one picked at random per throw) + crouched variant. The enemy grenadier
	// montages play directly — same skeleton, UpperBody slot, release notify included.
	UPROPERTY(EditAnywhere, Category = "Companion|Grenade")
	TArray<TObjectPtr<UAnimMontage>> GrenadeThrowStandMontages;

	UPROPERTY(EditAnywhere, Category = "Companion|Grenade")
	TObjectPtr<UAnimMontage> GrenadeThrowCrouchMontage;

};
