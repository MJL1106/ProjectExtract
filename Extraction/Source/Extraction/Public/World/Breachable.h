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
};
