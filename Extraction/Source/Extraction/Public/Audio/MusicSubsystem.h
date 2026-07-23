// UMusicSubsystem — adaptive music state machine. Polls the enemy director's live signals
// (searching/combat counts, sawtooth state, mission phase, wave events) and crossfades
// between the UMusicBankData palettes: sparse explore ambience with long silences, a
// suspicion pulse while enemies hunt, full combat/wave tracks with a Peak percussion
// overlay, and a quiet relief wind-down after fights. Empty bank = silently idle.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MusicSubsystem.generated.h"

class UAudioComponent;
class UMusicBankData;
class USoundBase;

UENUM(BlueprintType)
enum class EMusicState : uint8
{
	Explore,
	Suspicion,
	Combat,
	Wave,
	Relief,
};

UCLASS()
class EXTRACTION_API UMusicSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Audio|Music")
	EMusicState GetMusicState() const { return State; }

private:
	void Poll();
	EMusicState ComputeDesiredState(double Now) const;
	void TransitionTo(EMusicState NewState, double Now);
	void UpdateTrackForState();
	void UpdateExplore(double Now);
	void UpdatePeakOverlay();

	/** Crossfade to Sound (null = fade current out to silence). No-op if already playing it. */
	void StartTrack(USoundBase* Sound, float FadeSeconds);
	void StopTrack(float FadeSeconds);
	void PlayStinger(USoundBase* Sound) const;

	/** Looping track the bank prescribes for a state under the current mission phase
	 *  (null for Explore/Relief — those are silence-driven). */
	USoundBase* TrackForState(EMusicState ForState) const;

	UFUNCTION()
	void HandleWaveStarted(FName WaveId);

	UFUNCTION()
	void HandleWaveCompleted(FName WaveId);

	void DebugState(const TCHAR* What) const;

	UPROPERTY(Transient)
	TObjectPtr<const UMusicBankData> Bank;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> CurrentTrack;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> OutgoingTrack;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> OverlayComponent;

	EMusicState State = EMusicState::Explore;
	double StateEnterTime = 0.0;

	/** Explore scheduling: 0 = no gap scheduled yet; else world time the next ambient bed starts. */
	double NextExploreTime = 0.0;

	double ReliefUntilTime = 0.0;
	double LastSuspicionSignalTime = -1e9;

	bool bWaveActive = false;
	bool bOverlayOn = false;

	static constexpr float PollInterval = 0.5f;

	/** First ambient bed lands almost immediately at level start; the bank's silence
	 *  cadence governs every gap after it. */
	static constexpr float OpeningGapMinSeconds = 1.f;
	static constexpr float OpeningGapMaxSeconds = 3.f;

	FTimerHandle PollTimerHandle;
};
