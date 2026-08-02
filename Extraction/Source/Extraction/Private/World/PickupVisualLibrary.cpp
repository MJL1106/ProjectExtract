#include "World/PickupVisualLibrary.h"

#include "Components/MeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/Actor.h"

void UPickupVisualLibrary::SetPickupOutline(AActor* PickupActor, bool bEnabled)
{
	if (!IsValid(PickupActor)) return;

	// Attached actors matter as much as own components: a case-seated pickup can carry its
	// display geometry on a child actor, and the corpse-gun pickup rides the enemy's weapon.
	TArray<AActor*> Actors;
	Actors.Add(PickupActor);
	PickupActor->GetAttachedActors(Actors, /*bResetArray=*/false, /*bRecursivelyIncludeAttachedActors=*/true);

	for (AActor* Actor : Actors)
	{
		if (!IsValid(Actor)) continue;

		TInlineComponentArray<UMeshComponent*> Meshes(Actor);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (IsValid(Mesh) && !Mesh->IsA<UWidgetComponent>())
				Mesh->SetRenderCustomDepth(bEnabled);
		}
	}
}
