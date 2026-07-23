// Companion-only cover helpers shared by BTTask_MoveToCoverPoint, BTTask_CompanionCombat and
// BTService_CoverSwitchMonitor. The enemy cover pipeline never calls these — enemy behaviour is
// governed entirely by the shared EQS/scorer plus its own fire-task FSM.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

class AActor;
class AAIController;
class AController;
class APawn;
class ACompanionCharacter;
class UBlackboardComponent;
class UCompanionTuningDataAsset;
class UWorld;
struct FCoverData;

namespace CompanionCover
{
	/** Situational cover-commit triggers — commit to real cover when ANY is active. */
	struct FCoverTriggers
	{
		bool bUnderFire = false;
		bool bLowHealth = false;
		bool bLowAmmoOrReloading = false;
		bool bOutnumbered = false;

		bool Any() const { return bUnderFire || bLowHealth || bLowAmmoOrReloading || bOutnumbered; }

		/** Low health is the ONLY active trigger — the case the dash gate applies to. */
		bool LowHealthOnly() const { return bLowHealth && !bUnderFire && !bLowAmmoOrReloading && !bOutnumbered; }
	};

	/** Evaluates the commit triggers. bForRelease widens each threshold by its release margin so an
	 *  in-cover companion only exits once the situation has genuinely cleared (anti pop-out).
	 *  Under fire = graded pressure (suppression01 threshold / multi-hit count), not any-1-damage.
	 *  Reloading is release-only (stay in cover to reload; never run to cover just to reload).
	 *  COMBAT mode (bCombatModeStricterCommit) demands heavy pressure and ignores ammo entirely. */
	FCoverTriggers EvaluateTriggers(const ACompanionCharacter& Companion,
		const UCompanionTuningDataAsset& Tuning, int32 KnownThreatCount, bool bForRelease);

	/** Threat-count cap callers must pass to CountKnownThreats so mode-specific outnumbered
	 *  thresholds (CombatOutnumberedCount > CoverTriggerOutnumberedCount) stay reachable. */
	int32 OutnumberedCountCap(const UCompanionTuningDataAsset& Tuning);

	/** Low-HP dash gate: when low health is the ONLY active trigger, commit is allowed only if an
	 *  available cover sits within LowHealthCoverMaxDash of the pawn — a wounded companion never
	 *  sprints across open ground to reach a duck spot. Any other active trigger (or a disabled
	 *  gate) returns true. */
	bool LowHealthDashAllowed(UWorld* World, const FVector& PawnLoc, const AController* Querier,
		const UCompanionTuningDataAsset& Tuning, const FCoverTriggers& Triggers);

	/** Pressure SPIKE — heavy suppression, recent hits, or outnumbered. Deliberately excludes
	 *  static low health: a wounded-but-unpressured companion must not bypass the recommit
	 *  cooldown, or every deliberate cover release instantly re-commits and the companion
	 *  duck-hops between points. The recommit gates use THIS bar. */
	bool HasPressureSpiked(const ACompanionCharacter& Companion,
		const UCompanionTuningDataAsset& Tuning, int32 KnownThreatCount);

	/** STRONG pressure — HasPressureSpiked OR low health. The switch monitor's committed-time
	 *  release fires only when this is false, so a wounded companion holds working cover instead
	 *  of naturally cycling out of it. Release side only; commit sides use HasPressureSpiked. */
	bool IsStrongPressure(const ACompanionCharacter& Companion,
		const UCompanionTuningDataAsset& Tuning, int32 KnownThreatCount);

	/** Count of sight-perceived, enemy-tagged, alive threats known to the controller, capped.
	 *  Reads the existing perception component — no new perception cost. */
	int32 CountKnownThreats(AAIController* Controller, int32 Cap);

	/** Extra known threats beyond the focus target, nearest-first, capped to MaxExtra. Returns
	 *  actors (not locations) so body-shield traces can ignore the threat's own body. Extracted
	 *  from BTService_CoverSwitchMonitor so the FIRST cover pick can test the same threat set. */
	void GatherExtraThreatActors(AAIController* Controller, const APawn* Pawn, const AActor* FocusTarget,
		int32 MaxExtra, TArray<AActor*, TInlineAllocator<8>>& OutActors);

	/** Number of ExtraThreatActors whose body-shield check fails for this cover point. */
	int32 CountUncoveredThreats(UWorld* World, const FCoverData& Cover, float Standoff,
		float ChestHeight, const APawn* Pawn, TArrayView<AActor* const> ExtraThreatActors);

	/** True when Cover no longer protects against a threat at ThreatLoc: the threat sits outside
	 *  the fire arc widened by ArcSlackDeg, or the chest-height body-shield trace from the hunker
	 *  position fails. Enemy flank-break parity (BTTask_EnemyCombatFire's IsCoverCompromised). */
	bool IsCoverCompromised(UWorld* World, const FCoverData& Cover, const AActor* Threat,
		const FVector& ThreatLoc, float ArcHalfAngleDeg, float ArcSlackDeg, float Standoff,
		float ChestHeight, const APawn* Pawn);

	/** 2D distance from Point to the nearest AVAILABLE baked cover point within Radius, or -1 if
	 *  none. Skips points occupied or intended by anyone other than Querier — a duck spot that's
	 *  taken is no duck spot. One octree bounds query per call. */
	float DistToNearestCover(UWorld* World, const FVector& Point, float Radius, const AController* Querier);

	/** Location of the nearest available baked cover point within Radius of Point. False if none. */
	bool NearestCoverLocation(UWorld* World, const FVector& Point, float Radius,
		const AController* Querier, FVector& OutLocation);

	/** True when the 2D segment PawnLoc -> Dest passes within Clearance (cm) of any threat actor.
	 *  Pure point-to-segment math, no traces — cheap enough to run per candidate at re-eval cadence.
	 *  Deliberate straight-line approximation of the nav path: the failure it guards against is the
	 *  direct charge through an enemy group, not a nav detour that happens to skirt one enemy.
	 *  Clearance <= 0 disables (returns false). */
	bool PathPassesNearThreat(const FVector& PawnLoc, const FVector& Dest,
		TArrayView<AActor* const> Threats, float Clearance);

	/** Muzzle-gated approach-fire state shared by BTTask_MoveToCoverPoint's companion transit and
	 *  BTTask_CompanionCombat's final-approach walk — one implementation of "fire at the visible
	 *  combat target while walking to a committed cover point". */
	struct FApproachFireState
	{
		bool bFiring = false;
		float TickAccum = 0.f;
		/** Target the aim/focus were issued for — a BB retarget mid-move must re-issue them or fire
		 *  streams at the old target's position. */
		TWeakObjectPtr<AActor> LatchedTarget;

		void Reset()
		{
			bFiring = false;
			TickAccum = 0.f;
			LatchedTarget.Reset();
		}
	};

	/** Ticks the approach-fire loop (10 Hz internal throttle): fires at the BB combat target while
	 *  the companion has eye-line AND a clear muzzle line, holds otherwise. Honors the tuning's
	 *  bCoverApproachFireWhileMoving toggle and the reload state. Focus is set once at first fire
	 *  and held for the move (facing flicker on momentary LoS loss reads worse than a held torso). */
	void TickCoverApproachFire(ACompanionCharacter* Companion, AAIController* Controller,
		UBlackboardComponent* BB, FApproachFireState& State, float DeltaSeconds);

	/** Approach-fire teardown: stop fire, clear aim, reset the state latch. bKeepFocus=true on
	 *  arrival (keep facing the threat while entering cover); false on every failure/abort exit. */
	void StopCoverApproachFire(ACompanionCharacter* Companion, AAIController* Controller,
		FApproachFireState& State, bool bKeepFocus);

	/** Edge-aligned hunker position using the companion tuning's CoverCornerGap — the same corner
	 *  alignment enemies get from their archetype DA. Per GetEdgeAlignedHunkerPosition's contract,
	 *  EVERY companion system that defines "standing at this cover" must use this (arrival targets,
	 *  return snaps, arrival detection) or they disagree about the slot. Plain mid-wall hunkers made
	 *  endpoint covers resolve Front: over-top peeks only, no corner peeks, coin-flip idle side.
	 *  Falls back to the plain hunker when no tuning is reachable. */
	FVector CompanionHunkerPosition(const ACompanionCharacter& Companion, const FCoverData& Data, float Standoff);
}
