// UDirectorConfigData — per-mission tuning for the enemy director (tension, spawning, phase configs).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyTypes.h"
#include "DirectorConfigData.generated.h"

class AEnemyCharacter;

// --- Squad composition structs ---

USTRUCT(BlueprintType)
struct FSquadCompositionEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Squad")
	TSubclassOf<AEnemyCharacter> EnemyClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Squad", meta = (ClampMin = "1"))
	int32 Count = 1;
};

USTRUCT(BlueprintType)
struct FSquadComposition
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Squad")
	FName Name;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Squad", meta = (ClampMin = "0.01"))
	float Weight = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Squad")
	TArray<FSquadCompositionEntry> Entries;
};

// --- Per-phase config ---

USTRUCT(BlueprintType)
struct FMissionPhaseConfig
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phase")
	float IntensityCeiling = 60.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "1.0"))
	float SpawnCadenceSeconds = 25.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "1"))
	int32 MaxAlive = 12;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phase")
	TArray<FSquadComposition> Compositions;
};

// --- Data asset ---

UCLASS(BlueprintType)
class EXTRACTION_API UDirectorConfigData : public UDataAsset
{
	GENERATED_BODY()

public:
	UDirectorConfigData();

	// --- Tension ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension")
	float TensionDecayPerSecond = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension")
	float TensionPerPlayerHealthLost = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension")
	float TensionPerKill = 8.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension")
	float TensionPerEngagedEnemy = 3.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension")
	float PeakTensionThreshold = 75.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension")
	float ReliefEntryThreshold = 40.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension")
	float ReliefDuration = 25.f;

	// --- Spawning ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spawning")
	float SpawnDistanceMin = 1500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spawning")
	float SpawnDistanceMax = 4500.f;

	// --- Per-phase configs ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phases")
	FMissionPhaseConfig InfiltrationConfig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phases")
	FMissionPhaseConfig ObjectiveConfig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phases")
	FMissionPhaseConfig ExtractionConfig;

	/** Returns the config block for the given mission phase. */
	const FMissionPhaseConfig& GetPhaseConfig(EMissionPhase Phase) const;

	/** Logs and returns false if the config has values that would break the director. */
	bool Validate(FString& OutError) const;
};
