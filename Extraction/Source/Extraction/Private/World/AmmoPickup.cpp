// AAmmoPickup implementation.

#include "World/AmmoPickup.h"
#include "Game/MissionInventorySubsystem.h"
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

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(OverlapSphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AAmmoPickup::InitPickup(EEnemyWeaponAnimType InCategory, int32 InAmount)
{
	Category = InCategory;
	Amount = InAmount;
}

bool AAmmoPickup::TryCollect(APawn* Collector)
{
	if (!HasAuthority() || !IsValid(Collector)) return false;

	UMissionInventorySubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!Subsystem) return false;

	FLootGrant Grant;
	Grant.Type = ELootType::Ammo;
	Grant.AmmoCategory = Category;
	Grant.AmmoAmount = Amount;
	if (!Subsystem->GrantLoot(Grant, Collector)) return false;

	Destroy();
	return true;
}

void AAmmoPickup::BeginPlay()
{
	Super::BeginPlay();

	// Settle onto static ground so the box never hovers. WorldStatic only — a Visibility trace
	// can land the pickup on a corpse or pawn that then moves away.
	FHitResult Hit;
	const FVector Start = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AmmoPickupSettle), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.f, 0.f, 300.f), ECC_WorldStatic, Params))
		SetActorLocation(Hit.Location + FVector(0.f, 0.f, 2.f));

	if (Lifespan > 0.f)
		GetWorldTimerManager().SetTimer(LifespanTimerHandle, this, &AAmmoPickup::ExpireLifespan, Lifespan, false);
}

void AAmmoPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(LifespanTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void AAmmoPickup::ExpireLifespan()
{
	Destroy();
}
