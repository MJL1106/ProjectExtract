// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ExtractionTypes.h"
#include "World/LevelObjectiveFlow.h"
#include "ExtractionGameInstance.generated.h"

/**
 * Persistent game instance for Extraction.
 * Survives level transitions — use for player loadouts,
 * progression data, and session management.
 */
UCLASS()
class EXTRACTION_API UExtractionGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UExtractionGameInstance();

	virtual void Init() override;

	/** Records the checkpoint the current run has reached. LevelName keys the record so a stale
	 *  checkpoint can never leak into a different map. */
	void SetCheckpoint(FName LevelName, ELevelObjectiveStep Step);

	/** The recorded checkpoint for LevelName, or Inactive when none exists for that level. */
	ELevelObjectiveStep GetCheckpointForLevel(FName LevelName) const;

	/** Records the AObjectiveStep chain's resume point by StepId. Kept apart from the legacy enum
	 *  checkpoint above so a level can be migrated one chain at a time. */
	void SetStepCheckpoint(FName LevelName, FName StepId);

	/** The recorded step checkpoint for LevelName, or None when that level has none. */
	FName GetStepCheckpointForLevel(FName LevelName) const;

	/** Drops both records and the loadout snapshot — call on level completion so the next run starts clean. */
	void ClearCheckpoint();

	// ---- Loadout snapshot (checkpoint restart) ----

	/** Stores a loadout snapshot keyed by level name. */
	void SetLoadoutSnapshot(FName LevelName, const FCheckpointLoadoutSnapshot& Snapshot);

	/** Returns the snapshot for LevelName, or an invalid (bValid=false) snapshot when none exists. */
	const FCheckpointLoadoutSnapshot& GetLoadoutSnapshotForLevel(FName LevelName) const;

private:
	FName CheckpointLevelName;
	ELevelObjectiveStep LastCheckpointStep = ELevelObjectiveStep::Inactive;

	FName StepCheckpointLevelName;
	FName LastCheckpointStepId;

	/** Level name the loadout snapshot belongs to — prevents leaking into a different map. */
	FName LoadoutSnapshotLevelName;

	/** Loadout captured at the last checkpoint. UPROPERTY because it holds TSubclassOf<> refs. */
	UPROPERTY()
	FCheckpointLoadoutSnapshot LoadoutSnapshot;
};
