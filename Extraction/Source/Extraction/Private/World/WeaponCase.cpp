// AWeaponCase implementation.

#include "World/WeaponCase.h"

#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeaponCase, Log, All);

const FName AWeaponCase::PreviewComponentTag(TEXT("WeaponCaseSlotPreview"));

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

	// Unconditional: a PIE duplicate can carry previews in, and a re-run must never stack them.
	ClearPreviewComponents();

	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld()) return;

	BuildPreviewComponents();
}

void AWeaponCase::BeginPlay()
{
	Super::BeginPlay();

	ClearPreviewComponents();

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
	Options.Reserve(SlotDefinitions.Num());

	for (const FWeaponCaseSlot& Slot : SlotDefinitions)
	{
		if (Slot.SlotId.IsNone()) continue;
		Options.AddUnique(Slot.SlotId.ToString());
	}

	return Options;
}

const FWeaponCaseSlot* AWeaponCase::FindSlot(FName SlotId) const
{
	return SlotDefinitions.FindByPredicate(
		[SlotId](const FWeaponCaseSlot& Slot) { return Slot.SlotId == SlotId; });
}

void AWeaponCase::ResolveFilledSlots(TArray<const FWeaponCaseSlot*>& OutSlots) const
{
	OutSlots.Reset();
	OutSlots.Reserve(FilledSlots.Num());

	TSet<FName> Seen;
	Seen.Reserve(FilledSlots.Num());

	for (const FName& SlotId : FilledSlots)
	{
		if (SlotId.IsNone()) continue;

		bool bAlreadySeen = false;
		Seen.Add(SlotId, &bAlreadySeen);
		if (bAlreadySeen) continue;

		const FWeaponCaseSlot* Slot = FindSlot(SlotId);
		if (!Slot)
		{
			UE_LOG(LogWeaponCase, Warning, TEXT("'%s': FilledSlots entry '%s' matches no slot definition."),
				*GetName(), *SlotId.ToString());
			continue;
		}

		if (!Slot->SocketName.IsNone() && IsValid(CaseMesh) && !CaseMesh->DoesSocketExist(Slot->SocketName))
		{
			UE_LOG(LogWeaponCase, Warning, TEXT("'%s': slot '%s' names socket '%s', which the case mesh does not have."),
				*GetName(), *SlotId.ToString(), *Slot->SocketName.ToString());
		}

		OutSlots.Add(Slot);
	}
}

FTransform AWeaponCase::GetSlotWorldTransform(const FWeaponCaseSlot& Slot) const
{
	if (!IsValid(CaseMesh)) return GetActorTransform();

	const FTransform Base = Slot.SocketName.IsNone()
		? CaseMesh->GetComponentTransform()
		: CaseMesh->GetSocketTransform(Slot.SocketName, RTS_World);

	return Slot.Offset * Base;
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

void AWeaponCase::ClearPreviewComponents()
{
	// Sweep by tag, never by the array: UPROPERTY(Transient) is still written to the transaction
	// buffer (ShouldSerializeValue only skips transients for persistent archives), so an editor
	// undo hands back the PRE-edit array — components already destroyed — while the live previews
	// from the newer construction run stay registered. The tag rides the components themselves.
	TInlineComponentArray<USceneComponent*> SceneComponents(this);
	for (USceneComponent* Component : SceneComponents)
	{
		if (!IsValid(Component) || !Component->ComponentHasTag(PreviewComponentTag)) continue;
		Component->DestroyComponent();
	}

	PreviewComponents.Reset();
}

void AWeaponCase::BuildPreviewComponents()
{
	if (!IsValid(CaseMesh)) return;

	TArray<const FWeaponCaseSlot*> Slots;
	ResolveFilledSlots(Slots);
	if (Slots.IsEmpty()) return;

	PreviewComponents.Reserve(Slots.Num());

	for (const FWeaponCaseSlot* Slot : Slots)
	{
		UMeshComponent* Preview = nullptr;
		if (IsValid(Slot->PreviewMesh))
		{
			UStaticMeshComponent* StaticPreview = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			StaticPreview->SetStaticMesh(Slot->PreviewMesh);
			Preview = StaticPreview;
		}
		else if (IsValid(Slot->PreviewSkeletalMesh))
		{
			USkeletalMeshComponent* SkeletalPreview = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient);
			SkeletalPreview->SetSkeletalMeshAsset(Slot->PreviewSkeletalMesh);
			Preview = SkeletalPreview;
		}

		if (!Preview) continue;

		Preview->ComponentTags.Add(PreviewComponentTag);
		Preview->bIsEditorOnly = true;
		Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Preview->SetCanEverAffectNavigation(false);
		Preview->SetHiddenInGame(true);
		Preview->SetupAttachment(CaseMesh, Slot->SocketName);
		Preview->SetRelativeTransform(Slot->Offset);
		Preview->RegisterComponent();

		PreviewComponents.Add(Preview);
	}
}

void AWeaponCase::SpawnSlotItems()
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(CaseMesh)) return;

	TArray<const FWeaponCaseSlot*> Slots;
	ResolveFilledSlots(Slots);
	if (Slots.IsEmpty()) return;

	SpawnedItems.Reserve(Slots.Num());

	for (const FWeaponCaseSlot* Slot : Slots)
	{
		if (!IsValid(Slot->PickupClass.Get()))
		{
			UE_LOG(LogWeaponCase, Warning, TEXT("'%s': slot '%s' has no PickupClass — cutout left empty."),
				*GetName(), *Slot->SlotId.ToString());
			continue;
		}

		const FTransform SlotTransform = GetSlotWorldTransform(*Slot);
		AActor* Item = World->SpawnActorDeferred<AActor>(
			Slot->PickupClass, SlotTransform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(Item)) continue;

		// A Blueprint pickup's construction script runs INSIDE FinishSpawning, so this gap is the
		// only place its lie-flat behaviour can still be switched off. Fire before seating: the
		// hook sets variables the script is about to read, and a pure-BP item has no root yet.
		OnCaseItemPreFinish(Item, Slot->SlotId);

		// Seat BEFORE FinishSpawning so a pickup with a NATIVE root sees GetAttachParentActor()
		// inside its own BeginPlay — that is what makes the ammo box skip its ground-settle and
		// its despawn timer. A pure-Blueprint pickup has no root until its SCS runs inside
		// FinishSpawning, so seat again after: the second pass also beats any construction-script
		// repositioning.
		SeatItemInSlot(*Item, *Slot);
		Item->FinishSpawning(SlotTransform);
		if (!IsValid(Item)) continue;
		SeatItemInSlot(*Item, *Slot);

		SpawnedItems.Add(Item);
		OnCaseItemSpawned(Item, Slot->SlotId);
	}
}

void AWeaponCase::SeatItemInSlot(AActor& Item, const FWeaponCaseSlot& Slot)
{
	if (!Item.GetRootComponent() || !IsValid(CaseMesh)) return;

	Item.AttachToComponent(CaseMesh, FAttachmentTransformRules::SnapToTargetIncludingScale, Slot.SocketName);
	Item.SetActorRelativeTransform(Slot.Offset);
}
