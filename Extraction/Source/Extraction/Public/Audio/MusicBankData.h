// UMusicBankData — designer-populated bank for the adaptive music system. One track set per
// mission phase (Extraction doubles as the finale palette) plus shared stingers/overlay and
// all state-machine tunables. UMusicSubsystem reads it; C++ stays asset-path-free.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Enemy/EnemyTypes.h"
#include "MusicBankData.generated.h"

class USoundBase;

USTRUCT(BlueprintType)
struct FMusicPhaseSet
{
	GENERATED_BODY()

	/** Sparse ambient beds for quiet play; one is picked at random after each silence gap.
	 *  Author these as finite (non-looping) pieces — silence between them is the design. */
	UPROPERTY(EditDefaultsOnly, Category = "Tracks")
	TArray<TObjectPtr<USoundBase>> ExploreTracks;

	/** Low pulse while enemies are actively searching. Must be loop-authored. */
	UPROPERTY(EditDefaultsOnly, Category = "Tracks")
	TObjectPtr<USoundBase> SuspicionTrack;

	/** Full combat track. Must be loop-authored. */
	UPROPERTY(EditDefaultsOnly, Category = "Tracks")
	TObjectPtr<USoundBase> CombatTrack;

	/** Defend-wave track; falls back to CombatTrack when unset. Must be loop-authored. */
	UPROPERTY(EditDefaultsOnly, Category = "Tracks")
	TObjectPtr<USoundBase> WaveTrack;
};

UCLASS(BlueprintType)
class EXTRACTION_API UMusicBankData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ---------- per-phase palettes ----------

	UPROPERTY(EditDefaultsOnly, Category = "Palettes")
	FMusicPhaseSet Infiltration;

	UPROPERTY(EditDefaultsOnly, Category = "Palettes")
	FMusicPhaseSet Objective;

	/** The finale palette — everything gets urgent once the mission phase flips to Extraction. */
	UPROPERTY(EditDefaultsOnly, Category = "Palettes")
	FMusicPhaseSet Extraction;

	const FMusicPhaseSet& SetForPhase(EMissionPhase Phase) const
	{
		switch (Phase)
		{
		case EMissionPhase::Objective:  return Objective;
		case EMissionPhase::Extraction: return Extraction;
		default:                        return Infiltration;
		}
	}

	// ---------- shared layers / stingers ----------

	/** Percussion/pulse loop faded on top of the combat or wave track while the director
	 *  sits at Peak — big fights thicken without per-phase intensity variants. */
	UPROPERTY(EditDefaultsOnly, Category = "Shared")
	TObjectPtr<USoundBase> PeakOverlayLoop;

	/** One-shot hit covering the crossfade seam when combat music kicks in. */
	UPROPERTY(EditDefaultsOnly, Category = "Shared")
	TObjectPtr<USoundBase> StingerCombatIn;

	/** One-shot resolve when a fight winds down clean. */
	UPROPERTY(EditDefaultsOnly, Category = "Shared")
	TObjectPtr<USoundBase> StingerAllClear;

	/** One-shot on defend-wave completion. */
	UPROPERTY(EditDefaultsOnly, Category = "Shared")
	TObjectPtr<USoundBase> StingerWaveVictory;

	// ---------- tunables ----------

	/** Final volume multiplier for all tracks (mix balance lives on the SC_Music class; this
	 *  is a convenience trim). */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MusicVolume = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float StingerVolume = 0.9f;

	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.1"))
	float CrossfadeSeconds = 2.f;

	/** Fade-out used when music stops entirely (entering Relief or an explore gap). */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.1"))
	float StopFadeSeconds = 4.f;

	/** Combat music holds this long past the last combat signal before winding down. */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float CombatHoldSeconds = 8.f;

	/** Quiet wind-down after a fight before explore ambience may resume. */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float ReliefSeconds = 15.f;

	/** Suspicion music holds this long after the last searcher gives up (anti-flap). */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float SuspicionHoldSeconds = 6.f;

	/** Randomized silence gap between explore ambient tracks. */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float ExploreSilenceMinSeconds = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float ExploreSilenceMaxSeconds = 180.f;

	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.1"))
	float OverlayFadeSeconds = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float OverlayVolume = 0.8f;
};
