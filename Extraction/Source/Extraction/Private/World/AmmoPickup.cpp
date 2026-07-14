// AAmmoPickup implementation.

#include "World/AmmoPickup.h"
#include "Game/MissionInventorySubsystem.h"
#include "Character/ExtractionPlayer.h"
#include "Components/WeaponComponent.h"
#include "Weapon/WeaponBase.h"
#include "Data/WeaponDataAsset.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

AAmmoPickup::AAmmoPickup()
{
	PrimaryActorTick.bCanEverTick = false;

	OverlapSphere = CreateDefaultSubobject<USphereComponent>(TEXT("OverlapSphere"));
	SetRootComponent(OverlapSphere);
	OverlapSphere->InitSphereRadius(60.f);
	OverlapSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	OverlapSphere->SetCollisionObjectType(ECC_WorldDynamic);
	OverlapSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	OverlapSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(OverlapSphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AAmmoPickup::InitPickup(EEnemyWeaponAnimType InCategory, int32 InAmount)
{
	Category = InCategory;
	Amount = InAmount;
}

void AAmmoPickup::BeginPlay()
{
	Super::BeginPlay();

	OverlapSphere->OnComponentBeginOverlap.AddDynamic(this, &AAmmoPickup::OnSphereBeginOverlap);

	if (Lifespan > 0.f)
		GetWorldTimerManager().SetTimer(LifespanTimerHandle, this, &AAmmoPickup::ExpireLifespan, Lifespan, false);
}

void AAmmoPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(LifespanTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void AAmmoPickup::OnSphereBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority()) return;

	AExtractionPlayer* Player = Cast<AExtractionPlayer>(OtherActor);
	if (!IsValid(Player)) return;

	// Only collect when the drop matches the player's current weapon — otherwise leave it
	// on the ground so a later weapon swap can pick it up (no walk-over toast spam).
	const UWeaponComponent* WeaponComp = Player->FindComponentByClass<UWeaponComponent>();
	const AWeaponBase* Weapon = WeaponComp ? WeaponComp->GetCurrentWeapon() : nullptr;
	const UWeaponDataAsset* Data = Weapon ? Weapon->GetWeaponData() : nullptr;
	if (!Data || Data->EnemyWeaponAnimType != Category) return;

	UMissionInventorySubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!Subsystem) return;

	FLootGrant Grant;
	Grant.Type = ELootType::Ammo;
	Grant.AmmoCategory = Category;
	Grant.AmmoAmount = Amount;
	if (Subsystem->GrantLoot(Grant, Player))
		Destroy();
}

void AAmmoPickup::ExpireLifespan()
{
	Destroy();
}
