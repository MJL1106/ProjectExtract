// UEnemyArmourComponent — directional damage reduction for the Heavy archetype.
// Bolt-on: created at runtime by AEnemyCharacter::ApplyArchetypeData when bHasArmour is set.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyArmourComponent.generated.h"

class UEnemyArchetypeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnArmourPlateBroken, int32, PlatesRemaining);

UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UEnemyArmourComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyArmourComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called by AEnemyCharacter::ApplyArchetypeData immediately after RegisterComponent. */
	void InitFromArchetype(const UEnemyArchetypeData* Data);

	/**
	 * Applies directional armour reduction to incoming damage.
	 * Head region hits bypass frontal reduction (full + hitbox bonus).
	 * Returns the modified damage value.
	 */
	float ModifyIncomingDamage(float Damage, const FDamageEvent& DamageEvent, AActor* DamageCauser);

	/** Fired when a plate breaks. PlatesRemaining is 0 when all plates are gone. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Armour")
	FOnArmourPlateBroken OnPlateBroken;

	UFUNCTION(BlueprintPure, Category = "Enemy|Armour")
	int32 GetPlatesRemaining() const { return PlatesRemaining; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Armour")
	bool AllPlatesBroken() const { return PlatesRemaining <= 0; }

private:
	/** Cached from DA at InitFromArchetype time. */
	float FrontalArcHalfCosine = 0.f;
	float FrontalMultiplier = 0.3f;
	float RearMultiplier = 1.5f;
	float PlateBreakThreshold = 0.f;

	int32 TotalPlates = 3;
	int32 PlatesRemaining = 3;

	/** Cumulative frontal-arc damage absorbed; resets per-plate. */
	float FrontalDamageAccumulated = 0.f;

	/** Cached owner max health for plate-break threshold calculation. */
	float OwnerMaxHealth = 100.f;

	bool bInitialized = false;

	/**
	 * Resolves whether the hit came from the frontal arc.
	 * bOutResolved is false when direction can't be determined (no causer, invalid owner).
	 * Callers must check bOutResolved before acting on the return value.
	 */
	bool IsFrontalHit(const FDamageEvent& DamageEvent, AActor* DamageCauser, bool& bOutResolved) const;
};
