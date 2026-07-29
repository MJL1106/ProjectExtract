// UDirectorConfigData — per-phase config accessor + pinned defaults.

#include "DirectorConfigData.h"

UDirectorConfigData::UDirectorConfigData()
{
	InfiltrationConfig.SpawnCadenceSeconds = 45.f;
	InfiltrationConfig.MaxAlive = 8;
	InfiltrationConfig.IntensityCeiling = 40.f;

	ObjectiveConfig.SpawnCadenceSeconds = 25.f;
	ObjectiveConfig.MaxAlive = 12;
	ObjectiveConfig.IntensityCeiling = 60.f;

	ExtractionConfig.SpawnCadenceSeconds = 15.f;
	ExtractionConfig.MaxAlive = 20;
	ExtractionConfig.IntensityCeiling = 85.f;
}

const FMissionPhaseConfig& UDirectorConfigData::GetPhaseConfig(EMissionPhase Phase) const
{
	switch (Phase)
	{
	case EMissionPhase::Infiltration:	return InfiltrationConfig;
	case EMissionPhase::Objective:		return ObjectiveConfig;
	case EMissionPhase::Extraction:		return ExtractionConfig;
	default:							return InfiltrationConfig;
	}
}

bool UDirectorConfigData::Validate(FString& OutError) const
{
	bool bValid = true;

	struct FPhaseEntry { const TCHAR* Name; const FMissionPhaseConfig& Cfg; };
	const FPhaseEntry Phases[] = {
		{ TEXT("Infiltration"), InfiltrationConfig },
		{ TEXT("Objective"),    ObjectiveConfig },
		{ TEXT("Extraction"),   ExtractionConfig },
	};

	for (const FPhaseEntry& P : Phases)
	{
		if (P.Cfg.IntensityCeiling <= 0.f)
		{
			OutError += FString::Printf(TEXT("%s: IntensityCeiling <= 0 (permanent spawn block). "), P.Name);
			bValid = false;
		}
		if (P.Cfg.Compositions.IsEmpty())
		{
			OutError += FString::Printf(TEXT("%s: Compositions array is empty (spawn starvation). "), P.Name);
			bValid = false;
		}
		if (P.Cfg.ConcurrentSpawnZones < 1 || P.Cfg.ConcurrentSpawnZones > 4)
		{
			OutError += FString::Printf(TEXT("%s: ConcurrentSpawnZones=%d out of range [1,4]. "), P.Name, P.Cfg.ConcurrentSpawnZones);
			bValid = false;
		}
	}

	if (ReliefEntryThreshold >= PeakTensionThreshold)
	{
		OutError += TEXT("ReliefEntryThreshold >= PeakTensionThreshold (sawtooth collapse). ");
		bValid = false;
	}

	if (SpawnDistanceMin > SpawnDistanceMax)
	{
		OutError += TEXT("SpawnDistanceMin > SpawnDistanceMax. ");
		bValid = false;
	}

	if (StoreySeparationHeight <= 0.f)
	{
		OutError += TEXT("StoreySeparationHeight <= 0 (storey detection disabled). ");
		bValid = false;
	}

	if (SpawnDistanceMinDifferentStorey <= 0.f)
	{
		OutError += TEXT("SpawnDistanceMinDifferentStorey <= 0 (no minimum on other-storey zones). ");
		bValid = false;
	}

	if (SpawnDistanceMinDifferentStorey > SpawnDistanceMin)
	{
		OutError += TEXT("SpawnDistanceMinDifferentStorey > SpawnDistanceMin (other-storey zones are harder to use than same-floor -- defeats the purpose). ");
		bValid = false;
	}

	return bValid;
}
