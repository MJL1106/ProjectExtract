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
	 * Returns the lowest body point on Target with clear LOS from ObserverEye, head EXCLUDED.
	 * Ladder (low→high): pelvis → spine_03 (chest) → neck_01 (neck/upper body). No head, no arms.
	 * Traces ECC_Visibility ignoring Target and IgnoreActor so only world cover blocks (not the body).
	 * Out: OutPoint = first (lowest) visible point. Returns false when no body point is visible.
	 * Ragdoll/missing-mesh guard: falls back to a single actor-centre candidate.
	 * Used for player detection (CanBeSeenFrom), enemy aim, the enemy fire-gate, and combat
	 * contact-hold — so the enemy never detects, targets, or fires on a head-only peek.
	 */
	EXTRACTION_API bool GetVisibleBodyPoint(const AActor* Target, const FVector& ObserverEye, const AActor* IgnoreActor, FVector& OutPoint);
}
