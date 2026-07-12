// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionGameInstance.h"

UExtractionGameInstance::UExtractionGameInstance()
{
}

void UExtractionGameInstance::SetCheckpoint(FName LevelName, ELevelObjectiveStep Step)
{
	CheckpointLevelName = LevelName;
	LastCheckpointStep = Step;
}

ELevelObjectiveStep UExtractionGameInstance::GetCheckpointForLevel(FName LevelName) const
{
	return (LevelName != NAME_None && LevelName == CheckpointLevelName)
		? LastCheckpointStep : ELevelObjectiveStep::Inactive;
}

void UExtractionGameInstance::ClearCheckpoint()
{
	CheckpointLevelName = NAME_None;
	LastCheckpointStep = ELevelObjectiveStep::Inactive;
}
