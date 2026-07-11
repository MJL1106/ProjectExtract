// ALootContainer implementation.

#include "World/LootContainer.h"
#include "Game/MissionInventorySubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "UObject/Class.h"

DEFINE_LOG_CATEGORY_STATIC(LogLootContainer, Log, All);

ALootContainer::ALootContainer()
{
	PrimaryActorTick.bCanEverTick = false;

	ContainerMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ContainerMesh"));
	SetRootComponent(ContainerMesh);
	// Visible to the ping camera trace (same as ABreachableDoor's panel).
	ContainerMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ContainerMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

bool ALootContainer::CanLoot_Implementation() const
{
	return !bLooted && Contents.Num() > 0;
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
	OnOpened(Looter);
	GrantAllContents();
#if WITH_DEV_AUTOMATION_TESTS
	++CompletionBroadcastCount;
#endif
	OnLootCompleted.Broadcast(this, Looter);

	UE_LOG(LogLootContainer, Log, TEXT("%s: looted by %s (%d grants)"),
		*GetName(), *GetNameSafe(Looter), Contents.Num());
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
