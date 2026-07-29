// ALootContainer implementation.

#include "World/LootContainer.h"
#include "Audio/GameAudioSubsystem.h"
#include "Audio/SurfaceAudioBank.h"
#include "Game/MissionInventorySubsystem.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/Class.h"
#include "UI/LootMarkerComponent.h"
#include "World/InteractionEventSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogLootContainer, Log, All);

ALootContainer::ALootContainer()
{
	PrimaryActorTick.bCanEverTick = false;
	// Only bLooted crosses the wire, but the client's prompt scan reads it every frame it looks
	// at this volume — without it the "Search" prompt never clears on a dedicated server.
	bReplicates = true;

	InteractVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractVolume"));
	SetRootComponent(InteractVolume);
	InteractVolume->SetBoxExtent(FVector(50.f, 50.f, 50.f));
	InteractVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractVolume->SetCollisionObjectType(ECC_WorldStatic);
	// Interact channel ONLY. Blocking ECC_Visibility here would also block enemy line-of-sight,
	// companion muzzle clearance and cover generation — every one of those traces Visibility, and
	// an invisible box over a table would silently break all three.
	InteractVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractVolume->SetCollisionResponseToChannel(ExtractionInteraction::InteractTraceChannel, ECR_Block);
	InteractVolume->SetGenerateOverlapEvents(false);
	InteractVolume->SetCanEverAffectNavigation(false);
	InteractVolume->ShapeColor = FColor::Yellow;

#if WITH_EDITORONLY_DATA
	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	Billboard->SetupAttachment(InteractVolume);
	Billboard->bIsEditorOnly = true;
	Billboard->bIsScreenSizeScaled = true;
	Billboard->SetHiddenInGame(true);
#endif

	LootMarker = CreateDefaultSubobject<ULootMarkerComponent>(TEXT("LootMarker"));
	LootMarker->SetupAttachment(InteractVolume);
	LootMarker->MarkerWorldZOffset = 60.f;
}

void ALootContainer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ALootContainer, bLooted);
}

bool ALootContainer::CanLoot_Implementation() const
{
	return !bLooted && Contents.Num() > 0;
}

// --- IWorldInteractable ---

bool ALootContainer::CanWorldInteract_Implementation(AActor* /*Interactor*/) const
{
	return CanLootRespectingScriptOverride();
}

void ALootContainer::WorldInteract_Implementation(AActor* Interactor)
{
	// Execute_ rather than the _Implementation directly: a BP subclass that overrides Loot must
	// still win here, exactly as it did on the legacy ILootable branch.
	ILootable::Execute_Loot(this, Interactor);
}

FText ALootContainer::GetWorldInteractionPrompt_Implementation(AActor* /*Interactor*/) const
{
	return InteractPrompt;
}

float ALootContainer::GetWorldInteractHoldSeconds_Implementation(AActor* /*Interactor*/) const
{
	return 0.f;
}

bool ALootContainer::CanLootRespectingScriptOverride() const
{
	static const FName CanLootFunctionName = GET_FUNCTION_NAME_CHECKED(ILootable, CanLoot);
	return GetClass()->IsFunctionImplementedInScript(CanLootFunctionName)
		? ILootable::Execute_CanLoot(this)
		: CanLoot_Implementation();
}

void ALootContainer::Loot_Implementation(AActor* Looter)
{
	if (!HasAuthority()) return;
	if (!CanLootRespectingScriptOverride())
	{
		UE_LOG(LogLootContainer, Verbose, TEXT("%s: Loot called but nothing to loot"), *GetName());
		return;
	}

	bLooted = true;
	// Push it now: the looting client is standing in the volume with the prompt on screen.
	ForceNetUpdate();
	OnOpened(Looter);
	GrantAllContents();

	if (UGameAudioSubsystem* AudioSys = GetWorld()->GetSubsystem<UGameAudioSubsystem>())
	{
		if (const USurfaceAudioBank* Bank = AudioSys->GetBank())
			AudioSys->PlayAt(Bank->PickupLoot, GetActorLocation());
	}
#if WITH_DEV_AUTOMATION_TESTS
	++CompletionBroadcastCount;
#endif
	OnLootCompleted.Broadcast(this, Looter);

	// Raised HERE, on the success branch, rather than blanket from the player's interact commit:
	// a listener watching "the player used THAT actor" must not hear about a refused interaction.
	if (UInteractionEventSubsystem* Events = GetWorld() ? GetWorld()->GetSubsystem<UInteractionEventSubsystem>() : nullptr)
		Events->NotifyWorldInteract(this, Looter);

	if (IsValid(LootMarker))
	{
		LootMarker->RefreshMarkerNow();
	}

	UE_LOG(LogLootContainer, Log, TEXT("%s: looted by %s (%d grants)"),
		*GetName(), *GetNameSafe(Looter), Contents.Num());
}

void ALootContainer::MarkLootedForCheckpoint()
{
	if (!HasAuthority()) return;
	if (bLooted) return;

	// No ForceNetUpdate here, unlike the live loot path: this runs at level load with nobody
	// standing in the volume looking at a prompt, so there is nothing to push early to.
	bLooted = true;

	if (IsValid(LootMarker))
	{
		LootMarker->RefreshMarkerNow();
	}

	UWorld* World = GetWorld();

	// Visual state only — the drawer the player already pulled open stays open through the restart.
	// Deliberately NOT OnOpened: that one carries the slide, the SFX and the pickup theatre for a
	// search the player is doing right now, none of which belongs to a level load.
	//
	// Deferred one tick rather than called inline: this runs from the objective entry step's
	// BeginPlay, and actor BeginPlay order is unspecified. A container Blueprint that caches its
	// closed pose, or grabs a timeline or mesh reference, in its OWN BeginPlay would run AFTER the
	// hook and overwrite the restored pose — the emptied drawer reads as untouched, which is the
	// exact bug this hook exists to fix. Weakly captured: the container can be destroyed in the gap.
	if (World)
	{
		TWeakObjectPtr<ALootContainer> WeakSelf(this);
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakSelf]()
		{
			ALootContainer* Container = WeakSelf.Get();
			if (!IsValid(Container)) return;
#if WITH_DEV_AUTOMATION_TESTS
			++Container->RestoredLootedCount;
#endif
			Container->OnRestoredLooted();
		}));
	}

	UMissionInventorySubsystem* Inventory = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!Inventory) return;

	// Keycards only. They are kept rather than consumed, so a later beat's locked door still needs the
	// card back; ammo and stims were spent by the run that earned them. Granted NOW rather than with
	// the pose: the beat resuming on this card re-reads the inventory the moment it activates, which
	// is a tick before the deferred hook lands.
	for (const FLootGrant& Grant : Contents)
	{
		if (Grant.Type != ELootType::Keycard) continue;
		Inventory->RecordKeycard(Grant.KeycardId, /*bSilent=*/true);
	}

	UE_LOG(LogLootContainer, Log, TEXT("%s: marked looted by checkpoint resume (keycards re-granted)"), *GetName());
}

void ALootContainer::GrantAllContents()
{
	UWorld* World = GetWorld();
	UMissionInventorySubsystem* Subsystem = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!Subsystem) return;

	// Grants always target the player (default recipient) — the companion has no inventory.
	for (const FLootGrant& Grant : Contents)
		Subsystem->GrantLoot(Grant);
}
