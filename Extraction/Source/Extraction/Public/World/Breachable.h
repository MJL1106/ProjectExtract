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
};
