// UInteractionEventSubsystem — world-scope "an IWorldInteractable was actually used" notification.
// The interface carries no completion event of its own, so anything that needs to react to "the
// player used THAT actor" (AObjectiveStep's Interacted condition) listens here instead of coupling
// to every concrete interactable's private delegate. World lifetime, so nothing leaks between PIE
// sessions.
//
// Raised by each interactable from its OWN success branch, never blanket from the player's commit:
// WorldInteract has no return value, and implementers routinely accept the call and refuse the
// action (a locked lift toasts "eliminate remaining enemies" and returns). A blanket raise turns
// every refused press into a completed objective beat.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "InteractionEventSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWorldInteractPerformed, AActor*, Target, AActor*, Interactor);

UCLASS()
class EXTRACTION_API UInteractionEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Called by an interactable once its interaction has actually taken effect. Target may already
	 *  be destroying itself as a result, so listeners must compare identity rather than dereference.
	 *  Interactor may be null when a non-player system drove the interaction.
	 *
	 *  Blueprint-callable because a BP interactable is otherwise invisible to every listener: the
	 *  interface carries no completion event, so implementing IWorldInteractable is not enough — the
	 *  BP has to raise this from its own success branch, exactly as the native interactables do. */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void NotifyWorldInteract(AActor* Target, AActor* Interactor);

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnWorldInteractPerformed OnWorldInteractPerformed;
};
