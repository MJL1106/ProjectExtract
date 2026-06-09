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
};
