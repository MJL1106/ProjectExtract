// UEnemyShieldComponent — breakable frontal static-mesh shield for the Shield archetype.
// The component IS the mesh: collision blocks Visibility so weapon traces hit it directly.
// Bolt-on: created at runtime by AEnemyCharacter::ApplyArchetypeData when bHasShield is set.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StreamableManager.h"
#include "EnemyShieldComponent.generated.h"

class UEnemyArchetypeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnShieldBroken);

UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UEnemyShieldComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UEnemyShieldComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called by AEnemyCharacter::ApplyArchetypeData immediately after RegisterComponent. */
	void InitFromArchetype(const UEnemyArchetypeData* Data);

	/**
	 * Routes incoming damage through the shield.
	 *   FPointDamageEvent where HitInfo.Component == this  → fully absorbed into ShieldHP, returns 0.
	 *   FRadialDamageEvent (grenade)                       → damages shield heavily AND passes through.
	 *   Other/generic events (takedown)                    → pass through untouched.
	 * Returns damage remaining to be applied to the character.
	 */
	float ProcessIncomingDamage(float Damage, const FDamageEvent& DamageEvent, AActor* DamageCauser);

	UFUNCTION(BlueprintPure, Category = "Enemy|Shield")
	bool IsShieldBroken() const { return bShieldBroken; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Shield")
	float GetShieldHealthPercent() const { return ShieldMaxHealth > 0.f ? ShieldCurrentHealth / ShieldMaxHealth : 0.f; }

	/** Fired once when the shield HP reaches 0. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Shield")
	FOnShieldBroken OnShieldBroken;

private:
	float ShieldMaxHealth = 400.f;
	float ShieldCurrentHealth = 400.f;

	/** Multiplier applied to radial damage before hitting shield HP (grenades punch harder). */
	static constexpr float RadialShieldDamageMultiplier = 2.f;

	bool bShieldBroken = false;
	bool bInitialized = false;

	/** Keeps the async mesh load request alive until the mesh is set or the component is destroyed. */
	TSharedPtr<FStreamableHandle> MeshLoadHandle;

	/** Applies damage to ShieldCurrentHealth, triggers break if <= 0. */
	void ApplyDamageToShield(float Damage);

	/** Hides mesh, disables collision, broadcasts OnShieldBroken. No re-grow. */
	void BreakShield();

	/** Called when the async mesh load completes. */
	void OnMeshLoaded(TSoftObjectPtr<UStaticMesh> SoftMesh);
};
