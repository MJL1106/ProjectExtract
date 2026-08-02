// UEnemyGrenadierComponent — manages grenade supply, telegraph, and lob for the Grenadier archetype.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyGrenadierComponent.generated.h"

class UEnemyArchetypeData;
class AEnemyGrenadeProjectile;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGrenadeTelegraph, FVector, PredictedLanding, float, TimeToImpact);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGrenadeCancelled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGrenadeThrown);

/** Owner-agnostic init values — mirror of the UEnemyArchetypeData grenadier block, for non-enemy
 *  throwers (the companion inits from UCompanionTuningDataAsset). */
USTRUCT()
struct FGrenadierInitParams
{
	GENERATED_BODY()

	int32 GrenadeSupply = 8;
	float GrenadeCooldown = 6.f;
	float GrenadeFuseTime = 2.5f;
	float GrenadeTelegraphTime = 1.f;
	float GrenadeMinRange = 400.f;
	float GrenadeMaxRange = 2000.f;
	float GrenadeDamage = 80.f;
	float GrenadeDamageRadius = 600.f;
	float GrenadeLandingDistanceScale = 1.f;
	FName GrenadeThrowSocket = TEXT("GrenadeSocket");

	UPROPERTY()
	TSubclassOf<AEnemyGrenadeProjectile> GrenadeProjectileClass;
};

UCLASS(ClassGroup = "Enemy", meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UEnemyGrenadierComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyGrenadierComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Initialise supply/timing/damage from the archetype data asset. */
	void InitFromArchetype(const UEnemyArchetypeData* Data);

	/** Initialise from explicit values — non-enemy throwers (companion). */
	void InitFromParams(const FGrenadierInitParams& Params);

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

	/**
	 * Primary, notify-driven grenade release. Called by UAnimNotify_EnemyGrenadeRelease at the
	 * hand-open frame. Clears the fallback telegraph timer and spawns the grenade from the socket.
	 * No-ops if bTelegraphing is false (guards against duplicate spawns).
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Grenadier")
	void ReleaseGrenade();

	/** True while the telegraph timer is running (before the projectile spawns). */
	UFUNCTION(BlueprintPure, Category = "Enemy|Grenadier")
	bool IsTelegraphing() const { return bTelegraphing; }

	/** Broadcast at the start of a throw — BPs use this to show the grenade indicator. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Grenadier")
	FOnGrenadeTelegraph OnGrenadeTelegraph;

	/** Broadcast when a throw is cancelled mid-telegraph — BPs use this to hide the grenade indicator. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Grenadier")
	FOnGrenadeCancelled OnGrenadeCancelled;

	/** Broadcast only once the projectile has actually spawned. Anything that must not fire for a
	 *  cancelled wind-up (the companion's "frag out" call) hangs off this, not OnGrenadeTelegraph. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Grenadier")
	FOnGrenadeThrown OnGrenadeThrown;

private:
	/** Cached from DA at InitFromArchetype. */
	int32 GrenadeSupply = 8;
	float GrenadeCooldown = 6.f;
	float GrenadeFuseTime = 2.5f;
	float GrenadeTelegraphTime = 1.f;
	float GrenadeMinRange = 400.f;
	float GrenadeMaxRange = 2000.f;
	float GrenadeDamage = 80.f;
	float GrenadeDamageRadius = 600.f;

	UPROPERTY()
	TSubclassOf<AEnemyGrenadeProjectile> GrenadeProjectileClass;

	bool bTelegraphing = false;
	bool bCooldownActive = false;

	/** Socket name read from the archetype DA; grenade launches from here at release. */
	FName GrenadeThrowSocket = TEXT("GrenadeSocket");

	/** Horizontal distance scale for the landing point (1.0 = exact target, <1 = shorter throw). */
	float GrenadeLandingDistanceScale = 1.f;

	/** Velocity solved at TryThrowAt commit time; used as fallback when the re-solve at release fails. */
	FVector PendingLaunchVelocity = FVector::ZeroVector;

	/** Target landing location stored at commit time; re-arced from the socket at release. */
	FVector PendingLandingLocation = FVector::ZeroVector;

	/** Arc height that passed the trajectory check at commit time; reused for the release re-solve. */
	float PendingArcParam = 0.5f;

	FTimerHandle TelegraphTimerHandle;
	FTimerHandle CooldownTimerHandle;

	/** Shared spawn path — called by both ReleaseGrenade() and the fallback timer. */
	void SpawnGrenadeFromSocket();
	void OnCooldownElapsed();

	/** MaxSpeed from the projectile class CDO — the launch velocity gets clamped to it. 0 = uncapped. */
	float GetProjectileMaxSpeed() const;
};
