// AEnemyGrenadeProjectile — frag grenade: bouncing projectile, fuse-detonated radial damage.
// Spawner configures via Init(). Detonation filters damage by instigator team attitude.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyGrenadeProjectile.generated.h"

class UProjectileMovementComponent;
class USphereComponent;

UCLASS(Blueprintable)
class EXTRACTION_API AEnemyGrenadeProjectile : public AActor
{
	GENERATED_BODY()

public:
	AEnemyGrenadeProjectile();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Called by the spawner (UEnemyGrenadierComponent) to configure this projectile.
	 * Sets damage, blast radius, fuse duration, launch velocity, and the predicted landing location
	 * (used by BP to position the ground indicator).
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Grenade")
	void Init(float InDamage, float InDamageRadius, float InFuseTime, const FVector& LaunchVelocity, const FVector& InPredictedLanding);

	/** Predicted landing location set by the spawner — used by BP to position the ground indicator. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Grenade")
	FVector GetPredictedLanding() const { return PredictedLanding; }

	/** BP hook — implement explosion VFX/SFX here. Called on the server just before radial damage. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Enemy|Grenade")
	void OnExplode();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Grenade")
	TObjectPtr<USphereComponent> SphereCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Grenade")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

private:
	float Damage = 80.f;
	float DamageRadius = 350.f;

	UPROPERTY()
	FVector PredictedLanding = FVector::ZeroVector;

	FTimerHandle FuseTimerHandle;

	void Detonate();
};
