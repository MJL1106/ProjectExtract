// UInteractionEventSubsystem implementation.

#include "World/InteractionEventSubsystem.h"

void UInteractionEventSubsystem::NotifyWorldInteract(AActor* Target, AActor* Interactor)
{
	// Deliberately a null check, not IsValid: an interactable that destroys itself as part of its
	// own interaction is exactly the case a listener still needs to hear about.
	if (Target == nullptr) return;
	OnWorldInteractPerformed.Broadcast(Target, Interactor);
}
