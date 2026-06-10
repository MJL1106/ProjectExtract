// Shared suppression component for AI-controlled pawns (enemies + companion, never the player).
// Near-miss fire raises a 0-1 suppression value; fast decay. IsSuppressed gates BT peek/fire.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SuppressionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSuppressedStateChanged, bool, bNowSuppressed);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class EXTRACTION_API USuppressionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USuppressionComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --- Public API ---

	/** Called by WeaponBase per near-miss. Weight defaults to 1 (normal round). */
	void RegisterNearMiss(float Weight = 1.f);

	/** Silently zero suppression and stop the decay timer without broadcasting. Called from HandleDeath. */
	void DeactivateForDeath();

	/** True when suppression value >= SuppressedThreshold (with hysteresis on un-suppress). */
	UFUNCTION(BlueprintPure, Category = "Suppression")
	bool IsSuppressed() const { return bIsSuppressed; }

	/** Current suppression as a 0-1 fraction. */
	UFUNCTION(BlueprintPure, Category = "Suppression")
	float GetSuppression01() const { return SuppressionValue; }

	/** Configure resistance from archetype data. Enemies pass DA SuppressionResistance; companion keeps default 1.0. */
	void ConfigureSuppression(float Resistance);

	// --- Delegates ---

	UPROPERTY(BlueprintAssignable, Category = "Suppression|Events")
	FOnSuppressedStateChanged OnSuppressedStateChanged;

protected:

	/** Suppression added per near-miss (before resistance scaling). */
	UPROPERTY(EditDefaultsOnly, Category = "Suppression|Tuning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SuppressionPerNearMiss = 0.25f;

	/** Suppression decay per second while the decay timer is running. */
	UPROPERTY(EditDefaultsOnly, Category = "Suppression|Tuning", meta = (ClampMin = "0.0"))
	float DecayPerSecond = 0.6f;

	/** Value at or above which IsSuppressed() returns true. */
	UPROPERTY(EditDefaultsOnly, Category = "Suppression|Tuning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SuppressedThreshold = 0.5f;

	/** Value below which IsSuppressed() clears (hysteresis band). */
	UPROPERTY(EditDefaultsOnly, Category = "Suppression|Tuning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnsuppressedThreshold = 0.3f;

	/** Interval for the decay timer tick. */
	UPROPERTY(EditDefaultsOnly, Category = "Suppression|Tuning", meta = (ClampMin = "0.01"))
	float DecayTickInterval = 0.1f;

private:

	void StartDecayTimer();
	void OnDecayTick();

	float SuppressionValue = 0.f;
	float Resistance = 1.f;
	bool bIsSuppressed = false;

	FTimerHandle DecayTimerHandle;
};
