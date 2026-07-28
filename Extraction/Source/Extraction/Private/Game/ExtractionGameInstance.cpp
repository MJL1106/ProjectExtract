// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionGameInstance.h"
#include "Misc/App.h"

UExtractionGameInstance::UExtractionGameInstance()
{
}

void UExtractionGameInstance::Init()
{
	Super::Init();

	// Placeholder mute: the only audio right now is kit-default leftovers (breathing,
	// footsteps, aim whoosh) — silence everything until the sound pass starts.
	// DELETE this line when audio work begins.
	FApp::SetVolumeMultiplier(0.f);
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

void UExtractionGameInstance::SetStepCheckpoint(FName LevelName, FName StepId)
{
	StepCheckpointLevelName = LevelName;
	LastCheckpointStepId = StepId;
}

FName UExtractionGameInstance::GetStepCheckpointForLevel(FName LevelName) const
{
	return (LevelName != NAME_None && LevelName == StepCheckpointLevelName)
		? LastCheckpointStepId : NAME_None;
}

void UExtractionGameInstance::ClearCheckpoint()
{
	CheckpointLevelName = NAME_None;
	LastCheckpointStep = ELevelObjectiveStep::Inactive;
	StepCheckpointLevelName = NAME_None;
	LastCheckpointStepId = NAME_None;
}
