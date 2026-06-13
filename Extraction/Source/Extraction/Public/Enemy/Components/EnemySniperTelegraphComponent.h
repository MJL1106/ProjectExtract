// UEnemySniperTelegraphComponent — laser telegraph timing and state for the Sniper archetype.
// Pure timing/state; BT task reads IsReadyToFire() and fires the weapon itself.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemySniperTelegraphComponent.generated.h"

class UEnemyArchetypeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSniperLaserChanged, bool, bLaserOn, AActor*, Target);

UCLASS(ClassGroup = "Enemy", meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UEnemySniperTelegraphComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemySniperTelegraphComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Initialise timing from the archetype data asset. */
	void InitFromArchetype(const UEnemyArchetypeData* Data);

	/**
	 * Turns the laser on and starts the aim telegraph timer.
	 * After SniperAimTelegraphTime elapses, IsReadyToFire() returns true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Sniper")
	void BeginTelegraph(AActor* Target);

	/** Turns the laser off and cancels the telegraph. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Sniper")
	void CancelTelegraph();

	/** True while the aim-laser is active (before the shot). */
	UFUNCTION(BlueprintPure, Category = "Enemy|Sniper")
	bool IsTelegraphing() const { return bTelegraphing; }

	/** True after SniperAimTelegraphTime has elapsed following BeginTelegraph(). */
	UFUNCTION(BlueprintPure, Category = "Enemy|Sniper")
	bool IsReadyToFire() const { return bReadyToFire; }

	/**
	 * Called by the BT task after firing. Turns the laser off, increments shot counter,
	 * and starts the shot-cooldown gate (CanBeginTelegraph() false until elapsed).
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Sniper")
	void NotifyShotFired();

	/** True when not in cooldown — BT task polls before calling BeginTelegraph(). */
	UFUNCTION(BlueprintPure, Category = "Enemy|Sniper")
	bool CanBeginTelegraph() const { return !bShotCooldownActive && !bTelegraphing; }

	/** How many shots have been fired since the last ResetRelocateCounter() call. */
	UFUNCTION(BlueprintPure, Category = "Enemy|Sniper")
	int32 GetShotsSinceRelocate() const { return ShotsSinceRelocate; }

	/** Called by the BT task after a successful relocate. */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Sniper")
	void ResetRelocateCounter();

	/** Broadcast when the laser turns on or off — BP uses this to toggle the laser VFX. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Sniper")
	FOnSniperLaserChanged OnLaserChanged;

private:
	float SniperAimTelegraphTime = 2.f;
	float SniperShotCooldown = 4.f;

	bool bTelegraphing = false;
	bool bReadyToFire = false;
	bool bShotCooldownActive = false;

	int32 ShotsSinceRelocate = 0;

	FTimerHandle TelegraphTimerHandle;
	FTimerHandle ShotCooldownTimerHandle;

	UPROPERTY()
	TWeakObjectPtr<AActor> TelegraphTarget;

	void OnTelegraphElapsed();
	void OnShotCooldownElapsed();
};
