// ALootPickup implementation.

#include "World/LootPickup.h"
#include "Game/MissionInventorySubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Audio/GameAudioSubsystem.h"
#include "Audio/SurfaceAudioBank.h"
#include "UI/LootMarkerComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogLootPickup, Log, All);

ALootPickup::ALootPickup()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PickupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMesh"));
	PickupMesh->SetupAttachment(SceneRoot);
	PickupMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PickupMesh->SetCanEverAffectNavigation(false);

	LootMarker = CreateDefaultSubobject<ULootMarkerComponent>(TEXT("LootMarker"));
	LootMarker->SetupAttachment(SceneRoot);
	LootMarker->MarkerWorldZOffset = 25.f;
}

bool ALootPickup::CanLoot_Implementation() const
{
	return !bLooted && Contents.Num() > 0;
}

void ALootPickup::Loot_Implementation(AActor* Looter)
{
	if (!ILootable::Execute_CanLoot(this)) return;

	bLooted = true;
	GrantAllContents();
	OnLooted(Looter);

	if (UGameAudioSubsystem* AudioSys = GetWorld()->GetSubsystem<UGameAudioSubsystem>())
	{
		if (const USurfaceAudioBank* Bank = AudioSys->GetBank())
			AudioSys->PlayAt(Bank->PickupLoot, GetActorLocation());
	}

	UE_LOG(LogLootPickup, Log, TEXT("%s: looted by %s (%d grants)"),
		*GetName(), *GetNameSafe(Looter), Contents.Num());

	if (IsValid(LootMarker))
	{
		LootMarker->RefreshMarkerNow();
	}

	// Hide and remove collision so the companion BT task (TWeakObjectPtr) sees it as
	// un-lootable rather than walking to an invisible actor. Deferred destroy via
	// SetLifeSpan avoids pulling the rug on any in-flight move-to.
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(2.f);
}

void ALootPickup::SetContents(const TArray<FLootGrant>& InContents)
{
	Contents = InContents;
}

void ALootPickup::GrantAllContents()
{
	UWorld* World = GetWorld();
	UMissionInventorySubsystem* Subsystem = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!Subsystem) return;

	for (const FLootGrant& Grant : Contents)
		Subsystem->GrantLoot(Grant);
}
