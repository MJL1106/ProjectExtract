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
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/GameplayStaticsTypes.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"

// Arc heights tried in order at commit time. ArcParam: 0 = straight up, 1 = flat at the target.
// Medium lob first (the grenadier's existing feel), then flatter throws for low ceilings and
// indoor corridors, then a higher lob for clearing tall cover at short range.
static constexpr float GrenadeArcCandidates[] = { 0.5f, 0.68f, 0.85f, 0.35f };

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

// True when the solved arc actually reaches the landing point. Simulates the whole parabola against
// world static geometry and requires the first blocking hit to be at the intended landing spot.
// The arc solver does no obstruction testing at all, so without this a thrower under a ceiling, in a
// doorway, or behind a cover lip happily "solves" a lob that smacks the roof and drops back on itself.
static bool IsGrenadeArcClear(UWorld* World, AActor* Ignore, const FVector& Start, const FVector& Velocity,
	const FVector& Landing, float MaxLaunchSpeed)
{
	if (!IsValid(World)) return false;

	constexpr float ProbeRadius      = 12.f;  // slightly over the 10 cm projectile sphere
	constexpr float MaxSimSeconds    = 6.f;   // longer than any in-range lob's flight time
	constexpr float LandingTolerance = 200.f; // well inside the blast radius

	// ProjectileMovement clamps Velocity to MaxSpeed every tick, so a launch the projectile can't
	// actually fly would land far short of the solved point. Reject those candidates outright.
	if (MaxLaunchSpeed > 0.f && Velocity.SizeSquared() > FMath::Square(MaxLaunchSpeed)) return false;

	// Object-type query rather than a channel trace: the grenade ignores ECC_Pawn, so pawns standing
	// along the path must not count as blockers.
	FPredictProjectilePathParams Params(
		ProbeRadius,
		Start,
		Velocity,
		MaxSimSeconds,
		UEngineTypes::ConvertToObjectType(ECC_WorldStatic),
		Ignore);
	Params.SimFrequency = 15.f;
	Params.bTraceComplex = false;

	FPredictProjectilePathResult Result;
	if (!UGameplayStatics::PredictProjectilePath(World, Params, Result))
		return false; // never came down on anything inside the sim window

	return FVector::DistSquared(Result.HitResult.Location, Landing) <= FMath::Square(LandingTolerance);
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

	// Validation arc-solve — commits only when the simulated lob actually reaches the landing point
	// from the socket. Each candidate arc is flown against static geometry; the first clear one wins,
	// so a flatter throw is picked automatically under a ceiling and a higher one over tall cover.
	// Deliberately absent from SpawnGrenadeFromSocket() — refusing at release would consume the
	// wind-up montage without spawning or decrementing supply.
	const float MaxLaunchSpeed = GetProjectileMaxSpeed();

	FVector OutVelocity = FVector::ZeroVector;
	float SolvedArc = 0.f;
	bool bSolved = false;
	for (const float ArcParam : GrenadeArcCandidates)
	{
		FVector Candidate;
		if (!UGameplayStatics::SuggestProjectileVelocity_CustomArc(
				this, Candidate, LaunchOrigin, AdjustedTarget, World->GetGravityZ(), ArcParam))
			continue;

		if (!IsGrenadeArcClear(World, Owner, LaunchOrigin, Candidate, AdjustedTarget, MaxLaunchSpeed))
			continue;

		OutVelocity = Candidate;
		SolvedArc = ArcParam;
		bSolved = true;
		break;
	}

	if (!bSolved) return false;

	PendingLaunchVelocity = OutVelocity;
	PendingLandingLocation = AdjustedTarget;
	PendingArcParam = SolvedArc;
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

	// Re-arc-solve from the actual socket position, keeping the arc height chosen at commit time —
	// re-solving at a fixed 0.5 would throw away the flatter arc the ceiling check picked. The
	// thrower can have shifted during the wind-up, so re-validate and re-pick if the arc no longer
	// flies clear. Falls back to the commit-time velocity if nothing solves (the montage and supply
	// are already committed, so refusing here is not an option).
	const float ReleaseMaxSpeed = GetProjectileMaxSpeed();

	FVector LaunchVelocity = PendingLaunchVelocity;
	FVector ResolvVelocity;
	if (UGameplayStatics::SuggestProjectileVelocity_CustomArc(
			this, ResolvVelocity, LaunchOrigin, PendingLandingLocation, World->GetGravityZ(), PendingArcParam)
		&& IsGrenadeArcClear(World, Owner, LaunchOrigin, ResolvVelocity, PendingLandingLocation, ReleaseMaxSpeed))
	{
		LaunchVelocity = ResolvVelocity;
	}
	else
	{
		for (const float ArcParam : GrenadeArcCandidates)
		{
			FVector Candidate;
			if (UGameplayStatics::SuggestProjectileVelocity_CustomArc(
					this, Candidate, LaunchOrigin, PendingLandingLocation, World->GetGravityZ(), ArcParam)
				&& IsGrenadeArcClear(World, Owner, LaunchOrigin, Candidate, PendingLandingLocation, ReleaseMaxSpeed))
			{
				LaunchVelocity = Candidate;
				break;
			}
		}
	}

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

		OnGrenadeThrown.Broadcast();
	}
}

float UEnemyGrenadierComponent::GetProjectileMaxSpeed() const
{
	if (!GrenadeProjectileClass) return 0.f;

	const AEnemyGrenadeProjectile* CDO = GrenadeProjectileClass->GetDefaultObject<AEnemyGrenadeProjectile>();
	if (!IsValid(CDO) || !IsValid(CDO->ProjectileMovement)) return 0.f;

	// 0 means unlimited on the movement component — pass that straight through as "no cap".
	return CDO->ProjectileMovement->MaxSpeed;
}

void UEnemyGrenadierComponent::OnCooldownElapsed()
{
	bCooldownActive = false;
}
