// FDirectorWaveRequest/FDirectorWaveProgress/FDirectorProfileSelector -- pure finite-wave state and
// contextual profile selection for the Enemy Director. No engine ticking or actor references here;
// consumed by UEnemyDirectorSubsystem and Room 2 world actors.

#pragma once

#include "CoreMinimal.h"
#include "EnemyTypes.h"
#include "DirectorConfigData.h"
#include "DirectorWaveTypes.generated.h"

USTRUCT(BlueprintType)
struct EXTRACTION_API FDirectorWaveRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	FName WaveId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave", meta = (ClampMin = "1"))
	int32 TargetSquads = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	EMissionPhase MissionPhase = EMissionPhase::Objective;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	TObjectPtr<UDirectorConfigData> ConfigOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave", meta = (ClampMin = "5.0"))
	float BlockedWarningSeconds = 30.f;

	/** Seed each spawned wave squad straight into Combat targeting the player. Off = squads get the
	 *  default sighting seed (Searching) and must confirm the fight through their own perception. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	bool bAutoEngage = true;

	/** Seconds between wave squad spawns. 0 = use the effective phase config's SpawnCadenceSeconds
	 *  (tuned for ambient pacing — often too slow for a scripted assault). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave", meta = (ClampMin = "0.0"))
	float SpawnCadenceOverride = 0.f;
};

UENUM(BlueprintType)
enum class EWaveCompletionAction : uint8
{
	UnlockExit,
	CompleteLevel,
	BroadcastOnly,
};

/** Pure squad/member tracker for a single finite wave. Not reflected -- lives on the subsystem only. */
struct EXTRACTION_API FDirectorWaveProgress
{
	int32 TargetSquads = 0;
	int32 SpawnedSquads = 0;
	int32 RemainingMembers = 0;

	void Begin(int32 InTargetSquads)
	{
		TargetSquads = InTargetSquads;
		SpawnedSquads = 0;
		RemainingMembers = 0;
	}

	bool RecordSuccessfulSquad(int32 SpawnedMemberCount)
	{
		if (SpawnedMemberCount <= 0) return false;

		++SpawnedSquads;
		RemainingMembers += SpawnedMemberCount;
		return true;
	}

	void RecordMemberEnded()
	{
		RemainingMembers = FMath::Max(RemainingMembers - 1, 0);
	}

	bool IsActive() const { return TargetSquads > 0; }

	bool CanSpawnMore() const
	{
		return IsActive() && SpawnedSquads < TargetSquads;
	}

	bool IsComplete() const
	{
		return IsActive() && SpawnedSquads >= TargetSquads && RemainingMembers == 0;
	}
};

/** Resolves which config/phase is currently authoritative: wave beats punishment beats base. */
struct EXTRACTION_API FDirectorProfileSelector
{
	static const UDirectorConfigData* SelectConfig(bool bWaveActive, const UDirectorConfigData* Wave,
		bool bPunishmentActive, const UDirectorConfigData* Punishment, const UDirectorConfigData* Base)
	{
		if (bWaveActive && Wave) return Wave;
		if (bPunishmentActive && Punishment) return Punishment;
		return Base;
	}

	static EMissionPhase SelectPhase(bool bWaveActive, EMissionPhase Wave,
		bool bPunishmentActive, EMissionPhase Punishment, EMissionPhase Base)
	{
		if (bWaveActive) return Wave;
		if (bPunishmentActive) return Punishment;
		return Base;
	}
};
