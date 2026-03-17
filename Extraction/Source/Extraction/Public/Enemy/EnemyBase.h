// Base enemy character — placeholder target for companion combat testing.
// Used by: AI targeting, companion combat, damage system.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "EnemyBase.generated.h"

class UHealthComponent;
class AWeaponBase;

DECLARE_LOG_CATEGORY_EXTERN(LogEnemy, Log, All);

UCLASS(Blueprintable)
class EXTRACTION_API AEnemyBase : public ACharacter, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	AEnemyBase();

	// --- IGameplayTagAssetInterface ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --- Components ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
	TObjectPtr<UStaticMeshComponent> EnemyMesh;

	// --- Config ---

	/** Seconds after death before the actor is destroyed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Config", meta = (ClampMin = "0.0"))
	float DestroyDelay = 3.0f;

	// --- Patrol ---

	/** Half-distance of the patrol path from spawn origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Patrol", meta = (ClampMin = "0.0"))
	float PatrolDistance = 300.0f;

	/** Movement speed in cm/s along the patrol path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Patrol", meta = (ClampMin = "0.0"))
	float PatrolSpeed = 200.0f;

	// --- Combat (optional) ---

	/** Weapon class to spawn. Leave None to disable firing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Combat")
	TSubclassOf<AWeaponBase> WeaponClass;

	/** Seconds between fire attempts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Combat", meta = (ClampMin = "0.1"))
	float FireInterval = 1.5f;

	/** Random aim offset in degrees per shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float AimInaccuracyDegrees = 5.0f;

	/** Max distance to engage a target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Combat", meta = (ClampMin = "0.0"))
	float MaxEngageRange = 2000.0f;

	virtual void Tick(float DeltaTime) override;

private:
	UFUNCTION()
	void HandleDeath();

	void DestroyAfterDeath();
	void TryFire();
	AActor* FindClosestTarget() const;

	UPROPERTY(VisibleInstanceOnly, Category = "Enemy|Tags")
	FGameplayTagContainer OwnedTags;

	UPROPERTY()
	TObjectPtr<AWeaponBase> CurrentWeapon;

	FVector SpawnLocation;

	/** Ping-pong alpha: 0 = left extent, 1 = right extent. */
	float PatrolAlpha = 0.0f;
	int8 PatrolDirection = 1;

	FTimerHandle DestroyTimerHandle;
	FTimerHandle FireTimerHandle;
};
