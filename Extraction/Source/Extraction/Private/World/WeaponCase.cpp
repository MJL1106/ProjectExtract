// AWeaponCase implementation.

#include "World/WeaponCase.h"
#include "World/WeaponCaseSlotComponent.h"

#include "Components/StaticMeshComponent.h"
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
}

bool AWeaponCase::IsEmpty() const
{
	// SpawnSlotItems is authority-only, so a client's list is empty from BeginPlay onward. Report
	// "not empty" rather than firing every picked-clean reaction the moment the level loads.
	if (!HasAuthority()) return false;

	for (const TWeakObjectPtr<AActor>& Item : SpawnedItems)
	{
		if (Item.IsValid()) return false;
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

		// Seat before FinishSpawning: native-root pickups read GetAttachParentActor() in their
		// own BeginPlay.  Seat again after: pure-BP pickups get their root from SCS inside
		// FinishSpawning, and the second pass beats any construction-script repositioning.
		SeatItemInSlot(*Item, *SlotComp);
		Item->FinishSpawning(SlotTransform);
		if (!IsValid(Item)) continue;
		SeatItemInSlot(*Item, *SlotComp);

		SpawnedItems.Add(Item);
		OnCaseItemSpawned(Item, SlotComp->SlotId);
		if (IsActorBeingDestroyed()) return;
	}
}

void AWeaponCase::SeatItemInSlot(AActor& Item, UWeaponCaseSlotComponent& SlotComp)
{
	if (!Item.GetRootComponent()) return;

	// Attach to the slot component -- its transform IS the seat.  SnapToTargetIncludingScale
	// because existing slots carry non-uniform scale (the injection pen's 0.59/0.55/0.50 shrink
	// now rides on the slot component's own transform).
	// A Static-mobility root under a non-Static parent is silently refused, and without an
	// attach parent "relative" means world, teleporting the item to (0,0,0).
	if (!Item.AttachToComponent(&SlotComp, FAttachmentTransformRules::SnapToTargetIncludingScale))
	{
		UE_LOG(LogWeaponCase, Warning,
			TEXT("Failed to attach '%s' to slot '%s' on '%s' -- check mobility."),
			*Item.GetName(), *SlotComp.GetName(), *GetName());
		return;
	}

	Item.SetActorRelativeTransform(FTransform::Identity);
}
