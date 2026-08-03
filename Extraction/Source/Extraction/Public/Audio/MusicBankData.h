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

	/** Continuous bed while the player is inside a stealth-discipline zone. Must be
	 *  loop-authored. Falls back to SuspicionTrack when unset. */
	UPROPERTY(EditDefaultsOnly, Category = "Tracks")
	TObjectPtr<USoundBase> StealthTrack;

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

	/** Fade-in when a track starts from silence (level entry, explore bed) rather than crossfading
	 *  from another track — kept short so the stealth bed arrives immediately instead of ramping up
	 *  under the full crossfade. 0 = instant. */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float EntryFadeSeconds = 0.25f;

	/** Fade-out used when music stops entirely (an explore gap, or a wind-down with no live
	 *  fight bed left to duck). */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.1"))
	float StopFadeSeconds = 4.f;

	/** Combat music holds this long past the last moment any enemy was still in combat — the
	 *  reference is the live count, not the fight's opening shot, so a long engagement's back half
	 *  isn't treated as already stale. This hold covers short breaks in contact; the relief duck
	 *  handles the longer lulls, riding the same bed back up if the fight re-ignites. */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float CombatHoldSeconds = 10.f;

	/** How recently something must actually have happened — a shot, a round landing, a near-miss —
	 *  for a live combat enemy count to read as a fight. Enemies hold combat awareness for their
	 *  archetype's LostContactGrace (45s by default) after losing the player, so the count alone
	 *  pins the music in Combat long after the shooting stops and CombatHoldSeconds never gets to
	 *  start counting. Sized to ride out a cautious flank where nobody has a shot; the hold layers
	 *  on top of it, so the audible tail after the last round is this plus CombatHoldSeconds.
	 *  0 = gate off, i.e. a live count alone reads as a fight again (the pre-gate behaviour). */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float CombatActivityWindowSeconds = 8.f;

	/** Quiet wind-down after a fight before explore ambience may resume. */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float ReliefSeconds = 20.f;

	/** Fraction of the fight bed's playing level it rides down to during relief — it keeps playing so
	 *  a re-ignited fight resumes seamlessly instead of restarting the track. MusicVolume is applied
	 *  both at spawn and as the fader target, so the audible result is MusicVolume^2 * this.
	 *  Must stay above zero: a zero fade target is read as a fade-out and kills the bed outright. */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float ReliefDuckVolume = 0.3f;

	/** How long the duck-down takes. Slow enough to read as the fight receding, not a cut. */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.1"))
	float ReliefDuckFadeSeconds = 6.f;

	/** Crossfade from the ducked fight bed into the stealth bed when relief ends — deliberately
	 *  long so the palette change is felt rather than heard. */
	UPROPERTY(EditDefaultsOnly, Category = "Tuning", meta = (ClampMin = "0.1"))
	float ReliefHandoffFadeSeconds = 10.f;

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
