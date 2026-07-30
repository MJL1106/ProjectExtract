// AAmmoPickup implementation.

#include "World/AmmoPickup.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

#include "Audio/GameAudioSubsystem.h"
#include "Audio/SurfaceAudioBank.h"
#include "Components/WeaponComponent.h"
#include "Game/MissionInventorySubsystem.h"
#include "Weapon/WeaponBase.h"

AAmmoPickup::AAmmoPickup()
{
	PrimaryActorTick.bCanEverTick = false;

	OverlapSphere = CreateDefaultSubobject<USphereComponent>(TEXT("OverlapSphere"));
	SetRootComponent(OverlapSphere);
	OverlapSphere->InitSphereRadius(60.f);
	OverlapSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	OverlapSphere->SetCollisionObjectType(ECC_WorldDynamic);
	OverlapSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	// Walk-over collection needs pawn overlaps. Overlap, never Block — a drop that blocked the
	// capsule would shove the player off the ledge it landed on. Radius stays designer-editable.
	OverlapSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	OverlapSphere->SetGenerateOverlapEvents(true);

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

	if (UGameAudioSubsystem* AudioSys = GetWorld()->GetSubsystem<UGameAudioSubsystem>())
	{
		if (const USurfaceAudioBank* Bank = AudioSys->GetBank())
			AudioSys->PlayAt(Bank->PickupAmmo, GetActorLocation());
	}

	Destroy();
	return true;
}

void AAmmoPickup::BeginPlay()
{
	Super::BeginPlay();

	// Seated in a weapon case (or any other holder): the drop rules don't apply. A ground settle
	// would drag it through the foam onto the floor, and the despawn timer would empty the case
	// while the player walked over. Enemy death drops are unattached and keep both.
	if (GetAttachParentActor()) return;

	// Settle onto static ground so the box never hovers. WorldStatic only — a Visibility trace
	// can land the pickup on a corpse or pawn that then moves away.
	FHitResult Hit;
	const FVector Start = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AmmoPickupSettle), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.f, 0.f, 300.f), ECC_WorldStatic, Params))
		SetActorLocation(Hit.Location + FVector(0.f, 0.f, 2.f));

	if (Lifespan > 0.f)
		GetWorldTimerManager().SetTimer(LifespanTimerHandle, this, &AAmmoPickup::ExpireLifespan, Lifespan, false);

	// Last, and below the seated-in-a-holder early return above: the sweep inside can collect and
	// Destroy() this actor outright, so nothing may be scheduled on it afterwards. It also has to
	// run after the settle move so it reads the drop's final resting position.
	if (bAutoCollectOnOverlap) EnableAutoCollect();
}

void AAmmoPickup::EnableAutoCollect()
{
	if (!IsValid(OverlapSphere)) return;

	OverlapSphere->OnComponentBeginOverlap.AddDynamic(this, &AAmmoPickup::OnSphereBeginOverlap);
	OverlapSphere->OnComponentEndOverlap.AddDynamic(this, &AAmmoPickup::OnSphereEndOverlap);

	// A death drop can land under a pawn already standing there. That first overlap resolved during
	// component registration -- before the delegates above existed -- so it never fires, and the box
	// would sit at the player's feet until the despawn timer ate it. Sweep for it once.
	TArray<AActor*> AlreadyOverlapping;
	OverlapSphere->GetOverlappingActors(AlreadyOverlapping, APawn::StaticClass());
	for (AActor* Other : AlreadyOverlapping)
	{
		TryAutoCollect(Cast<APawn>(Other));
		if (IsActorBeingDestroyed()) return;
	}
}

void AAmmoPickup::OnSphereBeginOverlap(
	UPrimitiveComponent* /*OverlappedComp*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	TryAutoCollect(Cast<APawn>(OtherActor));
}

void AAmmoPickup::OnSphereEndOverlap(
	UPrimitiveComponent* /*OverlappedComp*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/)
{
	APawn* OverlappingPawn = Cast<APawn>(OtherActor);
	if (!IsValid(OverlappingPawn)) return;

	// Stepping off clears the refusal, so walking back on after a reload (or after picking up a gun
	// that takes this category) tries again. Only end-overlap clears it: a player who reloads while
	// still standing on a full-reserve box has to step off and back on before it collects.
	RefusedCollectors.Remove(OverlappingPawn);
}

void AAmmoPickup::TryAutoCollect(APawn* OverlappingPawn)
{
	// TryCollect still works on an actor already inside UWorld::DestroyActor, so two begin-overlap
	// broadcasts in the same frame (player and companion stepping on one drop) would grant twice.
	if (IsActorBeingDestroyed()) return;

	if (!bAutoCollectOnOverlap) return;
	if (!HasAuthority() || !IsValid(OverlappingPawn)) return;
	if (RefusedCollectors.Contains(OverlappingPawn)) return;

	// Resolved once per overlap event and threaded into both checks below and the grant itself.
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerPawn)) return;

	if (!IsFriendlyCollector(OverlappingPawn, PlayerPawn)) return;

	// EVERY collector pre-checks compatibility in silence, the player included: GrantAmmo raises a
	// "no compatible weapon" toast on refusal, and because end-overlap clears the refusal, strafing
	// across the sphere would re-toast on every re-entry. The walk-over path is intentionally silent
	// -- that toast belongs to the deliberate interact, which reaches TryCollect directly.
	if (!PlayerHasCompatibleWeapon(PlayerPawn))
	{
		MarkCollectorRefused(OverlappingPawn);
		return;
	}

	// Always the player, never OverlappingPawn: GrantLoot resolves ammo against the recipient's own
	// UWeaponComponent, so handing it the companion would top up the companion's gun instead.
	if (!TryCollect(PlayerPawn)) MarkCollectorRefused(OverlappingPawn);
}

bool AAmmoPickup::IsFriendlyCollector(const APawn* OverlappingPawn, const APawn* PlayerPawn) const
{
	if (!IsValid(OverlappingPawn) || !IsValid(PlayerPawn)) return false;

	// Team ids live on the pawns themselves via IGenericTeamAgentInterface (player, companion and
	// rescued extractee all read friendly to the player; enemies read hostile). Resolved through the
	// attitude solver so no team-id literal is baked in here.
	return FGenericTeamId::GetAttitude(OverlappingPawn, PlayerPawn) == ETeamAttitude::Friendly;
}

bool AAmmoPickup::PlayerHasCompatibleWeapon(const APawn* PlayerPawn) const
{
	if (!IsValid(PlayerPawn)) return false;

	const UWeaponComponent* WeaponComp = PlayerPawn->FindComponentByClass<UWeaponComponent>();
	if (!IsValid(WeaponComp)) return false;

	return IsValid(WeaponComp->FindWeaponByAmmoCategory(Category));
}

void AAmmoPickup::MarkCollectorRefused(APawn* OverlappingPawn)
{
	// The set only ever holds the two or three allies that can stand on one drop, so stale entries
	// are pruned here instead of on a cleanup timer.
	for (auto It = RefusedCollectors.CreateIterator(); It; ++It)
	{
		if (!(*It).IsValid()) It.RemoveCurrent();
	}

	RefusedCollectors.Add(OverlappingPawn);
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
