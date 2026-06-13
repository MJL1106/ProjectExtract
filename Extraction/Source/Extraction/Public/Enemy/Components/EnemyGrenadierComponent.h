// UEnemyGrenadierComponent — manages grenade supply, telegraph, and lob for the Grenadier archetype.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyGrenadierComponent.generated.h"

class UEnemyArchetypeData;
class AEnemyGrenadeProjectile;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGrenadeTelegraph, FVector, PredictedLanding, float, TimeToImpact);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGrenadeCancelled);

UCLASS(ClassGroup = "Enemy", meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UEnemyGrenadierComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyGrenadierComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Initialise supply/timing/damage from the archetype data asset. */
	void InitFromArchetype(const UEnemyArchetypeData* Data);

	/** True when supply > 0, cooldown has elapsed, and no throw is currently telegraphing. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Grenadier")
	bool CanThrow() const;

	/**
	 * Range-checks TargetLocation (GrenadeMinRange/MaxRange), arc-solves a lob trajectory.
	 * On success: starts the telegraph timer, broadcasts OnGrenadeTelegraph, emits bark, then
	 * spawns the projectile with the solved velocity. Returns false if out-of-range or unsolvable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Grenadier")
	bool TryThrowAt(const FVector& TargetLocation);

	/**
	 * Aborts an in-progress telegraph (clears timer, resets state). Does NOT decrement supply.
	 * Called by HandleDeath and BT task AbortTask.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Grenadier")
	void CancelThrow();

	/** True while the telegraph timer is running (before the projectile spawns). */
	UFUNCTION(BlueprintPure, Category = "Enemy|Grenadier")
	bool IsTelegraphing() const { return bTelegraphing; }

	/** Broadcast at the start of a throw — BPs use this to show the grenade indicator. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Grenadier")
	FOnGrenadeTelegraph OnGrenadeTelegraph;

	/** Broadcast when a throw is cancelled mid-telegraph — BPs use this to hide the grenade indicator. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Grenadier")
	FOnGrenadeCancelled OnGrenadeCancelled;

private:
	/** Cached from DA at InitFromArchetype. */
	int32 GrenadeSupply = 3;
	float GrenadeCooldown = 12.f;
	float GrenadeFuseTime = 2.5f;
	float GrenadeTelegraphTime = 1.f;
	float GrenadeMinRange = 500.f;
	float GrenadeMaxRange = 2000.f;
	float GrenadeDamage = 80.f;
	float GrenadeDamageRadius = 350.f;

	UPROPERTY()
	TSubclassOf<AEnemyGrenadeProjectile> GrenadeProjectileClass;

	bool bTelegraphing = false;
	bool bCooldownActive = false;

	/** Location solved during telegraph; used when the actual spawn fires. */
	FVector PendingLaunchVelocity = FVector::ZeroVector;
	FVector PendingLandingLocation = FVector::ZeroVector;

	FTimerHandle TelegraphTimerHandle;
	FTimerHandle CooldownTimerHandle;

	void SpawnGrenade();
	void OnCooldownElapsed();
};
