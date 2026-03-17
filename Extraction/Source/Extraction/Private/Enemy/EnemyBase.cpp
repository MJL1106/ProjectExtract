// Base enemy character — placeholder target for companion combat testing.

#include "EnemyBase.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "ExtractionTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagAssetInterface.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY(LogEnemy);

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	NetUpdateFrequency = 10.0f;
	MinNetUpdateFrequency = 5.0f;

	// Capsule
	GetCapsuleComponent()->SetCapsuleSize(34.0f, 88.0f);

	// Health
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

	// Visible placeholder mesh — bright red cylinder so it's easy to spot
	EnemyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EnemyMesh"));
	EnemyMesh->SetupAttachment(GetCapsuleComponent());
	EnemyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -88.0f));
	EnemyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		EnemyMesh->SetStaticMesh(CylinderMesh.Object);
		EnemyMesh->SetRelativeScale3D(FVector(0.68f, 0.68f, 1.76f));
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (RedMaterial.Succeeded())
	{
		EnemyMesh->SetMaterial(0, RedMaterial.Object);
	}

	// Gameplay tags
	OwnedTags.AddTag(TAG_Character_Enemy);
}

void AEnemyBase::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.AppendTags(OwnedTags);
}

void AEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	if (HealthComponent && !HealthComponent->OnDeath.IsAlreadyBound(this, &AEnemyBase::HandleDeath))
		HealthComponent->OnDeath.AddDynamic(this, &AEnemyBase::HandleDeath);

	// Create a red dynamic material at runtime for visibility
	if (EnemyMesh)
	{
		UMaterialInstanceDynamic* DynMat = EnemyMesh->CreateDynamicMaterialInstance(0);
		if (DynMat)
		{
			DynMat->SetVectorParameterValue(FName("Color"), FLinearColor(1.0f, 0.05f, 0.05f, 1.0f));
		}
	}

	SpawnLocation = GetActorLocation();

	// Spawn and attach weapon if configured (server only — weapon replicates to clients)
	if (HasAuthority() && WeaponClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;

		CurrentWeapon = GetWorld()->SpawnActor<AWeaponBase>(WeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (IsValid(CurrentWeapon))
		{
			CurrentWeapon->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			CurrentWeapon->InitializeAmmo();

			GetWorldTimerManager().SetTimer(FireTimerHandle, this, &AEnemyBase::TryFire, FireInterval, true, FireInterval);

			UE_LOG(LogEnemy, Log, TEXT("%s equipped weapon %s"), *GetName(), *CurrentWeapon->GetName());
		}
	}

	UE_LOG(LogEnemy, Log, TEXT("%s spawned with tag Character.Enemy"), *GetName());
}

void AEnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DestroyTimerHandle);
		World->GetTimerManager().ClearTimer(FireTimerHandle);
	}

	if (HealthComponent)
		HealthComponent->OnDeath.RemoveDynamic(this, &AEnemyBase::HandleDeath);

	Super::EndPlay(EndPlayReason);
}

void AEnemyBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (PatrolDistance <= 0.0f || PatrolSpeed <= 0.0f) return;

	// Advance alpha based on speed and patrol distance
	const float AlphaStep = (PatrolSpeed * DeltaTime) / (PatrolDistance * 2.0f);
	PatrolAlpha += AlphaStep * PatrolDirection;

	if (PatrolAlpha >= 1.0f)
	{
		PatrolAlpha = 1.0f;
		PatrolDirection = -1;
	}
	else if (PatrolAlpha <= 0.0f)
	{
		PatrolAlpha = 0.0f;
		PatrolDirection = 1;
	}

	const FVector LeftPoint = SpawnLocation - FVector(PatrolDistance, 0.0f, 0.0f);
	const FVector RightPoint = SpawnLocation + FVector(PatrolDistance, 0.0f, 0.0f);
	const FVector NewLocation = FMath::Lerp(LeftPoint, RightPoint, PatrolAlpha);

	SetActorLocation(FVector(NewLocation.X, NewLocation.Y, GetActorLocation().Z));
}

void AEnemyBase::HandleDeath()
{
	UE_LOG(LogEnemy, Log, TEXT("%s died"), *GetName());

	// Stop patrol
	SetActorTickEnabled(false);

	// Stop firing
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(FireTimerHandle);

	if (IsValid(CurrentWeapon))
		CurrentWeapon->StopFiring();

	// Disable collision so projectiles pass through
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Stop all movement
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		Movement->StopMovementImmediately();

	// Destroy after delay
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().SetTimer(DestroyTimerHandle, this, &AEnemyBase::DestroyAfterDeath, DestroyDelay, false);
}

void AEnemyBase::DestroyAfterDeath()
{
	Destroy();
}

void AEnemyBase::TryFire()
{
	if (!IsValid(CurrentWeapon) || !CurrentWeapon->CanFire()) return;

	AActor* Target = FindClosestTarget();
	if (!Target) return;

	const FVector MyLocation = GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();
	const float DistSq = FVector::DistSquared(MyLocation, TargetLocation);

	if (DistSq > FMath::Square(MaxEngageRange)) return;

	// Line of sight check
	const UWorld* World = GetWorld();
	if (!World) return;

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(CurrentWeapon);

	const bool bHit = World->LineTraceSingleByChannel(Hit, MyLocation, TargetLocation, ECC_Visibility, QueryParams);
	if (bHit && Hit.GetActor() != Target) return;

	// Aim at target with random inaccuracy
	FRotator AimRotation = (TargetLocation - MyLocation).Rotation();
	AimRotation.Yaw += FMath::RandRange(-AimInaccuracyDegrees, AimInaccuracyDegrees);
	AimRotation.Pitch += FMath::RandRange(-AimInaccuracyDegrees, AimInaccuracyDegrees);
	SetActorRotation(FRotator(0.0f, AimRotation.Yaw, 0.0f));

	// Fire a single shot
	CurrentWeapon->StartFiring();
	CurrentWeapon->StopFiring();
}

AActor* AEnemyBase::FindClosestTarget() const
{
	TArray<AActor*> Characters;
	UGameplayStatics::GetAllActorsOfClass(this, ACharacter::StaticClass(), Characters);

	AActor* ClosestTarget = nullptr;
	float ClosestDistSq = MAX_FLT;
	const FVector MyLocation = GetActorLocation();

	for (AActor* Actor : Characters)
	{
		if (Actor == this) continue;

		// Skip other enemies
		if (const IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(Actor))
		{
			FGameplayTagContainer ActorTags;
			TagInterface->GetOwnedGameplayTags(ActorTags);
			if (ActorTags.HasTag(TAG_Character_Enemy)) continue;
		}

		const float DistSq = FVector::DistSquared(MyLocation, Actor->GetActorLocation());
		if (DistSq < ClosestDistSq)
		{
			ClosestDistSq = DistSq;
			ClosestTarget = Actor;
		}
	}

	return ClosestTarget;
}
