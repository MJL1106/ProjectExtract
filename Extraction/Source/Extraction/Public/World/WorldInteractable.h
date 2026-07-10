// IWorldInteractable -- generic interaction interface for world actors that respond to the
// player's interact trace (extraction targets, terminals, etc.). Checked in TryWorldInteract
// BEFORE the legacy loot/keycard paths so new implementors take priority.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "WorldInteractable.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UWorldInteractable : public UInterface
{
	GENERATED_BODY()
};

class EXTRACTION_API IWorldInteractable
{
	GENERATED_BODY()

public:
	/** Returns true when Interactor is currently allowed to interact with this actor. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	bool CanWorldInteract(AActor* Interactor) const;

	/** Execute the interaction. Called on authority only. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void WorldInteract(AActor* Interactor);

	/** Short prompt text shown on the player HUD while looking at this actor. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	FText GetWorldInteractionPrompt(AActor* Interactor) const;
};
