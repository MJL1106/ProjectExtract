// UExtractionPlayerMovement — consumption-side route speed lock.

#include "Character/ExtractionPlayerMovement.h"
#include "Character/ExtractionPlayer.h"

float UExtractionPlayerMovement::GetMaxSpeed() const
{
	const float BaseSpeed = Super::GetMaxSpeed();

	const AExtractionPlayer* Player = Cast<AExtractionPlayer>(GetCharacterOwner());
	if (!Player) return BaseSpeed;

	const float Lock = Player->GetRouteSpeedLock();
	if (Lock <= 0.f || Player->GetIsDBNO()) return BaseSpeed;

	// Only ground/air locomotion (the walk-speed family); leave swim/fly/custom alone.
	if (!IsMovingOnGround() && !IsFalling()) return BaseSpeed;

	// Crouch is capped, never boosted; standing locks to the exact value.
	return IsCrouching() ? FMath::Min(BaseSpeed, Lock) : Lock;
}
