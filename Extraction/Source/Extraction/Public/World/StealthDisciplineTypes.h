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
	float SprintPressurePerSecond = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float PressurePerShot = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float DecayPerSecond = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float WarningThreshold = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float EscalationThreshold = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float SprintSpeedThreshold = 550.f;
};

/** Pure per-actor pressure tracker. Not reflected -- lives on the discipline volume only. */
struct EXTRACTION_API FStealthPressureAccumulator
{
	float Pressure = 0.f;
	float ContinuousSprintSeconds = 0.f;
	bool bWarned = false;
	bool bEscalated = false;

	// Escalated supersedes Warned: a single sample crossing both thresholds returns only Escalated.
	EStealthPressureTransition Advance(float DeltaSeconds, bool bSprinting, int32 NormalShots,
		const FStealthDisciplineSettings& Settings)
	{
		// ClampMin metadata is editor-UI only; a zero/negative escalation threshold would trip on entry.
		if (Settings.EscalationThreshold <= 0.f) return EStealthPressureTransition::None;

		ContinuousSprintSeconds = bSprinting ? ContinuousSprintSeconds + DeltaSeconds : 0.f;

		const bool bSprintPressure = bSprinting && ContinuousSprintSeconds > Settings.SprintGraceSeconds;
		if (bSprintPressure) Pressure += Settings.SprintPressurePerSecond * DeltaSeconds;

		if (NormalShots > 0) Pressure += Settings.PressurePerShot * static_cast<float>(NormalShots);

		if (!bSprinting && NormalShots <= 0) Pressure -= Settings.DecayPerSecond * DeltaSeconds;

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
		bWarned = false;
		bEscalated = false;
	}
};
