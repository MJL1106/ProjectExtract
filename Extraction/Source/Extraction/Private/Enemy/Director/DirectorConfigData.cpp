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

	return bValid;
}
