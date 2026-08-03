// AEnemyGrenadeProjectile

#include "EnemyGrenadeProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Components/SphereComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GenericTeamAgentInterface.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h" // explicit — TArray<FOverlapResult> (house style)
#include "CollisionShape.h"

AEnemyGrenadeProjectile::AEnemyGrenadeProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("SphereCollision"));
	SphereCollision->InitSphereRadius(10.f);
	// Explicit setup — no reliance on a "Projectile" profile that may not exist in this project.
	SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SphereCollision->SetCollisionObjectType(ECC_WorldDynamic);
	SphereCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereCollision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	SphereCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	// Ignore pawns so the grenade doesn't bounce off the thrower at the moment of launch.
	SphereCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	RootComponent = SphereCollision;

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = 0.3f;
	ProjectileMovement->Friction = 0.5f;
	ProjectileMovement->InitialSpeed = 0.f;
	ProjectileMovement->MaxSpeed = 5000.f;
	ProjectileMovement->bRotationFollowsVelocity = true;
}

void AEnemyGrenadeProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(FuseTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void AEnemyGrenadeProjectile::Init(float InDamage, float InDamageRadius, float InFuseTime, const FVector& LaunchVelocity, const FVector& InPredictedLanding)
{
	Damage = InDamage;
	DamageRadius = InDamageRadius;
	PredictedLanding = InPredictedLanding;

	ProjectileMovement->Velocity = LaunchVelocity;

	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().SetTimer(
			FuseTimerHandle,
			this,
			&AEnemyGrenadeProjectile::Detonate,
			InFuseTime,
			false);
	}
}

void AEnemyGrenadeProjectile::Detonate()
{
	if (!HasAuthority()) return;

	OnExplode();

	// Team-filtered blast: ignore every pawn that is not hostile to the instigator.
	// This grenade is shared by enemy grenadiers and the companion, so the filter is
	// instigator-relative (enemies = team 1, player + companion = team 0). Neutrals
	// (e.g. a captive extractee with NoTeam) are also spared.
	// If the instigator is null or destroyed (thrower died mid-flight), fall back to
	// unfiltered damage — no filter is safer than accidentally shielding the player.
	TArray<AActor*> IgnoreActors;
	APawn* InstigatorPawn = GetInstigator();
	if (IsValid(InstigatorPawn))
	{
		UWorld* World = GetWorld();
		if (IsValid(World))
		{
			TArray<FOverlapResult> Overlaps;
			FCollisionObjectQueryParams ObjParams(FCollisionObjectQueryParams::AllDynamicObjects);
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GrenadeTeamFilter), false);
			QueryParams.AddIgnoredActor(this);

			if (World->OverlapMultiByObjectType(
					Overlaps,
					GetActorLocation(),
					FQuat::Identity,
					ObjParams,
					FCollisionShape::MakeSphere(DamageRadius),
					QueryParams))
			{
				IgnoreActors.Reserve(Overlaps.Num());
				for (const FOverlapResult& Overlap : Overlaps)
				{
					APawn* CandidatePawn = Cast<APawn>(Overlap.GetActor());
					if (IsValid(CandidatePawn)
						&& FGenericTeamId::GetAttitude(CandidatePawn, InstigatorPawn) != ETeamAttitude::Hostile)
					{
						IgnoreActors.AddUnique(CandidatePawn);
					}
				}
			}
		}
	}

	// Falloff: full damage at centre, inner radius at 25% of DamageRadius, 10% minimum at the edge.
	UGameplayStatics::ApplyRadialDamageWithFalloff(
		this,
		Damage,
		Damage * 0.1f,
		GetActorLocation(),
		DamageRadius * 0.25f,
		DamageRadius,
		1.f,
		UDamageType::StaticClass(),
		IgnoreActors,
		this,
		GetInstigatorController());

	Destroy();
}
