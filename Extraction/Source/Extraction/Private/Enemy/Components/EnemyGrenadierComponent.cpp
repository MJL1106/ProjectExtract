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
#include "Engine/HitResult.h"
#include "CollisionShape.h"

// Projects a world-space point down to the nearest WorldStatic surface below it.
// Returns Point with its Z replaced by the hit Z. Falls back to subtracting a nominal capsule half-height on miss.
static FVector ProjectGrenadeTargetToGround(UWorld* World, const FVector& Point, const AActor* IgnoreActor)
{
	if (!World) return Point;

	constexpr float StartOffsetUp  = 100.f;   // lift above Point to avoid starting inside geometry
	constexpr float TraceDepthDown = 400.f;   // covers steps/slopes; won't punch to a separate lower storey
	constexpr float FallbackDrop   = 96.f;    // player standing capsule half-height (ExtractionCharacter)

	const FVector Start = Point + FVector(0.f, 0.f, StartOffsetUp);
	const FVector End   = Start  - FVector(0.f, 0.f, TraceDepthDown);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrenadeGroundProject), false);
	Params.AddIgnoredActor(IgnoreActor);

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		return FVector(Point.X, Point.Y, Hit.Location.Z);

	return FVector(Point.X, Point.Y, Point.Z - FallbackDrop);
}

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

void UEnemyGrenadierComponent::InitFromParams(const FGrenadierInitParams& Params)
{
	GrenadeSupply = Params.GrenadeSupply;
	GrenadeCooldown = Params.GrenadeCooldown;
	GrenadeFuseTime = Params.GrenadeFuseTime;
	GrenadeTelegraphTime = Params.GrenadeTelegraphTime;
	GrenadeMinRange = Params.GrenadeMinRange;
	GrenadeMaxRange = Params.GrenadeMaxRange;
	GrenadeDamage = Params.GrenadeDamage;
	GrenadeDamageRadius = Params.GrenadeDamageRadius;
	GrenadeProjectileClass = Params.GrenadeProjectileClass;
	GrenadeThrowSocket = Params.GrenadeThrowSocket;
	GrenadeLandingDistanceScale = Params.GrenadeLandingDistanceScale;

	if (!GrenadeProjectileClass)
		UE_LOG(LogTemp, Warning, TEXT("%s: UEnemyGrenadierComponent — InitFromParams without a GrenadeProjectileClass. Grenades will not spawn."), *GetNameSafe(GetOwner()));
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

	const float PreGroundZ = AdjustedTarget.Z;
	AdjustedTarget = ProjectGrenadeTargetToGround(World, AdjustedTarget, Owner);
	UE_LOG(LogTemp, Verbose, TEXT("%s: grenade aim Z %.0f -> grounded %.0f"), *GetNameSafe(Owner), PreGroundZ, AdjustedTarget.Z);

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

	// --- Launch clearance: sweep a sphere along the initial trajectory to catch wall collisions ---
	// The arc solver does no obstruction testing, so a grenadier behind tall cover (or in the
	// open after the lob gate was widened) can solve an arc that plants the grenade straight
	// into the wall it is hiding behind. This catches that case at commit time.
	// Deliberately absent from SpawnGrenadeFromSocket() — refusing at release would consume the
	// wind-up montage without spawning or decrementing supply.
	constexpr float ClearanceProbeRadius = 15.f;   // slightly larger than the 10 cm projectile sphere
	constexpr float ClearanceProbeDistance = 150.f; // ~1.5 m along the solved velocity — clears the thrower's cover lip
	{
		const FVector SweepDir = OutVelocity.GetSafeNormal();
		const FVector SweepEnd = LaunchOrigin + SweepDir * ClearanceProbeDistance;

		FCollisionQueryParams ClearanceParams(SCENE_QUERY_STAT(GrenadeLaunchClearance), false);
		ClearanceParams.AddIgnoredActor(Owner);

		FHitResult ClearanceHit;
		if (World->SweepSingleByChannel(
				ClearanceHit,
				LaunchOrigin,
				SweepEnd,
				FQuat::Identity,
				ECC_WorldStatic,
				FCollisionShape::MakeSphere(ClearanceProbeRadius),
				ClearanceParams)
			&& ClearanceHit.bBlockingHit && !ClearanceHit.bStartPenetrating)
		{
			return false;
		}
	}

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
				BarkSys->RequestBark(Owner, DA->BarkSet, EBarkType::GrenadeOut);
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

	// Don't spawn if the thrower died mid wind-up (owner-agnostic: enemy or companion).
	if (const UHealthComponent* HC = Owner->FindComponentByClass<UHealthComponent>())
	{
		if (HC->IsDead()) return;
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
