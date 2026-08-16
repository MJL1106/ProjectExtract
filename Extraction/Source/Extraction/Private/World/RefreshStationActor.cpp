// ARefreshStationActor implementation.

#include "World/RefreshStationActor.h"

#include "Character/ExtractionPlayer.h"
#include "Components/ConsumableInventoryComponent.h"
#include "Components/HealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WeaponComponent.h"
#include "Engine/World.h"
#include "MissionInventorySubsystem.h"
#include "Weapon/WeaponBase.h"

ARefreshStationActor::ARefreshStationActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	StationMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StationMesh"));
	StationMesh->SetupAttachment(SceneRoot);
	StationMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	StationMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	StationMesh->SetCanEverAffectNavigation(false);
}

bool ARefreshStationActor::CanWorldInteract_Implementation(AActor* Interactor) const
{
	if (!Cast<AExtractionPlayer>(Interactor)) return false;

	const UWorld* World = GetWorld();
	return World && (World->GetTimeSeconds() - LastUseWorldTime) >= ReuseCooldown;
}

void ARefreshStationActor::WorldInteract_Implementation(AActor* Interactor)
{
	AExtractionPlayer* Player = Cast<AExtractionPlayer>(Interactor);
	if (!IsValid(Player)) return;

	UWorld* World = GetWorld();
	if (World) LastUseWorldTime = World->GetTimeSeconds();

	// Current maxes back in as the new maxes — InitializeHealth is the one call that tops up
	// health AND shield in one stroke without waiting on shield regen.
	if (UHealthComponent* Health = Player->FindComponentByClass<UHealthComponent>())
		Health->InitializeHealth(Health->GetMaxHealth(), Health->GetMaxShield());

	// Spawn-default ammo on every carried weapon, not just the held one. InitializeAmmo already
	// broadcasts to the HUD and syncs the kit visual item, so no extra resync is needed.
	if (UWeaponComponent* Weapons = Player->FindComponentByClass<UWeaponComponent>())
	{
		if (AWeaponBase* Primary = Weapons->GetPrimaryWeapon(); IsValid(Primary))
			Primary->InitializeAmmo();
		if (AWeaponBase* Secondary = Weapons->GetSecondaryWeapon(); IsValid(Secondary))
			Secondary->InitializeAmmo();

		// InitializeAmmo syncs the kit visual item UNCONDITIONALLY (unlike AddReserveAmmo's
		// IsHidden guard), and the owner has ONE SpawnedItem — whichever refill ran last stamped
		// its counts over the held gun's HUD. Re-assert the held weapon's numbers.
		if (AWeaponBase* Held = Weapons->GetCurrentWeapon(); IsValid(Held))
			Held->ResyncVisualAmmo();
	}

	if (UConsumableInventoryComponent* Consumables = Player->FindComponentByClass<UConsumableInventoryComponent>())
		Consumables->SetStimCount(Consumables->GetMaxStims());

	if (UMissionInventorySubsystem* Inventory = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr)
		Inventory->OnLootNotify.Broadcast(NSLOCTEXT("RefreshStation", "Refreshed", "Loadout refreshed"));

	OnRefreshed(Player);
}

FText ARefreshStationActor::GetWorldInteractionPrompt_Implementation(AActor* /*Interactor*/) const
{
	return InteractionPrompt;
}
