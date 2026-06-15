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
}
