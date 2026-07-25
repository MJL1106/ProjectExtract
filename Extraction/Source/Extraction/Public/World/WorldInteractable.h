// IWorldInteractable -- generic interaction interface for world actors that respond to the
// player's interact trace (extraction targets, terminals, etc.). Checked in TryWorldInteract
// BEFORE the legacy loot/keycard paths so new implementors take priority.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Interface.h"
#include "WorldInteractable.generated.h"

namespace ExtractionInteraction
{
	/** Dedicated interact trace channel — ECC_GameTraceChannel2, declared as "Interact" in
	 *  DefaultEngine.ini with DefaultResponse=Ignore so nothing blocks it by accident.
	 *  Invisible interact volumes block THIS and ignore ECC_Visibility: enemy line-of-sight,
	 *  companion muzzle clearance and cover generation all trace Visibility, and a box draped
	 *  over a table must never answer those. Interact traces run a second pass here. */
	inline constexpr ECollisionChannel InteractTraceChannel = ECC_GameTraceChannel2;
}

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

	/** Seconds the player must hold Interact before WorldInteract fires. 0 (the default, and what
	 *  every legacy implementor returns) keeps the press instant. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	float GetWorldInteractHoldSeconds(AActor* Interactor) const;

	/** Fired on authority when a hold interaction begins — the beat that runs UNDER the hold
	 *  (scripted VO, an approach animation). Never called for instant interactables, and a
	 *  cancelled hold does not un-fire it, so implementors own their own once-only state. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void OnWorldInteractHoldStarted(AActor* Interactor);
};
