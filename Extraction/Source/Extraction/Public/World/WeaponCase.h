// AWeaponCase — designer-placed open weapon case whose foam cutouts hold real pickup actors.
// Each cutout is a UWeaponCaseSlotComponent added in the Blueprint's Components panel — the
// designer drags the gizmo to position it and parents preview meshes under it as children.
// A placed INSTANCE only ticks which of those slots are filled (FilledSlots).
// In the editor the filled slots show their preview children; on BeginPlay the authority hides
// every preview and spawns the real pickup actors attached to the slot components.
// Meshes and pickup classes are assigned in the Blueprint subclass — C++ stays asset-agnostic.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeaponCase.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UWeaponCaseSlotComponent;

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking), PrioritizeCategories = "Case")
class EXTRACTION_API AWeaponCase : public AActor
{
	GENERATED_BODY()

public:
	AWeaponCase();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** True once every spawned item has been collected (pickups destroy themselves on collect).
	 *  A case that spawned nothing reads as empty. Hook for "case is picked clean" dressing.
	 *  ALWAYS false off authority: only the server spawns the items, so a client's list is empty
	 *  from BeginPlay onward and "empty" there would be a lie, not a state. Nothing consumes this
	 *  across the network yet, so there is deliberately no replicated counter behind it. */
	UFUNCTION(BlueprintPure, Category = "Case")
	bool IsEmpty() const;

	/** Backs the FilledSlots dropdown — returns every authored SlotId on this case type. */
	UFUNCTION()
	TArray<FString> GetSlotIdOptions() const;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Case")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Case body. Mesh assigned in the BP subclass. Blocks ECC_Visibility by default so bullets
	 *  stop on it (weapon hitscan traces Visibility), and never blocks CoverGen so a case sitting
	 *  on a table can't bake AI cover points. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Case")
	TObjectPtr<UStaticMeshComponent> CaseMesh;

	/** Escape hatch, off by default. The kit interaction component traces ECC_Visibility from the
	 *  camera, so a blocking case body can in principle swallow that trace before it reaches an
	 *  item sunk deep in the foam. Tick this ONLY if PIE actually shows that happening — the first
	 *  fix is the slot's nudge transform (lift the item clear of the foam), because ticking this
	 *  also makes the case body transparent to bullets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Case")
	bool bIgnoreInteractionTrace = false;

	/** Which cutouts this placed case actually holds. Picked from a dropdown of slot component
	 *  SlotIds; duplicates are ignored and unknown names are logged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Case", meta = (GetOptions = "GetSlotIdOptions"))
	TArray<FName> FilledSlots;

	// Two hooks, and the difference is the whole point — getting them the wrong way round is the
	// easy mistake. A Blueprint pickup's construction script runs INSIDE FinishSpawning, so
	// anything that must beat that script has to be set in the pre-finish hook; anything that
	// operates on the item's finished components has to wait for the post-spawn one.

	/** Fired on authority in the gap between SpawnActorDeferred and FinishSpawning — BEFORE the
	 *  item's construction script and BeginPlay. Set flags here: this is the only window in which
	 *  the attachment pickup's lie-flat behaviour (its script rotates tall meshes onto their side
	 *  and recentres them, which fights every foam cutout) can still be switched off. A pure-BP
	 *  pickup has no components yet at this point — variables only. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Case")
	void OnCaseItemPreFinish(AActor* Item, FName SlotId);

	/** Fired on authority once the item is fully spawned and seated — AFTER its construction
	 *  script and BeginPlay. Touch components here: shrinking the item's interaction collider
	 *  (foam sockets sit ~11 cm apart) is what this exists for. Too late to stop anything the
	 *  construction script does — that is OnCaseItemPreFinish. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Case")
	void OnCaseItemSpawned(AActor* Item, FName SlotId);

private:
	/** Weak: pickups destroy themselves when collected. */
	TArray<TWeakObjectPtr<AActor>> SpawnedItems;

	/** Logs warnings for slot components with blank or duplicate SlotIds. */
	void ValidateSlotIds(TConstArrayView<UWeaponCaseSlotComponent*> AllSlots) const;

	/** De-duplicated resolution of FilledSlots against slot components, warning on unknowns. */
	void ResolveFilledSlots(TArray<UWeaponCaseSlotComponent*>& OutSlots) const;

	/** Applies bIgnoreInteractionTrace + the CoverGen opt-out. Runs from OnConstruction rather
	 *  than the constructor: a BP subclass's property defaults aren't loaded onto the CDO yet. */
	void ApplyCaseMeshCollision();

	void SpawnSlotItems();

	/** Attaches a spawned pickup to its slot component. No-op until the item has a root. */
	void SeatItemInSlot(AActor& Item, UWeaponCaseSlotComponent& SlotComp);
};
