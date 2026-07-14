// Interface for actors that the companion can be commanded to breach (doors, hatches, etc.).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Breachable.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UBreachable : public UInterface
{
	GENERATED_BODY()
};

class EXTRACTION_API IBreachable
{
	GENERATED_BODY()

public:
	/** Execute the breach action. Called by the companion on arrival. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Breach")
	void Breach(AActor* Breacher);

	/** Returns true if this actor is currently in a breachable state. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Breach")
	bool CanBreach() const;

	/** Where the breacher should stand (and which way it should face) to play its breach
	 *  animation — a point in front of the actor on the breacher's side. Returns false when the
	 *  implementer has no meaningful stand point (caller falls back to plain proximity). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Breach")
	bool GetBreachStandPoint(const AActor* Breacher, FVector& OutLocation, FRotator& OutFacing) const;

	/** Where the breacher should move AFTER the breach so it doesn't block the opening.
	 *  bEnterRoom true = a point inside, past the doorway, laterally clear of the swung leaf
	 *  (Loud entry); false = a point outside beside the frame (Tactical/Quiet sidestep).
	 *  Returns false when the implementer has no meaningful point (caller just holds). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Breach")
	bool GetPostBreachPoint(const AActor* Breacher, bool bEnterRoom, FVector& OutLocation) const;

	/** True when the breacher should enter the room after breaching regardless of breach type —
	 *  a per-actor override so designated doors get the Loud-style push-through even on a
	 *  Tactical/Quiet breach. Montage and noise are unaffected. Defaults to false. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Breach")
	bool ShouldForcePushThrough() const;
	virtual bool ShouldForcePushThrough_Implementation() const { return false; }
};
