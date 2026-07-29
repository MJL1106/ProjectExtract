// UWeaponCaseSlotComponent implementation.

#include "World/WeaponCaseSlotComponent.h"

#include "Components/MeshComponent.h"

DEFINE_LOG_CATEGORY(LogWeaponCase);

UWeaponCaseSlotComponent::UWeaponCaseSlotComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

#if WITH_EDITORONLY_DATA
	// Editor sprite so unfilled (hidden-preview) slots remain selectable in the viewport.
	bVisualizeComponent = true;
#endif
}

void UWeaponCaseSlotComponent::SetPreviewVisible(bool bShowPreviews)
{
	// Manual tree walk -- see header for why we avoid SetVisibility(bPropagateToChildren).
	const AActor* MyOwner = GetOwner();
	TArray<USceneComponent*> Stack;
	GetChildrenComponents(false /* direct children only */, Stack);

	TArray<USceneComponent*> GrandChildren;

	while (Stack.Num() > 0)
	{
		USceneComponent* Child = Stack.Pop(EAllowShrinking::No);
		if (!IsValid(Child)) continue;

		// Cross-actor guard: spawned pickups attach under this component -- their meshes belong
		// to a different actor and must never be hidden or de-collided by preview toggling.
		if (Child->GetOwner() != MyOwner) continue;

		// Nested-slot stop: a slot parented under another for grouping owns its own preview set.
		if (Child->IsA<UWeaponCaseSlotComponent>()) continue;

#if WITH_EDITORONLY_DATA
		// Visualization components (sprites/billboards from bVisualizeComponent) stay visible
		// so unfilled slots keep a selectable handle in the viewport.  Uses
		// IsVisualizationComponent() rather than IsEditorOnly(): designers should tick
		// "Is Editor Only" on preview meshes (strips them during cooking), and that must not
		// exempt them from the hide/de-collide pass.
		if (Child->IsVisualizationComponent()) continue;
#endif

		Child->SetVisibility(bShowPreviews);

		if (UMeshComponent* Mesh = Cast<UMeshComponent>(Child))
		{
			// Defend against designer-added meshes whose project defaults (QueryAndPhysics /
			// BlockAllDynamic) would block the kit interaction trace and bake navmesh.
			const ECollisionEnabled::Type Current = Mesh->GetCollisionEnabled();
			if (Current != ECollisionEnabled::NoCollision)
			{
				UE_LOG(LogWeaponCase, Verbose,
					TEXT("'%s': forcing NoCollision on preview mesh '%s' (was %d)."),
					*GetName(), *Mesh->GetName(), static_cast<int32>(Current));
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			Mesh->SetCanEverAffectNavigation(false);
		}

		// Recurse into this child's children (depth-first).
		GrandChildren.Reset();
		Child->GetChildrenComponents(false, GrandChildren);
		Stack.Append(GrandChildren);
	}
}
