// AWeaponCase implementation.

#include "World/WeaponCase.h"
#include "UI/LootMarkerComponent.h"
#include "World/WeaponCaseSlotComponent.h"

#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#endif

// LogWeaponCase is DECLARE'd in WeaponCaseSlotComponent.h, DEFINE'd in WeaponCaseSlotComponent.cpp.

AWeaponCase::AWeaponCase()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CaseMesh"));
	CaseMesh->SetupAttachment(SceneRoot);
	// Native default only — ApplyCaseMeshCollision re-applies both of these once the BP subclass's
	// bIgnoreInteractionTrace is actually readable.
	CaseMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	CaseMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel1 /* CoverGen — DefaultEngine.ini */, ECR_Ignore);

	// Widget class, Z offset and max distance are all designer-facing on the component itself, so
	// nothing is stamped here — the BP subclass owns the look.
	LootMarker = CreateDefaultSubobject<ULootMarkerComponent>(TEXT("LootMarker"));
	LootMarker->SetupAttachment(SceneRoot);
}

void AWeaponCase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyCaseMeshCollision();

	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld()) return;

	// BP class preview (EditorPreview world): show every slot so designers can see and
	// position meshes in the Blueprint viewport.  Level editor (Editor world): show only
	// the slots this placed instance actually holds.
	if (!World || World->WorldType != EWorldType::Editor)
	{
		TInlineComponentArray<UWeaponCaseSlotComponent*> SlotComps(this);
		for (UWeaponCaseSlotComponent* SlotComp : SlotComps)
		{
			if (IsValid(SlotComp)) SlotComp->SetPreviewVisible(true);
		}
		return;
	}

	// Level editor: show previews only for filled slots.
	TSet<FName> FilledSet;
	FilledSet.Reserve(FilledSlots.Num());
	for (const FName& Id : FilledSlots)
	{
		if (!Id.IsNone()) FilledSet.Add(Id);
	}

	TInlineComponentArray<UWeaponCaseSlotComponent*> SlotComps(this);
	for (UWeaponCaseSlotComponent* SlotComp : SlotComps)
	{
		if (!IsValid(SlotComp)) continue;
		SlotComp->SetPreviewVisible(FilledSet.Contains(SlotComp->SlotId));
	}
}

void AWeaponCase::BeginPlay()
{
	Super::BeginPlay();

	// Hide EVERY slot's preview before spawning the real items.  The cross-actor guard in
	// SetPreviewVisible makes this order-independent, but hiding first avoids a flicker where
	// previews and real pickups are both visible between BeginPlay and the spawn.
	TInlineComponentArray<UWeaponCaseSlotComponent*> SlotComps(this);
	for (UWeaponCaseSlotComponent* SlotComp : SlotComps)
	{
		if (IsValid(SlotComp)) SlotComp->SetPreviewVisible(false);
	}

	if (!HasAuthority()) return;
	SpawnSlotItems();

	// A BP spawn hook can destroy the case mid-spawn; SpawnSlotItems bails out when it does.
	if (IsActorBeingDestroyed()) return;

	// An item that is collected by being HIDDEN rather than destroyed fires no OnDestroyed, so the
	// marker also polls the case on its own availability timer. The OnDestroyed bindings stay: they
	// clear the marker the instant the last item dies instead of up to one poll interval later.
	if (IsValid(LootMarker))
	{
		LootMarker->SetAvailabilityQuery(
			FLootMarkerAvailabilityQuery::CreateUObject(this, &AWeaponCase::IsMarkerAvailable));
	}

	// A case whose slots were all empty (no FilledSlots, or no PickupClass on them) starts unmarked
	// rather than advertising loot it never held.
	RefreshLootMarker();
}

void AWeaponCase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Symmetry with the BeginPlay bindings. Both are hygiene rather than a leak fix -- an item
	// detached from a dying case would otherwise hold a stale binding back to it.
	for (const TWeakObjectPtr<AActor>& WeakItem : SpawnedItems)
	{
		if (AActor* Item = WeakItem.Get()) Item->OnDestroyed.RemoveAll(this);
	}

	if (IsValid(LootMarker)) LootMarker->ClearAvailabilityQuery();

	Super::EndPlay(EndPlayReason);
}

bool AWeaponCase::IsMarkerAvailable() const
{
	return !IsEmpty();
}

bool AWeaponCase::IsEmpty() const
{
	// SpawnSlotItems is authority-only, so a client's list is empty from BeginPlay onward. Report
	// "not empty" rather than firing every picked-clean reaction the moment the level loads.
	if (!HasAuthority()) return false;

	for (const TWeakObjectPtr<AActor>& WeakItem : SpawnedItems)
	{
		const AActor* Item = WeakItem.Get();
		if (!IsValid(Item)) continue;

		// Destruction is NOT this project's loot convention: ALootPickup hides itself, disables
		// collision, then SetLifeSpan(2.f), and a kit BP pickup may only hide itself and never die
		// at all. A hidden or fully collision-disabled item has been taken -- counting it as still
		// present leaves the case marker lit for two seconds, or forever.
		// Both conditions, not either: a seated case item can legitimately spawn with actor collision
		// off (it is parented into foam and must not block the case), and treating that alone as
		// "collected" would leave every case reading empty and its marker permanently dark.
		if (Item->IsHidden() && !Item->GetActorEnableCollision()) continue;

		return false;
	}
	return true;
}

TArray<FString> AWeaponCase::GetSlotIdOptions() const
{
	TArray<FString> Options;

	// Live slot components exist on placed actors and in PIE.
	TInlineComponentArray<UWeaponCaseSlotComponent*> SlotComps(this);
	Options.Reserve(SlotComps.Num());
	for (const UWeaponCaseSlotComponent* SlotComp : SlotComps)
	{
		if (!IsValid(SlotComp) || SlotComp->SlotId.IsNone()) continue;
		Options.AddUnique(SlotComp->SlotId.ToString());
	}

	if (!Options.IsEmpty()) return Options;

#if WITH_EDITOR
	// CDO has no SCS components -- walk the SimpleConstructionScript node tree up the
	// UBlueprintGeneratedClass super-class chain to populate the dropdown on a Blueprint's
	// Class Defaults tab.  GetActualComponentTemplate resolves InheritableComponentHandler
	// overrides so a child BP that changes SlotId shows the child's value, not the parent's.
	UBlueprintGeneratedClass* LeafBPGC = Cast<UBlueprintGeneratedClass>(GetClass());
	const UBlueprintGeneratedClass* BPGC = LeafBPGC;
	while (BPGC)
	{
		if (const USimpleConstructionScript* SCS = BPGC->SimpleConstructionScript)
		{
			for (const USCS_Node* Node : SCS->GetAllNodes())
			{
				if (!Node) continue;
				const auto* Template = Cast<UWeaponCaseSlotComponent>(
					Node->GetActualComponentTemplate(LeafBPGC));
				if (!Template || Template->SlotId.IsNone()) continue;
				Options.AddUnique(Template->SlotId.ToString());
			}
		}
		BPGC = Cast<UBlueprintGeneratedClass>(BPGC->GetSuperClass());
	}
#endif

	return Options;
}

void AWeaponCase::ValidateSlotIds(TConstArrayView<UWeaponCaseSlotComponent*> AllSlots) const
{
	TSet<FName> SeenIds;
	SeenIds.Reserve(AllSlots.Num());
	for (const UWeaponCaseSlotComponent* Comp : AllSlots)
	{
		if (!IsValid(Comp)) continue;
		if (Comp->SlotId.IsNone())
		{
			UE_LOG(LogWeaponCase, Warning, TEXT("'%s': slot '%s' has no SlotId."),
				*GetName(), *Comp->GetName());
			continue;
		}
		bool bDupe = false;
		SeenIds.Add(Comp->SlotId, &bDupe);
		if (bDupe)
		{
			UE_LOG(LogWeaponCase, Warning, TEXT("'%s': duplicate SlotId '%s' on '%s'."),
				*GetName(), *Comp->SlotId.ToString(), *Comp->GetName());
		}
	}
}

void AWeaponCase::ResolveFilledSlots(TArray<UWeaponCaseSlotComponent*>& OutSlots) const
{
	OutSlots.Reset();
	OutSlots.Reserve(FilledSlots.Num());
	TInlineComponentArray<UWeaponCaseSlotComponent*> AllSlots(this);

	ValidateSlotIds(AllSlots);

	TSet<FName> Seen;
	Seen.Reserve(FilledSlots.Num());
	for (const FName& Id : FilledSlots)
	{
		if (Id.IsNone()) continue;
		bool bDupe = false;
		Seen.Add(Id, &bDupe);
		if (bDupe) continue;

		UWeaponCaseSlotComponent** Found = AllSlots.FindByPredicate(
			[Id](const UWeaponCaseSlotComponent* C) { return IsValid(C) && C->SlotId == Id; });
		if (!Found)
		{
			UE_LOG(LogWeaponCase, Warning, TEXT("'%s': FilledSlots entry '%s' matches no slot component."),
				*GetName(), *Id.ToString());
			continue;
		}
		OutSlots.Add(*Found);
	}
}

void AWeaponCase::ApplyCaseMeshCollision()
{
	if (!IsValid(CaseMesh)) return;

	// Solid to bullets: weapon hitscan traces ECC_Visibility, and a hard case pellets pass
	// straight through is a visible bug. The seated items protrude above the foam, so their own
	// interaction boxes are expected to win the kit's camera trace on approach — bIgnoreInteractionTrace
	// is the fallback for a case where they don't.
	CaseMesh->SetCollisionResponseToChannel(ECC_Visibility, bIgnoreInteractionTrace ? ECR_Ignore : ECR_Block);

	// Never negotiable: a gun case on a table would otherwise bake AI cover points out of a prop
	// nothing can actually take cover behind.
	CaseMesh->SetCollisionResponseToChannel(ECC_GameTraceChannel1 /* CoverGen — DefaultEngine.ini */, ECR_Ignore);
}

void AWeaponCase::SpawnSlotItems()
{
	UWorld* World = GetWorld();
	if (!World) return;

	TArray<UWeaponCaseSlotComponent*> Slots;
	ResolveFilledSlots(Slots);
	if (Slots.IsEmpty()) return;

	SpawnedItems.Reserve(Slots.Num());

	for (UWeaponCaseSlotComponent* SlotComp : Slots)
	{
		// BP hooks can destroy the case actor or invalidate slots mid-iteration.
		if (!IsValid(SlotComp)) continue;

		if (!IsValid(SlotComp->PickupClass.Get()))
		{
			UE_LOG(LogWeaponCase, Warning, TEXT("'%s': slot '%s' has no PickupClass -- cutout left empty."),
				*GetName(), *SlotComp->SlotId.ToString());
			continue;
		}

		const FTransform SlotTransform = SlotComp->GetComponentTransform();
		AActor* Item = World->SpawnActorDeferred<AActor>(
			SlotComp->PickupClass, SlotTransform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(Item)) continue;

		OnCaseItemPreFinish(Item, SlotComp->SlotId);
		if (IsActorBeingDestroyed()) return;

		// Seat before FinishSpawning: a no-op for SCS-rooted pickups (GetRootComponent() is
		// null until FinishSpawning wires the SCS), but correct for a future native-root
		// pickup whose BeginPlay reads GetAttachParentActor().  Seat again after: the
		// authoritative pass -- the item now has its full component tree, and this beats any
		// construction-script repositioning.
		SeatItemInSlot(*Item, *SlotComp);
		Item->FinishSpawning(SlotTransform);
		if (!IsValid(Item)) continue;
		SeatItemInSlot(*Item, *SlotComp);

		SpawnedItems.Add(Item);

		// Pickups Destroy() themselves on collect, so their own destruction is the only "case was
		// looted" signal there is. Authority-only, matching the spawn itself.
		Item->OnDestroyed.AddDynamic(this, &AWeaponCase::HandleItemDestroyed);

		OnCaseItemSpawned(Item, SlotComp->SlotId);
		if (IsActorBeingDestroyed()) return;
	}
}

void AWeaponCase::HandleItemDestroyed(AActor* DestroyedActor)
{
	// OnDestroyed broadcasts from inside UWorld::DestroyActor BEFORE the actor is marked garbage, so
	// the dying item's weak pointer can still read valid right here -- leave it in the array and
	// IsEmpty() reports "not empty" on the very last item and the marker never clears. Drop it by
	// pointer identity, and sweep out any entry that has already gone stale while we are here.
	SpawnedItems.RemoveAll([DestroyedActor](const TWeakObjectPtr<AActor>& Item)
	{
		return !Item.IsValid() || Item.Get() == DestroyedActor;
	});

	RefreshLootMarker();
}

void AWeaponCase::RefreshLootMarker()
{
	// IsEmpty() is deliberately authority-gated and lies off authority, so the marker follows it.
	if (!HasAuthority() || !IsValid(LootMarker)) return;
	LootMarker->SetMarkerAvailable(!IsEmpty());
}

void AWeaponCase::SeatItemInSlot(AActor& Item, UWeaponCaseSlotComponent& SlotComp)
{
	if (!Item.GetRootComponent()) return;

	// Attach to the slot component -- SnapToTargetIncludingScale preserves the slot's authored
	// scale (e.g. the stim pen's 0.59/0.55/0.50 shrink).  A Static-mobility root under a
	// non-Static parent is silently refused; warn and bail rather than teleport to origin.
	if (!Item.AttachToComponent(&SlotComp, FAttachmentTransformRules::SnapToTargetIncludingScale))
	{
		UE_LOG(LogWeaponCase, Warning,
			TEXT("Failed to attach '%s' to slot '%s' on '%s' -- check mobility."),
			*Item.GetName(), *SlotComp.GetName(), *GetName());
		return;
	}

	// Weapon skeletal meshes (SKM_InfimaAR_KitRig etc.) ship with no PhysicsAsset, so
	// bSimulatePhysics = true on their mesh creates no bodies and nothing actually simulates
	// today.  This is defensive: if a PhysicsAsset is ever added, a simulating body would
	// ignore its attach parent and tumble off the foam cutout.
	DisableItemPhysics(Item);

	UMeshComponent* DisplayMesh = nullptr;
	FTransform AuthoredOffset;
	FTransform AppliedRelative;
	CorrectMeshOffset(Item, SlotComp, DisplayMesh, AuthoredOffset, AppliedRelative);

	VerifySeatTransform(Item, SlotComp, DisplayMesh, AuthoredOffset, AppliedRelative);
}

void AWeaponCase::CorrectMeshOffset(AActor& Item, UWeaponCaseSlotComponent& SlotComp,
	UMeshComponent*& OutDisplayMesh, FTransform& OutAuthoredOffset, FTransform& OutAppliedRelative)
{
	// The pickup's visible mesh often sits at a non-identity AUTHORED offset from its actor
	// root (weapon meshes carry +90 deg pitch and a small Z lift in their SCS template).
	// Seat the *mesh* on the slot by applying the inverse of that authored offset.
	//
	// IMPORTANT: we read SCS-template (archetype) transforms, NOT live transforms.
	// BP_Attachment_Pickup::FlattenDisplayMesh (construction script) recentres and optionally
	// rotates the display mesh at runtime.  Using the live transform would invert that
	// flatten, regressing every attachment cutout to a wrong orientation.
	OutDisplayMesh = FindItemDisplayMesh(Item);
	OutAuthoredOffset = FTransform::Identity;
	OutAppliedRelative = FTransform::Identity;

	if (!OutDisplayMesh)
	{
		Item.SetActorRelativeTransform(FTransform::Identity);
		UE_LOG(LogWeaponCase, Warning,
			TEXT("No display mesh on '%s' for slot '%s' -- seating root at slot origin."),
			*Item.GetName(), *SlotComp.SlotId.ToString());
		return;
	}

	// Strip scale before inverting so the inverse's translation is not contaminated by 1/S.
	// The slot's own scale (applied by SnapToTargetIncludingScale) remains the sole source of
	// item scaling.
	OutAuthoredOffset = ComposeAuthoredMeshOffset(*OutDisplayMesh, *Item.GetRootComponent());
	OutAuthoredOffset.SetScale3D(FVector::OneVector);
	OutAppliedRelative = OutAuthoredOffset.Inverse();
	Item.SetActorRelativeTransform(OutAppliedRelative);
}

void AWeaponCase::VerifySeatTransform(
	const AActor& Item,
	const UWeaponCaseSlotComponent& SlotComp,
	const UMeshComponent* DisplayMesh,
	const FTransform& AuthoredOffset,
	const FTransform& AppliedRelative) const
{
	if (!IsValid(DisplayMesh)) return;

	const FTransform SlotWorld = SlotComp.GetComponentTransform();
	const FTransform MeshWorld = DisplayMesh->GetComponentTransform();
	constexpr double LocationToleranceUU = 0.1;
	constexpr double QuatTolerance = 0.01;
	const bool bLocOK = MeshWorld.GetLocation().Equals(SlotWorld.GetLocation(), LocationToleranceUU);
	const bool bRotOK = MeshWorld.GetRotation().Equals(SlotWorld.GetRotation(), QuatTolerance);

	if (bLocOK && bRotOK)
	{
		UE_LOG(LogWeaponCase, Verbose, TEXT("SeatVerify OK | Slot='%s' Item='%s' Mesh='%s'"),
			*SlotComp.SlotId.ToString(), *Item.GetName(), *DisplayMesh->GetName());
		return;
	}

	const FTransform RootRelative = Item.GetRootComponent()
		? Item.GetRootComponent()->GetRelativeTransform() : FTransform::Identity;
	UE_LOG(LogWeaponCase, Warning,
		TEXT("SeatVerify MISMATCH | Slot='%s' Item='%s' Mesh='%s' LocOK=%d RotOK=%d\n")
		TEXT("  SlotWorld:  Loc=%s Rot=%s Quat=%s\n")
		TEXT("  MeshWorld:  Loc=%s Rot=%s Quat=%s\n")
		TEXT("  Authored:   Loc=%s Rot=%s Quat=%s\n")
		TEXT("  Applied:    Loc=%s Rot=%s Quat=%s\n")
		TEXT("  RootRel:    Loc=%s Rot=%s Quat=%s"),
		*SlotComp.SlotId.ToString(), *Item.GetName(), *DisplayMesh->GetName(), bLocOK, bRotOK,
		*SlotWorld.GetLocation().ToString(), *SlotWorld.Rotator().ToString(), *SlotWorld.GetRotation().ToString(),
		*MeshWorld.GetLocation().ToString(), *MeshWorld.Rotator().ToString(), *MeshWorld.GetRotation().ToString(),
		*AuthoredOffset.GetLocation().ToString(), *AuthoredOffset.Rotator().ToString(), *AuthoredOffset.GetRotation().ToString(),
		*AppliedRelative.GetLocation().ToString(), *AppliedRelative.Rotator().ToString(), *AppliedRelative.GetRotation().ToString(),
		*RootRelative.GetLocation().ToString(), *RootRelative.Rotator().ToString(), *RootRelative.GetRotation().ToString());
}

FTransform AWeaponCase::ResolveAuthoredLinkTransform(const USceneComponent& Comp)
{
	const auto* Archetype = Cast<USceneComponent>(Comp.GetArchetype());

	// A genuine SCS component's archetype is the SCS template, not the class CDO.
	// UserConstructionScript components resolve to the CDO instead.
	if (IsValid(Archetype) &&
		(Archetype->HasAnyFlags(RF_ClassDefaultObject) ||
		 Comp.CreationMethod == EComponentCreationMethod::UserConstructionScript))
	{
		UE_LOG(LogWeaponCase, Warning,
			TEXT("ComposeAuthoredMeshOffset: '%s' on '%s' archetype resolved to class CDO "
			     "(CreationMethod %d) -- offset degrades to identity."),
			*Comp.GetName(),
			IsValid(Comp.GetOwner()) ? *Comp.GetOwner()->GetName() : TEXT("<no owner>"),
			static_cast<int32>(Comp.CreationMethod));
	}

	// Socket attachment or absolute-transform flags add engine-side
	// composition this relative-only walk does not replicate.
	if (Comp.GetAttachSocketName() != NAME_None ||
		Comp.IsUsingAbsoluteLocation() || Comp.IsUsingAbsoluteRotation() ||
		Comp.IsUsingAbsoluteScale())
	{
		UE_LOG(LogWeaponCase, Warning,
			TEXT("ComposeAuthoredMeshOffset: '%s' uses socket '%s' or absolute "
			     "transform flags -- composed offset may be wrong."),
			*Comp.GetName(), *Comp.GetAttachSocketName().ToString());
	}

	const USceneComponent* Source = IsValid(Archetype) ? Archetype : &Comp;
	return FTransform(Source->GetRelativeRotation(),
	                  Source->GetRelativeLocation(),
	                  Source->GetRelativeScale3D());
}

FTransform AWeaponCase::ComposeAuthoredMeshOffset(
	const UMeshComponent& DisplayMesh, const USceneComponent& Root)
{
	if (&DisplayMesh == &Root) return FTransform::Identity;

	// Walk the live attach-parent chain from the display mesh up to (but excluding) Root,
	// composing each link's ARCHETYPE relative transform.  For SCS-created components the
	// archetype is the SCS template -- the designer-authored transform before any construction-
	// script repositioning (e.g. BP_Attachment_Pickup::FlattenDisplayMesh).  When the template
	// relative is identity (attachment pickups), the composed result is identity and the
	// inverse in CorrectMeshOffset becomes a no-op, preserving the kit's flatten.
	FTransform Composed = FTransform::Identity;
	const USceneComponent* Current = &DisplayMesh;
	constexpr int32 MaxHops = 16;

	for (int32 Hop = 0; Hop < MaxHops; ++Hop)
	{
		Composed = Composed * ResolveAuthoredLinkTransform(*Current);

		const USceneComponent* Parent = Current->GetAttachParent();
		if (Parent == &Root) return Composed;
		if (!IsValid(Parent))
		{
			UE_LOG(LogWeaponCase, Warning,
				TEXT("ComposeAuthoredMeshOffset: parent chain from '%s' broke before reaching "
				     "root on '%s' -- returning partial offset."),
				*Current->GetName(),
				IsValid(Current->GetOwner()) ? *Current->GetOwner()->GetName() : TEXT("<no owner>"));
			return Composed;
		}
		Current = Parent;
	}

	UE_LOG(LogWeaponCase, Warning,
		TEXT("ComposeAuthoredMeshOffset: parent chain exceeded %d hops without reaching root "
		     "-- returning best-effort offset."),
		MaxHops);
	return Composed;
}

UMeshComponent* AWeaponCase::FindItemDisplayMesh(AActor& Item) const
{
	USceneComponent* Root = Item.GetRootComponent();
	if (!Root) return nullptr;

	// The root component itself may be the display mesh (e.g. a pickup whose skeletal mesh
	// is the root).  Check it first before walking children.
	if (!Root->IsA<UWidgetComponent>()
		&& (Root->IsA<USkeletalMeshComponent>() || Root->IsA<UStaticMeshComponent>()))
	{
		return Cast<UMeshComponent>(Root);
	}

	// Pass 0: direct children only (guarantees shallowest -- one directly parented to root --
	// so children-of-children like Handguard under Weapon never win over the weapon mesh
	// itself).  Pass 1: all descendants depth-first as a fallback.
	// UWidgetComponent is excluded: it inherits UMeshComponent, and the kit's interaction
	// widget sits on every pickup.  Components owned by a different actor (the child-actor
	// interaction area) are also skipped.
	// Plain TArray, not TInlineComponentArray: GetChildrenComponents takes a default-allocator
	// TArray&, which an inline-allocator array cannot bind to.
	TArray<USceneComponent*> ChildComps;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		Root->GetChildrenComponents(Pass > 0, ChildComps);
		for (USceneComponent* Child : ChildComps)
		{
			if (!IsValid(Child) || Child->GetOwner() != &Item) continue;
			if (Child->IsA<UWidgetComponent>()) continue;
			if (Child->IsA<USkeletalMeshComponent>() || Child->IsA<UStaticMeshComponent>())
			{
				return Cast<UMeshComponent>(Child);
			}
		}
	}

	return nullptr;
}

void AWeaponCase::DisableItemPhysics(AActor& Item)
{
	TInlineComponentArray<UPrimitiveComponent*> Primitives(&Item);
	for (UPrimitiveComponent* Prim : Primitives)
	{
		// No IsSimulatingPhysics() gate: that queries GetBodyInstance(NAME_None), which returns
		// nullptr when the mesh has no PhysicsAsset -- exactly the weapon skeletal meshes this
		// targets.  SetSimulatePhysics(false) safely clears BodyInstance.bSimulatePhysics
		// whether or not a physics body exists.
		if (IsValid(Prim)) Prim->SetSimulatePhysics(false);
	}
}
