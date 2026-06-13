// UEnemyDirectorSubsystem -- level-wide alert state + tension director + spawn pipeline.
// v1 (global alert + corpse registry + OnEnemyDied) preserved; v2 adds mission phases,
// tension sawtooth (Build/Peak/Relief), and the spawn pipeline (composition pick, zone pick, squad spawn).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyTypes.h"
#include "EnemyDirectorSubsystem.generated.h"

class AEnemyCharacter;
class AEnemySpawnZone;
class UDirectorConfigData;
class UHealthComponent;
class UEnemySquadSubsystem;
class UEnemySquad;
struct FMissionPhaseConfig;
struct FSquadComposition;

UENUM(BlueprintType)
enum class EDirectorState : uint8
{
	Build	UMETA(DisplayName = "Build"),
	Peak	UMETA(DisplayName = "Peak"),
	Relief	UMETA(DisplayName = "Relief"),
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGlobalAlertChanged, EGlobalAlertLevel, OldLevel, EGlobalAlertLevel, NewLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEnemyDied, AEnemyCharacter*, DeadEnemy, FVector, Location, bool, bWasOfficer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMissionPhaseChanged, EMissionPhase, OldPhase, EMissionPhase, NewPhase);

UCLASS()
class EXTRACTION_API UEnemyDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---------- v1 API (preserved) ----------

	UFUNCTION(BlueprintPure, Category = "Enemy|Director")
	EGlobalAlertLevel GetAlertLevel() const { return AlertLevel; }

	void ReportEnemySearching();
	void ReportEnemyCombat();
	void ReportBodyDiscovered();

	UFUNCTION(BlueprintCallable, Category = "Enemy|Director")
	void TripAlarm();

	void RegisterCorpse(AEnemyCharacter* Corpse);

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Director")
	FOnGlobalAlertChanged OnGlobalAlertChanged;

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Director")
	FOnEnemyDied OnEnemyDied;

	// ---------- v2 API: mission phases ----------

	UFUNCTION(BlueprintCallable, Category = "Enemy|Director")
	void SetMissionPhase(EMissionPhase NewPhase);

	UFUNCTION(BlueprintPure, Category = "Enemy|Director")
	EMissionPhase GetMissionPhase() const { return CurrentMissionPhase; }

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Director")
	FOnMissionPhaseChanged OnMissionPhaseChanged;

	// ---------- v2 API: config ----------

	UFUNCTION(BlueprintCallable, Category = "Enemy|Director")
	void SetDirectorConfig(UDirectorConfigData* InConfig);

	UFUNCTION(BlueprintPure, Category = "Enemy|Director")
	UDirectorConfigData* GetDirectorConfig() const { return Config.Get(); }

	// ---------- v2 API: director state ----------

	UFUNCTION(BlueprintPure, Category = "Enemy|Director")
	EDirectorState GetDirectorState() const { return DirectorState; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Director")
	float GetTension() const { return Tension; }

	// ---------- v2 API: spawn zone registration ----------

	void RegisterSpawnZone(AEnemySpawnZone* Zone);
	void UnregisterSpawnZone(AEnemySpawnZone* Zone);

private:

	// ---------- v1 internals ----------

	void Escalate(EGlobalAlertLevel NewLevel);

	static constexpr int32 MaxCorpses = 10;

	EGlobalAlertLevel AlertLevel = EGlobalAlertLevel::Calm;
	int32 BodiesDiscovered = 0;

	TArray<TWeakObjectPtr<AEnemyCharacter>> Corpses;

	// ---------- v2: named fallback constants ----------

	static constexpr float DefaultTensionDecay = 4.f;
	static constexpr float DefaultTensionPerHealthLost = 0.8f;
	static constexpr float DefaultTensionPerKill = 8.f;
	static constexpr float DefaultTensionPerEngaged = 3.f;
	static constexpr float DefaultEngageRadius = 3000.f;
	static constexpr float DefaultPeakThreshold = 75.f;
	static constexpr float DefaultReliefEntry = 40.f;
	static constexpr float DefaultReliefDuration = 25.f;
	static constexpr float DefaultSpawnDistMin = 1500.f;
	static constexpr float DefaultSpawnDistMax = 4500.f;
	static constexpr float NavProjectExtentXY = 200.f;
	static constexpr float NavProjectExtentZ = 400.f;
	static constexpr int32 MaxZoneCandidates = 5;

	// ---------- v2: tension ----------

	void DirectorTick();
	void UpdateTension(float DeltaSeconds, float EngagedCount);
	float PollPlayerHealthLost();
	void UpdateSawtooth(float DeltaSeconds);
	void TrySpawn(int32 AliveCount);
	bool ShouldSpawn(int32 AliveCount) const;

	struct FEnemySweepResult
	{
		int32 AliveCount = 0;
		float EngagedCount = 0.f;
	};

	FEnemySweepResult SweepEnemies() const;

	float Tension = 0.f;
	int32 RecentKills = 0;
	float CachedPlayerHealthLastTick = -1.f;
	TWeakObjectPtr<UHealthComponent> CachedPlayerHealth;

	// ---------- v2: sawtooth ----------

	EDirectorState DirectorState = EDirectorState::Build;
	float ReliefTimer = 0.f;

	// ---------- v2: mission phase ----------

	EMissionPhase CurrentMissionPhase = EMissionPhase::Infiltration;

	// ---------- v2: config ----------

	UPROPERTY()
	TObjectPtr<UDirectorConfigData> Config;

	bool bConfigFromZone = false;

	// ---------- v2: spawn zones ----------

	TArray<TWeakObjectPtr<AEnemySpawnZone>> SpawnZones;
	void PruneStaleZones();

	// ---------- v2: spawn pipeline ----------

	float TimeSinceLastSpawn = 0.f;
	const FMissionPhaseConfig& GetCurrentPhaseConfig() const;
	int32 GetCompositionSize(const FSquadComposition& Comp) const;
	bool PickComposition(const FMissionPhaseConfig& PhaseConfig, int32 AliveCount, FSquadComposition& OutComposition) const;
	AEnemySpawnZone* PickSpawnZone(const FVector& PlayerLoc, const FVector& ViewLoc, const FRotator& ViewRot) const;
	bool IsPointInPlayerSightline(const FVector& Point, const FVector& ViewLoc, const FVector& ViewDir, const FCollisionQueryParams& QueryParams) const;
	void SpawnSquadAtZone(const FSquadComposition& Composition, AEnemySpawnZone* Zone);
	AEnemyCharacter* SpawnEntryAtZone(UWorld* World, TSubclassOf<AEnemyCharacter> EnemyClass, AEnemySpawnZone* Zone, int32 Index);
	void SeedSquadWithFight(UEnemySquad* Squad) const;

	TWeakObjectPtr<UEnemySquadSubsystem> CachedSquadSubsystem;

	bool bLoggedNoComposition = false;
	bool bLoggedNoZone = false;

	// ---------- v2: kill tracking ----------

	UFUNCTION()
	void HandleEnemyKilled(AEnemyCharacter* DeadEnemy, FVector Location, bool bWasOfficer);

	// ---------- timers ----------

	static constexpr float DirectorTickInterval = 1.f;

	FTimerHandle DirectorTimerHandle;
};
