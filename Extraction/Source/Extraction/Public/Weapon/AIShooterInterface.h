// Interface implemented by any AI-controlled shooter (companion, enemy) to decouple WeaponBase from concrete character types.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AIShooterInterface.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UAIShooterInterface : public UInterface
{
	GENERATED_BODY()
};

class EXTRACTION_API IAIShooterInterface
{
	GENERATED_BODY()

public:
	/** Returns the actor this AI is currently aiming at. May be null. */
	virtual AActor* GetAIAimTarget() const = 0;

	/** Returns the current aim spread in degrees (accounts for settle, movement widen, etc.). */
	virtual float GetAIAimSpreadDegrees() const = 0;

	/** Optionally returns a world-space position to aim at instead of the target actor's origin.
	 *  Returns false (default) if no override is active; WeaponBase falls back to actor location. */
	virtual bool GetAIAimLocation(FVector& OutLocation) const { return false; }

	/** Returns the world-space point on Target that this shooter aims at (for hitscan direction).
	 *  Default returns target actor centre so enemy behaviour is unchanged.
	 *  Companion overrides to return the head/eye point so rounds actually strike partially-exposed enemies. */
	virtual FVector GetAimPointForTarget(const AActor* Target) const
	{
		return IsValid(Target) ? Target->GetActorLocation() : FVector::ZeroVector;
	}
};
