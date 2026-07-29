// AnimNotify_StimHeal

#include "AnimNotify_StimHeal.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ConsumableInventoryComponent.h"
#include "Engine/World.h"

void UAnimNotify_StimHeal::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp)) return;

	// Gate logging on game world so Persona preview doesn't spam.
	const UWorld* World = MeshComp->GetWorld();
	const bool bGameWorld = IsValid(World) && World->IsGameWorld();

	AActor* Owner = MeshComp->GetOwner();
	if (!IsValid(Owner))
	{
		if (bGameWorld)
			UE_LOG(LogConsumables, Warning, TEXT("StimHeal notify: mesh '%s' has no valid owner."), *MeshComp->GetName());
		return;
	}

	UConsumableInventoryComponent* Consumables = Owner->FindComponentByClass<UConsumableInventoryComponent>();
	if (!IsValid(Consumables))
	{
		if (bGameWorld)
			UE_LOG(LogConsumables, Warning, TEXT("StimHeal notify: owner '%s' (mesh '%s') has no UConsumableInventoryComponent."),
				*Owner->GetName(), *MeshComp->GetName());
		return;
	}

	Consumables->ApplyStimHealNow();
}
