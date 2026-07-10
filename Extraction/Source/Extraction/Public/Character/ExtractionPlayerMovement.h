// Player CharacterMovementComponent: applies the companion-route speed lock at the point of
// consumption (GetMaxSpeed) so kit-BP MaxWalkSpeed writes can never race or stomp it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExtractionPlayerMovement.generated.h"

UCLASS()
class EXTRACTION_API UExtractionPlayerMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/** Ground/air max speed honours AExtractionPlayer's route speed lock: standing speed is the
	 *  lock value exactly; crouched speed is capped by it but never boosted. DBNO crawl and
	 *  swim/fly/custom modes are untouched. */
	virtual float GetMaxSpeed() const override;
};
