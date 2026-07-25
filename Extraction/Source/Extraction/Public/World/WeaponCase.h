// AWeaponCase — designer-placed open weapon case whose foam cutouts hold real pickup actors.
// Two levels of authoring: slot definitions are fixed per case TYPE (class defaults, authored
// once in the BP), while a placed INSTANCE only ticks which of those slots are filled.
// In the editor the filled slots show preview meshes; on BeginPlay the authority destroys the
// previews and spawns the real pickup actors attached to the same socket + offset.
// Meshes and pickup classes are assigned in the Blueprint subclass — C++ stays asset-agnostic.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "WeaponCase.generated.h"

class USceneComponent;
class USkeletalMesh;
class UStaticMesh;
class UStaticMeshComponent;

/** One foam cutout: what sits in it, where that sits, and what it looks like before play. */
USTRUCT(BlueprintType)
struct FWeaponCaseSlot
{
	GENERATED_BODY()

	/** Designer-facing name. This is what a placed case's FilledSlots dropdown offers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Case|Slot")
	FName SlotId;

	/** Socket on the case mesh. None = parent straight to the case mesh and let Offset place it
	 *  (the hand-aligned injector pen in the assault-rifle case is authored that way). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Case|Slot")
	FName SocketName;

	/** Pickup actor spawned into this cutout on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Case|Slot")
	TSubclassOf<AActor> PickupClass;

	/** Editor preview only — assign whichever of the two matches the pickup, never both. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Case|Slot")
	TObjectPtr<UStaticMesh> PreviewMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Case|Slot")
	TObjectPtr<USkeletalMesh> PreviewSkeletalMesh;

	/** Relative to the socket, or to the case mesh itself when SocketName is None.
	 *  Non-uniform scale is supported and expected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Case|Slot")
	FTransform Offset;
};

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking))
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

	/** Every cutout this case type has. Authored once on the BP, not per placed instance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Case")
	TArray<FWeaponCaseSlot> SlotDefinitions;

	/** Which cutouts this placed case actually holds. Picked from a dropdown of SlotDefinitions
	 *  names; duplicates are ignored and unknown names are logged. */
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
	/** Tag stamped on every preview component. This — not the array below — is the identity
	 *  ClearPreviewComponents sweeps on, because the array does not survive an editor undo. */
	static const FName PreviewComponentTag;

	/** Editor dressing only, never saved into the level and never created in a game world.
	 *  Bookkeeping convenience only: Transient still round-trips through the transaction buffer,
	 *  so an undo can hand this back stale. The tag sweep is the source of truth. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> PreviewComponents;

	/** Weak: pickups destroy themselves when collected. */
	TArray<TWeakObjectPtr<AActor>> SpawnedItems;

	const FWeaponCaseSlot* FindSlot(FName SlotId) const;

	/** De-duplicated resolution of FilledSlots against SlotDefinitions, warning on unknowns. */
	void ResolveFilledSlots(TArray<const FWeaponCaseSlot*>& OutSlots) const;

	/** World transform of a slot's seat: Offset applied on top of the socket (or the case mesh). */
	FTransform GetSlotWorldTransform(const FWeaponCaseSlot& Slot) const;

	/** Applies bIgnoreInteractionTrace + the CoverGen opt-out. Runs from OnConstruction rather
	 *  than the constructor: a BP subclass's property defaults aren't loaded onto the CDO yet. */
	void ApplyCaseMeshCollision();

	void ClearPreviewComponents();
	void BuildPreviewComponents();
	void SpawnSlotItems();

	/** Attaches a spawned pickup to its cutout. No-op until the item has a root component. */
	void SeatItemInSlot(AActor& Item, const FWeaponCaseSlot& Slot);
};
