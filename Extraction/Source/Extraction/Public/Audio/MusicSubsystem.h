// UMusicSubsystem — adaptive music state machine. Polls the enemy director's live signals
// (searching/combat counts, sawtooth state, mission phase, wave events) and crossfades
// between the UMusicBankData palettes: sparse explore ambience with long silences, a
// suspicion pulse while enemies hunt, full combat/wave tracks with a Peak percussion
// overlay, and a relief wind-down that ducks the fight bed rather than cutting it — so a
// lull can re-ignite seamlessly and only a settled fight hands over to stealth.
// Empty bank = silently idle.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MusicSubsystem.generated.h"

class UAudioComponent;
class UHealthComponent;
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

	/** Stamp a real combat event (a shot fired, a round landing, a near-miss). The world-audio hub
	 *  calls this — the director's combat count can't tell a live fight from one that stopped, since
	 *  an enemy stays in combat awareness for its archetype's LostContactGrace after losing the
	 *  player. This is a gate on that count, never a trigger on its own. */
	void NotifyCombatActivity();

private:
	void Poll();
	EMusicState ComputeDesiredState(double Now) const;

	/** Stamps combat activity when the player's damage timestamp advances. Polled rather than hooked
	 *  because the melee and explosion damage paths never reach the world-audio hub, so hooking the
	 *  audio cues alone would leave a fight with no shooting reading as quiet. */
	void PollPlayerDamage(const UWorld& World, double Now);

	/** Whether combat activity landed inside the bank's window — what separates a fight in progress
	 *  from enemies still nominally "in combat" minutes after the shooting stopped. */
	bool HasRecentCombatActivity(double Now) const;
	void TransitionTo(EMusicState NewState, double Now);
	void UpdateTrackForState();
	void UpdateExplore(double Now);
	void UpdatePeakOverlay();

	/** Entering Relief: fade a live fight bed down to the duck level and leave it playing, so the
	 *  fight can resume seamlessly or hand the bed over to stealth. Stops outright otherwise. */
	void DuckOrStopForRelief(EMusicState OldState);

	/** Ride the fight bed back up to level instead of restarting it — whether it's still ducked on
	 *  CurrentTrack or already handed off and fading out under its replacement. False when there's no
	 *  live fight bed, or the phase flipped and the bed is now the wrong palette. */
	bool TryResumeDuckedTrack(EMusicState ForState);

	/** Crossfade length for leaving a fight bed behind: the long relief handoff whenever one is still
	 *  live (ducked or fading out under its replacement), the normal crossfade otherwise. */
	float FadeSecondsLeavingFightBed() const;

	/** Relief -> Explore: slow-crossfade the ducked bed straight into a stealth bed. False when
	 *  there's no ducked bed or the phase has no explore tracks — caller stops as before. */
	bool TryReliefHandoff();

	/** Random valid explore bed for the current mission phase; null if the phase has none. */
	USoundBase* PickExploreTrack() const;

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
	void DebugLine(const FString& Line) const;

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

	/** Last poll that saw a live combatant AND recent combat activity. The director's own stamp marks
	 *  combat ENTRY only, so a sustained fight with no fresh joiners reads stale long before it ends —
	 *  this is what makes CombatHoldSeconds measure from the moment the fight actually went quiet. */
	double LastCombatSignalTime = -1e9;

	/** World time of the last NotifyCombatActivity. */
	double LastCombatActivityTime = -1e9;

	/** Local player's health component, re-resolved whenever it goes stale (respawn, possession). */
	TWeakObjectPtr<const UHealthComponent> PlayerHealth;

	/** Last GetLastDamageWorldTime already turned into an activity stamp — an advance past this is a
	 *  fresh hit. Shares the component's never-damaged sentinel so a fresh pawn can't stamp. */
	float LastSeenPlayerDamageTime = -1e9f;

	/** World time the queued all-clear resolve fires; 0 = none. Held back until the fight bed has
	 *  actually ducked away, and cancelled outright if the fight re-ignites first. */
	double PendingAllClearTime = 0.0;

	bool bWaveActive = false;
	bool bOverlayOn = false;

	/** CurrentTrack is a fight bed still playing, faded down for relief — not yet handed off to
	 *  stealth, restored by a re-ignited fight, or replaced. */
	bool bReliefDucked = false;

	/** OutgoingTrack is that same fight bed after the handoff, still audible under the bed replacing
	 *  it. A fight re-igniting inside that window reclaims it rather than restarting from bar 1. */
	bool bOutgoingFightBed = false;

	static constexpr float PollInterval = 0.5f;

	FTimerHandle PollTimerHandle;
};
