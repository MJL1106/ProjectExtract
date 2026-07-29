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

class UMeshComponent;
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

	/** Seats a spawned pickup on its slot component: attaches to the slot, defensively kills
	 *  physics simulation, then delegates mesh-offset correction to CorrectMeshOffset.
	 *  No-op until the item has a root. */
	void SeatItemInSlot(AActor& Item, UWeaponCaseSlotComponent& SlotComp);

	/** Offsets a seated item so its display mesh (not its root) coincides with the slot's
	 *  authored transform.  Derives the mesh offset from SCS-template (archetype) transforms
	 *  rather than live transforms, so construction-script repositioning (e.g.
	 *  BP_Attachment_Pickup::FlattenDisplayMesh) is not inverted.
	 *  Out params expose the intermediate values for diagnostic verification. */
	void CorrectMeshOffset(AActor& Item, UWeaponCaseSlotComponent& SlotComp,
		UMeshComponent*& OutDisplayMesh, FTransform& OutAuthoredOffset, FTransform& OutAppliedRelative);

	/** Post-seat diagnostic: compares the display mesh's final world transform against the
	 *  slot's world transform using quaternion rotation comparison (immune to gimbal lock).
	 *  Logs Warning on mismatch, Verbose on match.  Read-only -- never writes transforms. */
	void VerifySeatTransform(
		const AActor& Item,
		const UWeaponCaseSlotComponent& SlotComp,
		const UMeshComponent* DisplayMesh,
		const FTransform& AuthoredOffset,
		const FTransform& AppliedRelative) const;

	/** Composes the authored (SCS-template) relative transform from @p DisplayMesh up to
	 *  (but excluding) @p Root.  Uses the live attach hierarchy for structure but reads
	 *  each component's archetype for the relative transform values.  Returns Identity when
	 *  DisplayMesh is the root or the parent chain is degenerate.
	 *
	 *  Precondition: every component in the chain attaches to socket NAME_None and uses
	 *  no absolute-location/rotation/scale flags.  The walk composes relative transforms
	 *  only; socket offsets and absolute-flag semantics are not replicated.  Violations
	 *  are logged but do not abort the composition. */
	static FTransform ComposeAuthoredMeshOffset(const UMeshComponent& DisplayMesh, const USceneComponent& Root);

	/** Resolves a single component's authored (SCS-template) relative transform, logging
	 *  diagnostics when the archetype lookup degrades to the class CDO or when the
	 *  component violates ComposeAuthoredMeshOffset's attachment assumptions. */
	static FTransform ResolveAuthoredLinkTransform(const USceneComponent& Comp);

	/** Returns the item's display mesh (UStaticMeshComponent or USkeletalMeshComponent,
	 *  excluding UWidgetComponent).  Checks the root component first, then direct children
	 *  (shallowest), then all descendants depth-first as a fallback.  Skips components owned
	 *  by a different actor.  Returns nullptr if none found. */
	UMeshComponent* FindItemDisplayMesh(AActor& Item) const;

	/** Defensively clears bSimulatePhysics on every UPrimitiveComponent the item owns.
	 *  Current weapon skeletal meshes have no PhysicsAsset (so nothing actually simulates),
	 *  but this prevents future PhysicsAsset additions from breaking the foam-seat attach. */
	static void DisableItemPhysics(AActor& Item);
};
