// UWeaponCaseSlotComponent -- one foam cutout in a weapon case.
// One instance per cutout, added by the designer in the case Blueprint's Components panel.
// Its own transform IS the seat: whatever world transform the component has is exactly where
// the pickup spawns and attaches.  The designer parents StaticMeshComponent or
// SkeletalMeshComponent children under it as editor previews -- dragging the slot drags the
// preview with it, and a slot can show more than one mesh (rifle body + handguard, etc.).
//
// Socket-relative seating was intentionally dropped: the component's own transform is the
// single source of truth for where a pickup spawns.  A SocketName field would reintroduce
// two places to author one position (the socket on the case mesh and the component offset),
// making them easy to drift apart without warning.
//
// Preview mesh children should have "Is Editor Only" ticked (Cooking category in Details)
// so they are stripped during cooking and never ship.  SetPreviewVisible uses
// IsVisualizationComponent() to skip editor sprites, so an editor-only preview mesh is
// still correctly hidden and de-collided.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Templates/SubclassOf.h"
#include "WeaponCaseSlotComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogWeaponCase, Log, All);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UWeaponCaseSlotComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UWeaponCaseSlotComponent();

	/** Designer-facing name — must match what a placed case ticks in FilledSlots.
	 *  Required; no fallback to the component name (SCS templates carry name suffixes
	 *  that disagree between the live actor and the class-defaults path in GetSlotIdOptions). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Case|Slot")
	FName SlotId;

	/** Actor spawned into this cutout on BeginPlay.  Assigned in the Blueprint subclass. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Case|Slot")
	TSubclassOf<AActor> PickupClass;

	/** Shows or hides this slot's preview meshes.
	 *  Walks the child-component tree manually (explicit stack, not SetVisibility with
	 *  bPropagateToChildren) for three reasons:
	 *  (1) Each mesh descendant also needs per-component collision/nav clobbering (NoCollision,
	 *      no nav affect) that propagating visibility alone cannot express.
	 *  (2) A cross-actor guard skips descendants owned by a different actor -- the spawned pickup
	 *      attaches under this component, and hiding/de-colliding its meshes would leave it
	 *      invisible and un-interactable regardless of call order.
	 *  (3) Stops at any nested UWeaponCaseSlotComponent without recursing past it, so an outer
	 *      slot cannot reach into an inner slot's preview set.
	 *  In editor builds, visualization components (sprites from bVisualizeComponent) are skipped
	 *  so unfilled slots keep a selectable handle in the viewport. */
	void SetPreviewVisible(bool bShowPreviews);
};
