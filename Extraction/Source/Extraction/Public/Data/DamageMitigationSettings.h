// Data-driven AI damage mitigation gate. Controls per-shot hit chance ramp and per-victim cadence cap.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DamageMitigationSettings.generated.h"

class UCurveFloat;

/**
 * Tuning asset for AI-fired damage mitigation. Player bullets always damage.
 * AI bullets (enemy->player/companion, companion->enemy) roll a per-shot hit
 * chance that ramps with continuous time-on-target, plus a per-victim cadence cap.
 * Non-damaging shots keep all existing FX/suppression/noise/morale behavior.
 */
UCLASS(BlueprintType)
class EXTRACTION_API UDamageMitigationSettings : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Master toggle. When false the gate is bypassed (all AI shots damage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mitigation")
	bool bEnabled = true;

	/** Hit chance at the instant the shooter acquires a new target (t=0). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mitigation|Ramp", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AcquireHitChance = 0.15f;

	/** Hit chance once the shooter has been on-target for RampTime seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mitigation|Ramp", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SettledHitChance = 0.9f;

	/** Seconds from target acquisition to full settled hit chance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mitigation|Ramp", meta = (ClampMin = "0.01"))
	float RampTime = 1.5f;

	/** Minimum gap (seconds) between shots before the ramp resets to t=0.
	 *  Actual reset gap = max(RampResetGapSeconds, ShotGapResetMultiplier / FireRate). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mitigation|Ramp", meta = (ClampMin = "0.0"))
	float RampResetGapSeconds = 0.6f;

	/** Multiplied by the weapon's seconds-per-shot interval to compute a fire-rate-aware ramp reset.
	 *  Ensures slow weapons (snipers) don't reset every shot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mitigation|Ramp", meta = (ClampMin = "1.0"))
	float ShotGapResetMultiplier = 2.5f;

	/** Minimum seconds between consecutive damaging hits on the same victim (cadence cap).
	 *  A failed hit-chance roll does NOT consume this window. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mitigation|Cadence", meta = (ClampMin = "0.0"))
	float PerVictimDamageInterval = 0.5f;

	/** Optional curve remapping the 0-1 ramp alpha. X = linear alpha, Y = remapped alpha.
	 *  When null, the ramp is linear. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mitigation|Ramp")
	TObjectPtr<UCurveFloat> RampCurve;

	// ---- Pure helpers (statically testable, no instance state) ----

	/** Returns the hit probability for the given TimeOnTarget against these settings.
	 *  Pure function: alpha = clamp(TimeOnTarget / RampTime, 0, 1), optionally remapped
	 *  by RampCurve, then lerped [AcquireHitChance, SettledHitChance]. */
	static float RampChance01(const UDamageMitigationSettings& S, float TimeOnTarget);

	/** Returns the effective gap (seconds) before which a new shot does NOT reset the ramp.
	 *  = max(S.RampResetGapSeconds, S.ShotGapResetMultiplier * SecondsPerShot).
	 *  SecondsPerShot = 1 / FireRateRPS (rounds per second). */
	static float EffectiveResetGap(const UDamageMitigationSettings& S, float FireRateRPS);
};
