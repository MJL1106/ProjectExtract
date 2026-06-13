// UEnemyGrenadierComponent

#include "EnemyGrenadierComponent.h"
#include "EnemyArchetypeData.h"
#include "EnemyGrenadeProjectile.h"
#include "BarkSubsystem.h"
#include "BarkSetData.h"
#include "EnemyTypes.h"
#include "EnemyCharacter.h"
#include "HealthComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"

UEnemyGrenadierComponent::UEnemyGrenadierComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEnemyGrenadierComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* World = GetWorld();
	if (IsValid(World))
	{
		World->GetTimerManager().ClearTimer(TelegraphTimerHandle);
		World->GetTimerManager().ClearTimer(CooldownTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UEnemyGrenadierComponent::InitFromArchetype(const UEnemyArchetypeData* Data)
{
	if (!IsValid(Data)) return;

	GrenadeSupply = Data->GrenadeSupply;
	GrenadeCooldown = Data->GrenadeCooldown;
	GrenadeFuseTime = Data->GrenadeFuseTime;
	GrenadeTelegraphTime = Data->GrenadeTelegraphTime;
	GrenadeMinRange = Data->GrenadeMinRange;
	GrenadeMaxRange = Data->GrenadeMaxRange;
	GrenadeDamage = Data->GrenadeDamage;
	GrenadeDamageRadius = Data->GrenadeDamageRadius;
	GrenadeProjectileClass = Data->GrenadeProjectileClass;

	if (Data->bIsGrenadier && !GrenadeProjectileClass)
		UE_LOG(LogTemp, Warning, TEXT("%s: UEnemyGrenadierComponent — bIsGrenadier is true but GrenadeProjectileClass is not set. Grenades will not spawn."), *GetNameSafe(GetOwner()));
}

bool UEnemyGrenadierComponent::CanThrow() const
{
	return GrenadeSupply > 0 && !bCooldownActive && !bTelegraphing && GrenadeProjectileClass != nullptr;
}

bool UEnemyGrenadierComponent::TryThrowAt(const FVector& TargetLocation)
{
	if (!CanThrow()) return false;

	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return false;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return false;

	const FVector LaunchOrigin = Owner->GetActorLocation();
	const float DistSq = FVector::DistSquared2D(LaunchOrigin, TargetLocation);

	if (DistSq < FMath::Square(GrenadeMinRange) || DistSq > FMath::Square(GrenadeMaxRange))
		return false;

	// Arc-solve for the lob trajectory
	FVector OutVelocity;
	const bool bSolved = UGameplayStatics::SuggestProjectileVelocity_CustomArc(
		this,
		OutVelocity,
		LaunchOrigin,
		TargetLocation,
		World->GetGravityZ(),
		0.5f);

	if (!bSolved) return false;

	PendingLaunchVelocity = OutVelocity;
	PendingLandingLocation = TargetLocation;
	bTelegraphing = true;

	// TimeToImpact = telegraph wind-up + estimated arc flight + fuse burn.
	// Flight estimate: distance / horizontal speed component (approximate, good enough for an indicator).
	const float HorizSpeed = FVector(OutVelocity.X, OutVelocity.Y, 0.f).Size();
	const float EstimatedFlight = (HorizSpeed > 1.f) ? FVector::Dist2D(LaunchOrigin, TargetLocation) / HorizSpeed : GrenadeFuseTime;
	const float TimeToImpact = GrenadeTelegraphTime + EstimatedFlight + GrenadeFuseTime;

	OnGrenadeTelegraph.Broadcast(PendingLandingLocation, TimeToImpact);

	// Emit bark
	if (AEnemyCharacter* EnemyChar = Cast<AEnemyCharacter>(Owner))
	{
		if (UBarkSubsystem* BarkSys = World->GetSubsystem<UBarkSubsystem>())
		{
			const UEnemyArchetypeData* DA = EnemyChar->GetArchetypeData();
			if (IsValid(DA) && IsValid(DA->BarkSet))
				BarkSys->RequestBark(Owner, DA->BarkSet, EBarkType::GrenadeOut, DA->DisplayName);
		}
	}

	World->GetTimerManager().SetTimer(
		TelegraphTimerHandle,
		this,
		&UEnemyGrenadierComponent::SpawnGrenade,
		GrenadeTelegraphTime,
		false);

	return true;
}

void UEnemyGrenadierComponent::CancelThrow()
{
	if (!bTelegraphing) return;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(TelegraphTimerHandle);

	bTelegraphing = false;
	OnGrenadeCancelled.Broadcast();
}

void UEnemyGrenadierComponent::SpawnGrenade()
{
	bTelegraphing = false;

	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return;

	// Guard: don't spawn if the owning enemy has already died (corpse persists).
	if (const AEnemyCharacter* EnemyChar = Cast<AEnemyCharacter>(Owner))
	{
		const UHealthComponent* HC = EnemyChar->GetHealthComponent();
		if (IsValid(HC) && HC->IsDead()) return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	if (!GrenadeProjectileClass) return;

	GrenadeSupply--;
	bCooldownActive = true;

	FActorSpawnParameters Params;
	Params.Owner = Owner;
	Params.Instigator = Cast<APawn>(Owner);
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AEnemyGrenadeProjectile* Projectile = World->SpawnActor<AEnemyGrenadeProjectile>(
		GrenadeProjectileClass,
		Owner->GetActorLocation(),
		PendingLaunchVelocity.Rotation(),
		Params);

	if (IsValid(Projectile))
		Projectile->Init(GrenadeDamage, GrenadeDamageRadius, GrenadeFuseTime, PendingLaunchVelocity, PendingLandingLocation);

	World->GetTimerManager().SetTimer(
		CooldownTimerHandle,
		this,
		&UEnemyGrenadierComponent::OnCooldownElapsed,
		GrenadeCooldown,
		false);
}

void UEnemyGrenadierComponent::OnCooldownElapsed()
{
	bCooldownActive = false;
}
