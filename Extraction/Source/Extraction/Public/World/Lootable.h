// Interface for actors that can be looted — by the player directly (Interact) or by the
// companion via ping command. Mirrors IBreachable so both ride the same ping/trace spine.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Lootable.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class ULootable : public UInterface
{
	GENERATED_BODY()
};

class EXTRACTION_API ILootable
{
	GENERATED_BODY()

public:
	/** Execute the loot action. Called by the player on Interact or the companion on arrival. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Loot")
	void Loot(AActor* Looter);

	/** Returns true if this actor currently has something to loot. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Loot")
	bool CanLoot() const;
};
