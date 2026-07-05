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
	};

	/** Evaluates the commit triggers. bForRelease widens each threshold by its release margin so an
	 *  in-cover companion only exits once the situation has genuinely cleared (anti pop-out). */
	FCoverTriggers EvaluateTriggers(const ACompanionCharacter& Companion,
		const UCompanionTuningDataAsset& Tuning, int32 KnownThreatCount, bool bForRelease);

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

	/** 2D distance from Point to the nearest AVAILABLE baked cover point within Radius, or -1 if
	 *  none. Skips points occupied or intended by anyone other than Querier — a duck spot that's
	 *  taken is no duck spot. One octree bounds query per call. */
	float DistToNearestCover(UWorld* World, const FVector& Point, float Radius, const AController* Querier);

	/** Location of the nearest available baked cover point within Radius of Point. False if none. */
	bool NearestCoverLocation(UWorld* World, const FVector& Point, float Radius,
		const AController* Querier, FVector& OutLocation);

	/** Edge-aligned hunker position using the companion tuning's CoverCornerGap — the same corner
	 *  alignment enemies get from their archetype DA. Per GetEdgeAlignedHunkerPosition's contract,
	 *  EVERY companion system that defines "standing at this cover" must use this (arrival targets,
	 *  return snaps, arrival detection) or they disagree about the slot. Plain mid-wall hunkers made
	 *  endpoint covers resolve Front: over-top peeks only, no corner peeks, coin-flip idle side.
	 *  Falls back to the plain hunker when no tuning is reachable. */
	FVector CompanionHunkerPosition(const ACompanionCharacter& Companion, const FCoverData& Data, float Standoff);
}
