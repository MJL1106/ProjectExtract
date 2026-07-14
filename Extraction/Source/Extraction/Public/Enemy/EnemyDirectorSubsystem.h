// UEnemyDirectorSubsystem -- level-wide alert state + tension director + spawn pipeline.
// v1 (global alert + corpse registry + OnEnemyDied) preserved; v2 adds mission phases,
// tension sawtooth (Build/Peak/Relief), and the spawn pipeline (composition pick, zone pick, squad spawn).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyTypes.h"
#include "DirectorWaveTypes.h"
#include "EnemyDirectorSubsystem.generated.h"

class ACompanionCharacter;
class AEnemyCharacter;
class AEnemyDirectorScopeVolume;
class AEnemySpawnZone;
class UDirectorConfigData;
class UHealthComponent;
class UEnemySquadSubsystem;
class UEnemySquad;
class UNavigationSystemV1;
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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDirectorWaveStarted, FName, WaveId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDirectorWaveProgress, FName, WaveId, int32, SpawnedSquads, int32, RemainingMembers);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDirectorWaveCompleted, FName, WaveId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDirectorWaveBlocked, FName, WaveId, FText, Reason);

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

	/** World seconds of the most recent enemy Combat-state entry reported to the director.
	 *  Unlike the alert ladder (a ratchet that never de-escalates), this is a decaying event
	 *  stamp — compare against a reference time to ask "has a fight started since X?". */
	float GetLastCombatReportTime() const { return LastCombatReportTime; }

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

	void RegisterScopeVolume(AEnemyDirectorScopeVolume* ScopeVolume);
	void UnregisterScopeVolume(AEnemyDirectorScopeVolume* ScopeVolume);

	// ---------- v2 API: finite wave lifecycle ----------

	UFUNCTION(BlueprintCallable, Category = "Enemy|Director|Wave")
	bool StartWave(const FDirectorWaveRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "Enemy|Director|Wave")
	void CancelWave(FName WaveId);

	bool ActivatePunishmentProfile(AActor* Source, UDirectorConfigData* Profile, EMissionPhase Phase);
	void DeactivatePunishmentProfile(AActor* Source);

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Director|Wave")
	FOnDirectorWaveStarted OnDirectorWaveStarted;

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Director|Wave")
	FOnDirectorWaveProgress OnDirectorWaveProgress;

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Director|Wave")
	FOnDirectorWaveCompleted OnDirectorWaveCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Enemy|Director|Wave")
	FOnDirectorWaveBlocked OnDirectorWaveBlocked;

	UFUNCTION(BlueprintPure, Category = "Enemy|Director|Wave")
	FName GetActiveWaveId() const { return ActiveWaveRequest.WaveId; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Director|Wave")
	bool IsWaveActive() const { return WaveProgress.IsActive(); }

	UFUNCTION(BlueprintPure, Category = "Enemy|Director|Wave")
	int32 GetWaveSpawnedSquads() const { return WaveProgress.SpawnedSquads; }

	UFUNCTION(BlueprintPure, Category = "Enemy|Director|Wave")
	int32 GetWaveRemainingMembers() const { return WaveProgress.RemainingMembers; }

private:

	// ---------- v1 internals ----------

	void Escalate(EGlobalAlertLevel NewLevel);

	static constexpr int32 MaxCorpses = 10;

	EGlobalAlertLevel AlertLevel = EGlobalAlertLevel::Calm;
	int32 BodiesDiscovered = 0;
	float LastCombatReportTime = -1e9f;

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

	// Zone selection: sightline samples are raised to head height above the projected
	// floor, and zones are scored (distance band + companion proximity) instead of
	// picked randomly from the first few that pass.
	static constexpr float SpawnEyeHeightOffset = 160.f;
	static constexpr float SpawnGroundClearance = 2.f;
	static constexpr int32 MinSightlineSamples = 3;
	static constexpr float IdealDistanceFrac = 0.35f;
	static constexpr float DistanceBandBonus = 25.f;
	static constexpr float CompanionProximityPenalty = 60.f;
	static constexpr float ZoneScoreJitter = 10.f;

	// ---------- v2: tension ----------

	void DirectorTick();
	void UpdateTension(float DeltaSeconds, float EngagedCount);
	float PollPlayerHealthLost();
	void UpdateSawtooth(float DeltaSeconds);
	bool TrySpawn(int32 AliveCount, TArray<AEnemyCharacter*>* OutSpawned = nullptr);
	bool ShouldSpawn(int32 AliveCount) const;

	// ---------- v2: effective config / phase selection ----------

	const UDirectorConfigData* GetEffectiveConfig() const;
	EMissionPhase GetEffectivePhase() const;

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

	// ---------- v2: optional director scope volumes ----------

	TArray<TWeakObjectPtr<AEnemyDirectorScopeVolume>> ScopeVolumes;
	void PruneStaleScopeVolumes();
	bool HasActiveScopeVolumes() const;
	bool IsActorInsideAnyScope(const AActor* Actor) const;
	bool IsPointInsideAnyScope(const FVector& Point) const;

	// ---------- v2: spawn pipeline ----------

	float TimeSinceLastSpawn = 0.f;
	const FMissionPhaseConfig& GetCurrentPhaseConfig() const;
	int32 GetCompositionSize(const FSquadComposition& Comp) const;
	bool PickComposition(const FMissionPhaseConfig& PhaseConfig, int32 AliveCount, FSquadComposition& OutComposition) const;
	AEnemySpawnZone* PickSpawnZone(const FVector& PlayerLoc, const FVector& ViewLoc, const FRotator& ViewRot, int32 SquadSize) const;
	bool IsPointInPlayerSightline(const FVector& Point, const FVector& ViewLoc, const FVector& ViewDir, const FCollisionQueryParams& QueryParams) const;
	bool IsZoneHiddenFromPlayer(const AEnemySpawnZone* Zone, int32 SampleCount, const FVector& ViewLoc, const FVector& ViewDir, const FCollisionQueryParams& QueryParams, UNavigationSystemV1* NavSys) const;
	float ScoreZone(const AEnemySpawnZone* Zone, const FVector& PlayerLoc, const FVector& CompanionLoc, bool bHasCompanion, float DistMin, float DistMax) const;
	const ACompanionCharacter* FindCompanion() const;
	void SpawnSquadAtZone(const FSquadComposition& Composition, AEnemySpawnZone* Zone, TArray<AEnemyCharacter*>& OutSpawned);
	AEnemyCharacter* SpawnEntryAtZone(UWorld* World, TSubclassOf<AEnemyCharacter> EnemyClass, AEnemySpawnZone* Zone, int32 Index);
	void SeedSquadWithFight(UEnemySquad* Squad) const;

	TWeakObjectPtr<UEnemySquadSubsystem> CachedSquadSubsystem;

	bool bLoggedNoComposition = false;
	bool bLoggedNoZone = false;

	// ---------- v2: kill tracking ----------

	UFUNCTION()
	void HandleEnemyKilled(AEnemyCharacter* DeadEnemy, FVector Location, bool bWasOfficer);

	// ---------- v2: wave lifecycle ----------

	void CompleteWave();
	void ClearWaveState();
	void PruneStaleWaveMembers();
	void AccrueWaveBlockedTime();

	/** Auto-engage waves only: re-seed Combat on any live wave member that decayed to Unaware
	 *  (gave up and returned to a guard post) — a passive holdout stalls the kill-all wave. */
	void ReassertWaveMemberEngagement();

	UPROPERTY()
	FDirectorWaveRequest ActiveWaveRequest;

	FDirectorWaveProgress WaveProgress;

	TSet<TWeakObjectPtr<AEnemyCharacter>> WaveMembers;

	float WaveBlockedTime = 0.f;
	bool bWaveBlockedBroadcast = false;

	// ---------- v2: punishment profile ----------

	TWeakObjectPtr<AActor> PunishmentSource;

	UPROPERTY(Transient)
	TObjectPtr<UDirectorConfigData> PunishmentConfig;

	EMissionPhase PunishmentPhase = EMissionPhase::Infiltration;

	// ---------- timers ----------

	static constexpr float DirectorTickInterval = 1.f;

	FTimerHandle DirectorTimerHandle;
};
