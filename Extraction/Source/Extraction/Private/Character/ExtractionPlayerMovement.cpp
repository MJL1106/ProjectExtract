// UExtractionPlayerMovement — consumption-side route speed lock.

#include "Character/ExtractionPlayerMovement.h"
#include "Character/ExtractionPlayer.h"

float UExtractionPlayerMovement::GetMaxSpeed() const
{
	const float BaseSpeed = Super::GetMaxSpeed();

	const AExtractionPlayer* Player = Cast<AExtractionPlayer>(GetCharacterOwner());
	if (!Player) return BaseSpeed;

	float Speed = BaseSpeed;

	// Route speed lock — only ground/air locomotion (the walk-speed family); swim/fly/custom alone.
	const float Lock = Player->GetRouteSpeedLock();
	if (Lock > 0.f && !Player->GetIsDBNO() && (IsMovingOnGround() || IsFalling()))
	{
		// Crouch is capped, never boosted; standing locks to the exact value.
		Speed = IsCrouching() ? FMath::Min(Speed, Lock) : Lock;
	}

	// Held-Ctrl walk cap — applied last so it combines min-wise with the route lock (whichever is
	// lower always wins) and always beats a kit-BP sprint write. Crouch excluded: already below
	// HeldWalkSpeed by design (PIE-verify; see the header comment on HeldWalkSpeed).
	if (Player->IsWalkHeld() && !Player->GetIsDBNO() && IsMovingOnGround() && !IsCrouching())
		Speed = FMath::Min(Speed, HeldWalkSpeed);

	return Speed;
}
