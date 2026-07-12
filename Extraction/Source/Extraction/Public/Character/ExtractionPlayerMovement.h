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
	 *  swim/fly/custom modes are untouched. The held-walk cap (see HeldWalkSpeed) is applied last
	 *  and combines min-wise with the route lock. */
	virtual float GetMaxSpeed() const override;

	/** Ground speed cap while AExtractionPlayer::IsWalkHeld() is true (standing only — crouch is
	 *  already below this by design). PIE-verify: if a future crouch speed exceeds this value, drop
	 *  the !IsCrouching() exclusion in GetMaxSpeed. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|Walk", meta = (ClampMin = "10.0"))
	float HeldWalkSpeed = 250.f;
};
