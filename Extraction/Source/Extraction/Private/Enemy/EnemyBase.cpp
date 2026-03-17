// Base enemy character — placeholder target for companion combat testing.

#include "EnemyBase.h"
#include "HealthComponent.h"
#include "ExtractionTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY(LogEnemy);

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

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

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AEnemyBase::HandleDeath);
	}

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

	UE_LOG(LogEnemy, Log, TEXT("%s spawned with tag Character.Enemy"), *GetName());
}

void AEnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(DestroyTimerHandle);

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
