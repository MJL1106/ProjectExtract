// AEnemyCharacter — single character class for all 7 enemy archetypes, driven by UEnemyArchetypeData.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "AIShooterInterface.h"
#include "ExtractionTypes.h"
#include "EnemyTypes.h"
#include "EnemyCharacter.generated.h"

class UHealthComponent;
class UEnemyArchetypeData;
class AWeaponBase;
class APatrolRoute;
class AEnemyAIController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTakedownExecuted, AActor*, Instigator);

UCLASS(Blueprintable)
class EXTRACTION_API AEnemyCharacter : public ACharacter,
	public IGameplayTagAssetInterface,
	public IAIShooterInterface,
	public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AEnemyCharacter();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// --- IGameplayTagAssetInterface ---
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	// --- IAIShooterInterface ---
	virtual AActor* GetAIAimTarget() const override;
	virtual float GetAIAimSpreadDegrees() const override;

	// --- IGenericTeamAgentInterface ---
	virtual FGenericTeamId GetGenericTeamId() const override;

	// --- Aim API ---

	/** Sets the actor to aim at. Resets the settle timer on a new target; clears on nullptr. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	void SetAimTarget(AActor* NewTarget);

	// --- Move speed ---

	UFUNCTION(BlueprintCallable, Category = "Enemy|Movement")
	void SetMoveSpeedMode(EEnemyMoveSpeedMode Mode);

	// --- Silent takedown ---

	/** True when alive, Unaware, and the instigator is within range in the rear arc (design §4). */
	UFUNCTION(BlueprintPure, Category = "Enemy|Takedown")
	bool CanBeTakenDown(const AActor* TakedownInstigator) const;

	/** Performs a silent instant kill if CanBeTakenDown passes. Returns whether it executed. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Takedown")
	bool ExecuteTakedown(AActor* TakedownInstigator);

	/** Fired on a successful takedown — animation/FX hook for BP. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Takedown")
	FOnTakedownExecuted OnTakedownExecuted;

	// --- Body discovery ---

	/** First caller gets true and owns reporting this body to the director. */
	bool TryMarkBodyReported();

	// --- Archetype ---

	/** Called by the controller after possession; sets speeds and initialises health from DA. */
	void ApplyArchetypeData();

	UFUNCTION(BlueprintPure, Category = "Enemy|Data")
	const UEnemyArchetypeData* GetArchetypeData() const { return ArchetypeData; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Components")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Weapon")
	AWeaponBase* GetCurrentWeapon() const { return CurrentWeapon.Get(); }

	// --- Config ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Data")
	TObjectPtr<UEnemyArchetypeData> ArchetypeData;

	/** Patrol route assigned in the level for this character. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Enemy|Patrol")
	TObjectPtr<APatrolRoute> PatrolRoute;

	/** Mesh socket to attach the spawned weapon to. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Weapon")
	FName WeaponSocket = TEXT("WeaponSocket");

	/** Maps skeleton bone names to hit regions for damage multiplier lookup. Mannequin defaults. */
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Hitbox")
	TMap<FName, EHitRegion> BoneToHitRegionMap;

private:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components", meta = (AllowPrivateAccess))
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY()
	TObjectPtr<AWeaponBase> CurrentWeapon;

	TWeakObjectPtr<AActor> CurrentAimTarget;
	TWeakObjectPtr<AController> LastDamageInstigator;

	/** World time at which the current aim target was set. Used to compute settle alpha without Tick. */
	float AimStartWorldTime = -1e9f;
	float LastDamageWorldTime = -1e9f;

	UPROPERTY(VisibleInstanceOnly, Category = "Enemy|Tags")
	FGameplayTagContainer OwnedTags;

	/** Set once when an enemy first reports this corpse to the director. */
	bool bBodyReported = false;

	/** Generic damage amount guaranteed to kill through any shield (takedown path). */
	static constexpr float TakedownDamage = 1.e6f;

	FTimerHandle DestroyTimerHandle;

	/** Resolves hitbox multiplier from the damage event's bone + damage type. */
	float GetHitboxDamageMultiplier(const FDamageEvent& DamageEvent) const;

	UFUNCTION()
	void HandleDeath();

	void DestroyAfterDeath();
};
