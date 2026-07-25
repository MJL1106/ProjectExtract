// UEnemyMoraleComponent — per-enemy morale (0-100): Confident → Shaken → Broken. Floor = fall-back, never rout (design §7).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyTypes.h"
#include "EnemyMoraleComponent.generated.h"

class UEnemyArchetypeData;
class AEnemyCharacter;
class UHealthComponent;
class USuppressionComponent;
class UBarkSubsystem;
class UBarkSetData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMoraleStateChanged, EMoraleState, OldState, EMoraleState, NewState);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UEnemyMoraleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyMoraleComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called by slice C after DA is available. Caches archetype profile values. */
	void InitFromArchetype(const UEnemyArchetypeData* Data);

	/** Called by slice C from HandleDeath. Clears timer and unbinds all delegates so corpses are inert. */
	void DeactivateForDeath();

	// --- Queries ---

	UFUNCTION(BlueprintPure, Category = "Enemy|Morale")
	EMoraleState GetMoraleState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Morale")
	float GetMorale01() const { return CurrentMorale / 100.f; }

	// --- Event ingress (called by slice C wiring or internally) ---

	/** This enemy hit a hostile target. */
	void NotifyDamagedTarget();

	/** This enemy killed a hostile target. */
	void NotifyTargetDowned();

	/** This enemy dropped below low-HP threshold (one-shot, called once). */
	void NotifyLowHealth();

	// --- Phase 5: squad-routed ingress (called by UEnemySquad, bypasses radius check) ---

	/** A squad member died. Applies ally/officer death morale loss without radius check.
	 *  DeathLocation gates the bark only — morale still drops squad-wide (radio). */
	void NotifySquadAllyDied(bool bWasOfficer, const FVector& DeathLocation);

	/** Officer rally: boosts morale, temporarily raises morale floor, un-pins Broken/Shaken. */
	void NotifyRally(float MoraleBoost, float FloorRaise);

	/** Director wave watchdog: restore morale above ShakenThreshold so the enemy regains bAggressive
	 *  pursuit in the combat fire task. Lighter than NotifyRally — no floor raise, no timer. */
	void RallyToConfident();

	/** Delegate broadcast on state transitions. Slice C's controller subscribes to write BB. */
	UPROPERTY(BlueprintAssignable, Category = "Enemy|Morale")
	FOnMoraleStateChanged OnMoraleStateChanged;

private:
	// --- Cached archetype profile ---
	bool bFearless = false;
	float MoraleFloor = 0.f;
	float MoraleEventResistance = 1.f;
	float ShakenThreshold = 60.f;
	float BrokenThreshold = 30.f;
	float RecoveryPerSecond = 2.f;
	float LossAllyDied = 15.f;
	float LossOfficerDied = 30.f;
	float LossSustainedSuppression = 10.f;
	float LossLowHealth = 20.f;
	float LossFlanked = 10.f;
	float GainDamagedTarget = 5.f;
	float GainTargetDowned = 15.f;

	// --- Cover protection & recovery tunables ---

	/** Multiplier applied to sustained-suppression and flank losses when owner has cover. Lower = more protection. */
	UPROPERTY(EditDefaultsOnly, Category = "Morale|Cover")
	float InCoverMoraleProtection = 0.25f;

	/** Multiplier applied to RecoveryPerSecond when owner is in cover and not heavily suppressed. */
	UPROPERTY(EditDefaultsOnly, Category = "Morale|Cover")
	float InCoverRecoveryMultiplier = 2.0f;

	/** Once Broken, morale must reach BrokenThreshold + this margin before returning to Shaken. Prevents flicker. */
	UPROPERTY(EditDefaultsOnly, Category = "Morale|Thresholds")
	float BrokenExitMargin = 15.f;

	/** Minimum interval (seconds) between flank-loss applications. Throttles per-tick flank drain. */
	UPROPERTY(EditDefaultsOnly, Category = "Morale|Flanking")
	float FlankLossInterval = 2.0f;

	// --- Runtime state ---
	float CurrentMorale = 100.f;
	EMoraleState CurrentState = EMoraleState::Confident;
	float LastLossWorldTime = -1e9f;
	float LastFlankLossWorldTime = -1e9f;
	bool bLowHealthFired = false;

	// --- Cached references ---
	UPROPERTY()
	TWeakObjectPtr<AEnemyCharacter> OwnerEnemy;

	UPROPERTY()
	TWeakObjectPtr<UHealthComponent> CachedHealthComp;

	UPROPERTY()
	TWeakObjectPtr<USuppressionComponent> CachedSuppressionComp;

	UPROPERTY()
	TObjectPtr<UBarkSetData> CachedBarkSet;

	// --- Phase 5: rally floor ---
	float RallyFloorRaise = 0.f;
	float RallyFloorDuration = 20.f;
	FTimerHandle RallyFloorTimerHandle;

	void ClearRallyFloor();

	/** Returns the effective morale floor (base + any active rally raise). */
	float GetEffectiveMoraleFloor() const;

	// --- Timers ---
	FTimerHandle MoraleTickHandle;

	static constexpr float MoraleTickInterval = 1.f;
	static constexpr float RecoveryGraceSeconds = 5.f;
	static constexpr float AllyDeathRadius = 2500.f;
	static constexpr float DeathWitnessEarshot = 1500.f;
	static constexpr float LowHealthThreshold = 0.3f;
	static constexpr float FlankedDotThreshold = -0.2f;

	/** Staggered 1s timer: recovery, sustained-suppression drain, low-HP check, flanked check. */
	void MoraleTick();

	/**
	 * Applies a morale delta (negative = loss, positive = gain). Scaled by 1/resistance. Clamps to floor.
	 * @param bIsContinuousDrain If true, does NOT reset the recovery grace clock (allows recovery under sustained fire).
	 */
	void ApplyMoraleDelta(float Delta, bool bIsContinuousDrain = false);

	/** Re-evaluates state from current value and broadcasts on change. */
	void EvaluateState();

	/** Checks if the combat target is behind this enemy's facing (cheap dot test). */
	void CheckFlanked(bool bInCover);

	/** Returns true if the owning controller's blackboard has BB_HasCover set. */
	bool IsOwnerInCover() const;

	/** Requests a bark through the subsystem if available. */
	void RequestBark(EBarkType Type) const;

	/** True if the owner plausibly witnessed a death there: close enough to hear it through walls,
	 *  or has line of sight to the spot. Gates ManDown barks so an enemy in another room doesn't
	 *  shout about a kill it can't see. */
	bool CanWitnessDeath(const FVector& DeathLocation) const;

	// --- Director death subscription ---
	UFUNCTION()
	void HandleEnemyDied(AEnemyCharacter* DeadEnemy, FVector Location, bool bWasOfficer);

	// --- Suppression subscription ---
	UFUNCTION()
	void HandleSuppressedStateChanged(bool bNowSuppressed);
};
