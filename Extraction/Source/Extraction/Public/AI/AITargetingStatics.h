// Shared targeting helpers for companion and enemy AI.
// Single source of truth for "where on an enemy should I aim/trace?"

#pragma once

#include "CoreMinimal.h"

class AActor;

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
	 * Ladder (low→high): pelvis → spine_03 (chest). When bIncludeHead is true, the head socket is
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
}
