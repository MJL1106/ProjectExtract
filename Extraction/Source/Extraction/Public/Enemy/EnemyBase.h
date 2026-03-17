// Base enemy character — placeholder target for companion combat testing.
// Used by: AI targeting, companion combat, damage system.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "EnemyBase.generated.h"

class UHealthComponent;

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

private:
	UFUNCTION()
	void HandleDeath();

	void DestroyAfterDeath();

	UPROPERTY(VisibleInstanceOnly, Category = "Enemy|Tags")
	FGameplayTagContainer OwnedTags;

	FTimerHandle DestroyTimerHandle;
};
