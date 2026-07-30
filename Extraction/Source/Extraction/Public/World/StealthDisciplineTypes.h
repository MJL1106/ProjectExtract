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
	float SprintGraceSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float SprintPressurePerSecond = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float PressurePerShot = 15.f;

	/** Pressure cost per suppressed shot. Suppressed fire is much quieter and should
	 *  barely move the needle compared to unsuppressed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float SuppressedPressurePerShot = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float DecayPerSecond = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float WarningThreshold = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float EscalationThreshold = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float SprintSpeedThreshold = 550.f;

	/** How long a dip below SprintSpeedThreshold is tolerated before the sprint streak resets.
	 *  Zero disables lapse tolerance (old hard-reset behaviour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "5.0", UIMax = "5.0"))
	float SprintLapseGraceSeconds = 0.75f;

	static constexpr float MaxSprintLapseGraceSeconds = 5.f;
};

/** Pure per-actor pressure tracker. Not reflected -- lives on the discipline volume only. */
struct EXTRACTION_API FStealthPressureAccumulator
{
	float Pressure = 0.f;
	float ContinuousSprintSeconds = 0.f;
	float SecondsSinceSprint = 0.f;
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

		const int32 TotalShots = NormalShots + SuppressedShots;
		if (!bSprinting && TotalShots <= 0)
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
		bWarned = false;
		bEscalated = false;
	}
};
