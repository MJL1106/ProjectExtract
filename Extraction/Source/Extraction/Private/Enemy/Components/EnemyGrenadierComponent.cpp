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
#include "Components/SkeletalMeshComponent.h"
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
	GrenadeThrowSocket = Data->GrenadeThrowSocket;
	GrenadeLandingDistanceScale = Data->GrenadeLandingDistanceScale;

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

	// Resolve launch origin from the hand socket so the range check matches the actual spawn point.
	FVector LaunchOrigin = Owner->GetActorLocation();
	if (const ACharacter* EnemyChar = Cast<ACharacter>(Owner))
	{
		USkeletalMeshComponent* Mesh = EnemyChar->GetMesh();
		if (IsValid(Mesh) && Mesh->DoesSocketExist(GrenadeThrowSocket))
			LaunchOrigin = Mesh->GetSocketLocation(GrenadeThrowSocket);
	}

	// Pull landing toward thrower by the DA scale (1.0 = exact target, <1 = shorter throw). Z stays on the ground.
	FVector AdjustedTarget = TargetLocation;
	if (GrenadeLandingDistanceScale < 1.f)
	{
		AdjustedTarget.X = LaunchOrigin.X + (TargetLocation.X - LaunchOrigin.X) * GrenadeLandingDistanceScale;
		AdjustedTarget.Y = LaunchOrigin.Y + (TargetLocation.Y - LaunchOrigin.Y) * GrenadeLandingDistanceScale;
	}

	const float DistSq = FVector::DistSquared2D(LaunchOrigin, AdjustedTarget);
	if (DistSq < FMath::Square(GrenadeMinRange) || DistSq > FMath::Square(GrenadeMaxRange))
		return false;

	// Validation arc-solve — commits only when a valid lob is possible from the socket.
	FVector OutVelocity;
	const bool bSolved = UGameplayStatics::SuggestProjectileVelocity_CustomArc(
		this,
		OutVelocity,
		LaunchOrigin,
		AdjustedTarget,
		World->GetGravityZ(),
		0.5f);

	if (!bSolved) return false;

	PendingLaunchVelocity = OutVelocity;
	PendingLandingLocation = AdjustedTarget;
	bTelegraphing = true;

	const float HorizSpeed = FVector(OutVelocity.X, OutVelocity.Y, 0.f).Size();
	const float EstimatedFlight = (HorizSpeed > 1.f) ? FVector::Dist2D(LaunchOrigin, AdjustedTarget) / HorizSpeed : GrenadeFuseTime;
	const float TimeToImpact = GrenadeTelegraphTime + EstimatedFlight + GrenadeFuseTime;

	OnGrenadeTelegraph.Broadcast(PendingLandingLocation, TimeToImpact);

	if (AEnemyCharacter* EnemyChar = Cast<AEnemyCharacter>(Owner))
	{
		if (UBarkSubsystem* BarkSys = World->GetSubsystem<UBarkSubsystem>())
		{
			const UEnemyArchetypeData* DA = EnemyChar->GetArchetypeData();
			if (IsValid(DA) && IsValid(DA->BarkSet))
				BarkSys->RequestBark(Owner, DA->BarkSet, EBarkType::GrenadeOut, DA->DisplayName);
		}
	}

	// Fallback timer — fires SpawnGrenadeFromSocket only if ReleaseGrenade() hasn't cleared bTelegraphing first.
	World->GetTimerManager().SetTimer(
		TelegraphTimerHandle,
		this,
		&UEnemyGrenadierComponent::SpawnGrenadeFromSocket,
		GrenadeTelegraphTime,
		false);

	return true;
}

void UEnemyGrenadierComponent::ReleaseGrenade()
{
	if (!bTelegraphing) return;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(TelegraphTimerHandle);

	SpawnGrenadeFromSocket();
}

void UEnemyGrenadierComponent::CancelThrow()
{
	if (!bTelegraphing) return;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(TelegraphTimerHandle);

	bTelegraphing = false;
	OnGrenadeCancelled.Broadcast();
}

void UEnemyGrenadierComponent::SpawnGrenadeFromSocket()
{
	// Guard: bTelegraphing=false means either CancelThrow was called or this already ran.
	if (!bTelegraphing) return;
	bTelegraphing = false;

	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return;

	// Don't spawn if the enemy died mid wind-up.
	if (const AEnemyCharacter* EnemyChar = Cast<AEnemyCharacter>(Owner))
	{
		const UHealthComponent* HC = EnemyChar->GetHealthComponent();
		if (IsValid(HC) && HC->IsDead()) return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	if (!GrenadeProjectileClass) return;

	// Resolve launch origin from the socket at release time for accurate throw.
	FVector LaunchOrigin = Owner->GetActorLocation();
	if (const ACharacter* EnemyChar = Cast<ACharacter>(Owner))
	{
		USkeletalMeshComponent* Mesh = EnemyChar->GetMesh();
		if (IsValid(Mesh) && Mesh->DoesSocketExist(GrenadeThrowSocket))
			LaunchOrigin = Mesh->GetSocketLocation(GrenadeThrowSocket);
	}

	// Re-arc-solve from the actual socket position; fall back to the commit-time velocity if unsolvable.
	FVector LaunchVelocity = PendingLaunchVelocity;
	FVector ResolvVelocity;
	const bool bResolved = UGameplayStatics::SuggestProjectileVelocity_CustomArc(
		this,
		ResolvVelocity,
		LaunchOrigin,
		PendingLandingLocation,
		World->GetGravityZ(),
		0.5f);

	if (bResolved) LaunchVelocity = ResolvVelocity;

	FActorSpawnParameters Params;
	Params.Owner = Owner;
	Params.Instigator = Cast<APawn>(Owner);
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AEnemyGrenadeProjectile* Projectile = World->SpawnActor<AEnemyGrenadeProjectile>(
		GrenadeProjectileClass,
		LaunchOrigin,
		LaunchVelocity.Rotation(),
		Params);

	// Only consume supply and start the cooldown when the projectile actually spawned.
	if (IsValid(Projectile))
	{
		Projectile->Init(GrenadeDamage, GrenadeDamageRadius, GrenadeFuseTime, LaunchVelocity, PendingLandingLocation);
		GrenadeSupply--;
		bCooldownActive = true;
		World->GetTimerManager().SetTimer(
			CooldownTimerHandle,
			this,
			&UEnemyGrenadierComponent::OnCooldownElapsed,
			GrenadeCooldown,
			false);
	}
}

void UEnemyGrenadierComponent::OnCooldownElapsed()
{
	bCooldownActive = false;
}
