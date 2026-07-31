// Shared targeting helpers for companion and enemy AI.
// Single source of truth for "where on an enemy should I aim/trace?"

#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"

class AActor;
class UWorld;

namespace AITargeting
{
	/**
	 * Returns the best world-space sight/aim location on Target:
	 *   1. Head bone socket ("head") on a Character mesh — first point to clear cover when standing.
	 *   2. APawn::GetPawnViewLocation() — eye height fallback when no head bone.
	 *   3. GetActorLocation() — final fallback for non-pawn actors.
	 * Returns FVector::ZeroVector if Target is invalid.
	 */
	EXTRACTION_API FVector GetSightLocation(const AActor* Target);

	/**
	 * Returns the lowest body point on Target with clear LOS from ObserverEye, head EXCLUDED by default.
	 * Ladder (low→high): pelvis → spine_03 (chest) → neck_01. When bIncludeHead is true, the head socket is
	 * appended as the HIGHEST (last-resort) candidate so it only fires when all body points are blocked.
	 * Traces ECC_Visibility ignoring Target and IgnoreActor so only world cover blocks (not the body).
	 * Out: OutPoint = first (lowest) visible point. Returns false when no point is visible.
	 * Ragdoll/missing-mesh guard: falls back to a single actor-centre candidate.
	 * Default callers (aim, fire-gate, contact-hold) pass bIncludeHead=false so a head-only peek
	 * stays undetected. The player's CanBeSeenFrom opts in for sniper observers on a standing target.
	 */
	EXTRACTION_API bool GetVisibleBodyPoint(const AActor* Target, const FVector& ObserverEye, const AActor* IgnoreActor, FVector& OutPoint, bool bIncludeHead = false);

	/**
	 * True iff Observer is a sniper-archetype enemy AND Target is standing (not crouched/prone).
	 * Centralises the rule so detection, contact-hold, fire-gate, and aim can't drift.
	 * Crouch/prone targets and all non-sniper enemies always return false.
	 */
	EXTRACTION_API bool ShouldIncludeHeadForObserver(const AActor* Observer, const AActor* Target);

	/**
	 * Traces ECC_Visibility from Start to End, but a hit APawn is stepped over instead of counted as
	 * a blocker: the pawn joins a local ignore set and the trace re-fires from the same Start. A hit
	 * with no actor, or a hit on a non-pawn actor, is real world geometry and reports blocked
	 * (OutBlocker, if given, is written with it). No hit at all is clear.
	 *
	 * Built for the commanded-takedown line-of-sight gate: in a double takedown the companion's
	 * victim, the second eligible enemy, and the player all end up shoulder-to-shoulder in the same
	 * small volume, and AExtractionPlayer/AEnemyCharacter meshes both explicitly block
	 * ECC_Visibility. Without this, those bodies read as permanent world cover exactly like a wall —
	 * the anchor search rejects otherwise-good firing spots and a moving body keeps re-breaking a
	 * line that was never really obstructed, since the kill itself is registered directly and the
	 * shot is cosmetic.
	 *
	 * QueryParams is taken BY VALUE: pawns stepped over are added to the local copy only, so the
	 * caller's own ignore set (self / victim / held weapon) is never mutated.
	 *
	 * Caps the pawn-skip loop at a small iteration limit so a pathological stack of bodies cannot
	 * spin the trace forever; exceeding the cap reports blocked rather than clear.
	 */
	EXTRACTION_API bool HasClearLineIgnoringPawns(const UWorld* World, const FVector& Start, const FVector& End,
		FCollisionQueryParams QueryParams, AActor** OutBlocker = nullptr);
}
