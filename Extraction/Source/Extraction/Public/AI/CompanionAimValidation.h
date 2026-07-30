// Shared aim-bearing validation for the companion brain. One seam, used by both allies: the armed
// extractee VIP is an AExtracteeCompanion : ACompanionCharacter and runs the same controller, tree
// and tuning asset, so anything fixed here is fixed for both.
//
// The rule this seam exists to enforce: a THREAT-DERIVED focal is never asserted on a bearing that
// has not been proven open. When a bearing cannot be proven, the answer is to decline and lower —
// never to invent an alternative one. A yaw-fan / sweep-arc facing was built, iterated three times
// and scrapped; do not re-introduce one here.

#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"

class UWorld;
struct FHitResult;

namespace CompanionAim
{
	/**
	 * First GEOMETRY hit along a line; pawns are stepped past instead of counted as walls. Characters
	 * block ECC_Visibility, so the player or another ally crossing within the standoff made the aim
	 * resolve fail and ended overwatch, then entry re-entered once they cleared — an END/START cycle
	 * driven by nothing but the player's footwork. Params is by value: each skipped pawn joins its
	 * ignore list before the re-trace. Returns false when the line holds nothing but pawns.
	 *
	 * NOT for visible-enemy bearings. There is deliberately no "the hit IS the target" exemption
	 * here, so a fully visible enemy standing clear of a wall is skipped as a pawn, the wall behind
	 * it is hit, and the bearing would be wrongly DECLINED — dropping the gun while an enemy is
	 * shooting. The combat-target, watch-visible and ready-threat gates keep their own
	 * target-exempting traces; only the threat-MEMORY bearings come through this.
	 */
	bool TraceGeometryPastPawns(const UWorld& World, const FVector& Start, const FVector& End,
		FCollisionQueryParams Params, FHitResult& OutHit);
}
