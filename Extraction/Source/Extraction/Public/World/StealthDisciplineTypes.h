// FStealthDisciplineSettings/FStealthPressureAccumulator -- pure stealth-pressure accumulation shared
// by the Room 2 discipline volume and its automation tests. No engine ticking or actor references here.

#pragma once

#include "CoreMinimal.h"
#include "StealthDisciplineTypes.generated.h"

UENUM(BlueprintType)
enum class EStealthPressureTransition : uint8
{
	None,
	Warned,
	Escalated,
};

USTRUCT(BlueprintType)
struct EXTRACTION_API FStealthDisciplineSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float SprintGraceSeconds = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float SprintPressurePerSecond = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float PressurePerShot = 15.f;

	/** Pressure cost per suppressed shot. Suppressed fire is much quieter and should
	 *  barely move the needle compared to unsuppressed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float SuppressedPressurePerShot = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float DecayPerSecond = 4.f;

	/** Seconds of no pressure gain before decay begins. Stops corner-stops and brief pauses
	 *  from immediately refunding sprint pressure. Zero restores the old instant-decay behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", UIMax = "10.0"))
	float DecayDelaySeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float WarningThreshold = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float EscalationThreshold = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float SprintSpeedThreshold = 550.f;

	/** How long a dip below SprintSpeedThreshold is tolerated before the sprint streak resets.
	 *  Zero disables lapse tolerance (old hard-reset behaviour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "5.0", UIMax = "5.0"))
	float SprintLapseGraceSeconds = 2.f;

	static constexpr float MaxSprintLapseGraceSeconds = 5.f;
};

/** Pure per-actor pressure tracker. Not reflected -- lives on the discipline volume only. */
struct EXTRACTION_API FStealthPressureAccumulator
{
	float Pressure = 0.f;
	float ContinuousSprintSeconds = 0.f;
	float SecondsSinceSprint = 0.f;
	float SecondsSincePressureGain = 0.f;
	bool bWarned = false;
	bool bEscalated = false;

	// Escalated supersedes Warned: a single sample crossing both thresholds returns only Escalated.
	EStealthPressureTransition Advance(float DeltaSeconds, bool bSprinting,
		int32 NormalShots, int32 SuppressedShots,
		const FStealthDisciplineSettings& Settings)
	{
		// ClampMin metadata is editor-UI only; a zero/negative escalation threshold would trip on entry.
		if (Settings.EscalationThreshold <= 0.f) return EStealthPressureTransition::None;

		const float EffectiveLapseGrace = FMath::Clamp(Settings.SprintLapseGraceSeconds, 0.f,
			FStealthDisciplineSettings::MaxSprintLapseGraceSeconds);

		if (bSprinting)
		{
			SecondsSinceSprint = 0.f;
			ContinuousSprintSeconds += DeltaSeconds;
		}
		else
		{
			SecondsSinceSprint += DeltaSeconds;
			// Tolerated lapse: preserve the sprint streak across a brief dip. Decay still runs below.
			const bool bToleratedLapse = ContinuousSprintSeconds > 0.f && EffectiveLapseGrace > 0.f
				&& SecondsSinceSprint <= EffectiveLapseGrace;

			if (!bToleratedLapse) ContinuousSprintSeconds = 0.f;
		}

		const bool bSprintPressure = bSprinting && ContinuousSprintSeconds > Settings.SprintGraceSeconds;
		if (bSprintPressure) Pressure += Settings.SprintPressurePerSecond * DeltaSeconds;

		if (NormalShots > 0) Pressure += Settings.PressurePerShot * static_cast<float>(NormalShots);
		if (SuppressedShots > 0) Pressure += Settings.SuppressedPressurePerShot * static_cast<float>(SuppressedShots);

		// Delay window: any tick that actually gained pressure resets it; otherwise it accrues,
		// gating decay below so a corner-stop doesn't instantly refund a sprint burst.
		const bool bGainedPressure = bSprintPressure || NormalShots > 0 || SuppressedShots > 0;
		if (bGainedPressure)
			SecondsSincePressureGain = 0.f;
		else
			SecondsSincePressureGain += DeltaSeconds;

		const int32 TotalShots = NormalShots + SuppressedShots;
		const bool bDecayDelayElapsed = SecondsSincePressureGain > FMath::Max(0.f, Settings.DecayDelaySeconds);
		if (!bSprinting && TotalShots <= 0 && bDecayDelayElapsed)
			Pressure -= Settings.DecayPerSecond * DeltaSeconds;

		Pressure = FMath::Clamp(Pressure, 0.f, Settings.EscalationThreshold);

		if (!bEscalated && Pressure >= Settings.EscalationThreshold)
		{
			bEscalated = true;
			bWarned = true;
			return EStealthPressureTransition::Escalated;
		}

		if (!bWarned && Pressure >= Settings.WarningThreshold)
		{
			bWarned = true;
			return EStealthPressureTransition::Warned;
		}

		return EStealthPressureTransition::None;
	}

	void Reset()
	{
		Pressure = 0.f;
		ContinuousSprintSeconds = 0.f;
		SecondsSinceSprint = 0.f;
		SecondsSincePressureGain = 0.f;
		bWarned = false;
		bEscalated = false;
	}
};
