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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Squad", meta = (ToolTip = "Enemy Blueprint/C++ class to spawn for this entry. Invalid entries are ignored by the director."))
	TSubclassOf<AEnemyCharacter> EnemyClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Squad", meta = (ClampMin = "1", ToolTip = "How many enemies of EnemyClass this composition spawns. Larger counts increase squad size and consume more MaxAlive headroom."))
	int32 Count = 1;
};

USTRUCT(BlueprintType)
struct FSquadComposition
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Squad", meta = (ToolTip = "Designer label for this composition. Used for readability and logs only; it does not affect selection."))
	FName Name;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Squad", meta = (ClampMin = "0.01", ToolTip = "Relative chance for this composition after alive-cap filtering. 2.0 is twice as likely as 1.0; lower values make it less likely."))
	float Weight = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Squad", meta = (ToolTip = "Enemy class/count pairs that make up the squad. If the total count will not fit inside the phase MaxAlive headroom, the whole composition is skipped."))
	TArray<FSquadCompositionEntry> Entries;
};

// --- Per-phase config ---

USTRUCT(BlueprintType)
struct FMissionPhaseConfig
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phase", meta = (ToolTip = "Tension ceiling for this phase. If current tension is at or above this value, spawning pauses. Lower values create more breathing room; higher values allow more pressure."))
	float IntensityCeiling = 60.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "1.0", ToolTip = "Minimum seconds between director spawn attempts in this phase while the director is Loud and in Build. Lower values create more frequent waves; higher values slow the response."))
	float SpawnCadenceSeconds = 25.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "1", ToolTip = "Maximum alive enemies allowed inside the active director scope for this phase. Placed enemies count too; if the alive count is at or above this value, no squad spawns."))
	int32 MaxAlive = 12;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phase", meta = (ToolTip = "Treat this phase as a sustained assault: spawning paces on cadence and MaxAlive only, ignoring the tension sawtooth (no Peak/Relief pauses), and squads arrive already engaged. Meant for defend/siege phases where the player must hold a position under continuous pressure, not for exploration where the sawtooth creates readable lulls."))
	bool bSustainedPressure = false;

	/** Squads fielded from different zones in a single ambient spawn beat. 1 = one direction
	 *  at a time (today's behaviour). Higher values make reinforcements arrive from multiple
	 *  directions simultaneously. Scripted waves are unaffected (always one squad per beat).
	 *  MaxAlive is respected across the whole beat, not per squad. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "1", ClampMax = "4",
		ToolTip = "Squads fielded from different zones in one ambient spawn beat. 1 = single direction; 2+ = multi-directional. Waves unaffected. MaxAlive shared across the beat."))
	int32 ConcurrentSpawnZones = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phase", meta = (ToolTip = "Weighted squad options for this phase. Empty means this phase cannot spawn. Compositions too large for the current MaxAlive headroom are skipped."))
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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension", meta = (ToolTip = "Tension removed per second while the director is active. Higher values make pressure fall faster; lower values keep the director in higher-pressure states longer."))
	float TensionDecayPerSecond = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension", meta = (ToolTip = "Tension gained per point of player health lost. Higher values make taking damage push the director toward Peak/Relief sooner, reducing extra spawns after the player is hurt."))
	float TensionPerPlayerHealthLost = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension", meta = (ToolTip = "Tension gained per enemy killed. Higher values make successful clearing push the director toward Peak/Relief sooner; lower values allow reinforcements to continue longer after kills."))
	float TensionPerKill = 8.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension", meta = (ToolTip = "Tension gained per combat-engaged enemy near the player each second. Higher values make active firefights pause spawning sooner; lower values allow more enemies to layer in."))
	float TensionPerEngagedEnemy = 3.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension", meta = (ToolTip = "Build changes to Peak when tension reaches this value. Lower values make the director stop adding spawns sooner; higher values allow more pressure before the pause."))
	float PeakTensionThreshold = 75.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension", meta = (ToolTip = "Peak changes to Relief once tension drops below this value. Must be lower than PeakTensionThreshold. Lower values keep Peak longer; higher values start relief sooner."))
	float ReliefEntryThreshold = 40.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tension", meta = (ToolTip = "Seconds spent in Relief before returning to Build. Higher values create a longer quiet window; lower values make waves resume sooner."))
	float ReliefDuration = 25.f;

	// --- Spawning ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spawning", meta = (ToolTip = "Minimum 3D distance from the player to an eligible spawn zone on the SAME storey. Higher values prevent close pop-ins; too high can starve small interiors."))
	float SpawnDistanceMin = 1500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spawning", meta = (ClampMin = "1.0", ToolTip = "Vertical gap (cm) above which a zone counts as a different storey from the player. Zones on another storey use SpawnDistanceMinDifferentStorey instead of SpawnDistanceMin, since the ceiling blocks the pop-in that the same-floor gate prevents."))
	float StoreySeparationHeight = 250.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spawning", meta = (ClampMin = "1.0", ToolTip = "Minimum horizontal distance from the player to an eligible spawn zone on a DIFFERENT storey. Deliberately smaller than SpawnDistanceMin because the ceiling blocks the pop-in that the same-floor gate prevents. Must not exceed SpawnDistanceMin (a larger value would make other-storey zones harder to use than same-floor ones, defeating the purpose)."))
	float SpawnDistanceMinDifferentStorey = 350.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spawning", meta = (ToolTip = "Maximum 3D distance from the player to an eligible spawn zone. Lower values keep reinforcements local; too low can starve large or multi-floor layouts."))
	float SpawnDistanceMax = 4500.f;

	// --- Punishment squad tracking ---

	/** While the director alarm is raised, periodically refresh punishment-spawned squads'
	 *  last-known position so they push toward the player's current area instead of freezing
	 *  on a stale point. Disable to let squads rely on their own perception only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spawning", meta = (ToolTip = "Refresh punishment-spawned squads' search target while the alarm is raised so they track the player's current area."))
	bool bRefreshSquadSearchTarget = true;

	/** Seconds between search-target refreshes for punishment squads while the alarm is up.
	 *  Must be greater than the squad's internal SightingRelayInterval (1s) to avoid fighting
	 *  its rate limiter. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spawning", meta = (ClampMin = "1.5", ToolTip = "Interval between search-target refreshes for punishment squads."))
	float SquadSearchRefreshInterval = 3.f;

	// --- Per-phase configs ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phases", meta = (ToolTip = "Phase config used for the early or stealth portion of the mission. Usually slower cadence, lower cap, and lighter squads."))
	FMissionPhaseConfig InfiltrationConfig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phases", meta = (ToolTip = "Phase config used for the main objective fights. Usually moderate cadence, moderate cap, and mixed squads."))
	FMissionPhaseConfig ObjectiveConfig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Phases", meta = (ToolTip = "Phase config used after the objective or during escape. Usually fastest cadence, highest cap, and hardest squads."))
	FMissionPhaseConfig ExtractionConfig;

	/** Returns the config block for the given mission phase. */
	const FMissionPhaseConfig& GetPhaseConfig(EMissionPhase Phase) const;

	/** Logs and returns false if the config has values that would break the director. */
	bool Validate(FString& OutError) const;
};
