// UGameAudioSubsystem — world-audio playback hub. Loads the USurfaceAudioBank once per world and
// serves surface-aware impact/footstep lookups plus the DBNO heartbeat/duck state. Pure playback:
// callers decide WHEN a sound happens, this decides WHICH asset and plays it.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Chaos/ChaosEngineInterface.h"
#include "GameAudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class USurfaceAudioBank;
struct FSurfaceSoundSet;

UCLASS()
class EXTRACTION_API UGameAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	const USurfaceAudioBank* GetBank() const { return Bank; }

	/** Surface set for a phys-material surface type; falls back to the bank's DefaultSurface.
	 *  Null only when no bank is configured. */
	const FSurfaceSoundSet* GetSurfaceSet(EPhysicalSurface Surface) const;

	/** Surface type off a hit's phys material (traces must set bReturnPhysicalMaterial). */
	static EPhysicalSurface SurfaceFromHit(const FHitResult& Hit);

	/** Surface bullet-impact cue at the hit point. */
	void PlayWorldImpact(const FHitResult& Hit);

	/** Bullet-into-body crack + meat-thump layer, once per victim per shot. bAsLocal2D = the local
	 *  player is the shooter: play as 2D hit-confirm so feedback reads at any range. bHeadshot
	 *  swaps the crack for the bank's HeadshotImpact when one is set. */
	void PlayFleshImpact(const FVector& Location, bool bAsLocal2D = false, bool bHeadshot = false);

	/** AI fire report with the bank's gunfire attenuation forced on (fire cues are 2D-authored). */
	void PlayAIFireReport(USoundBase* Sound, const FVector& Location) const;

	/** Non-positional one-shot (hit confirms, UI-adjacent feedback). */
	void Play2D(USoundBase* Sound, float Volume = 1.f) const;

	/** Owner-aware foley: 2D for the local player's own body (a positional one-shot at the capsule
	 *  sits off-centre from the camera ear and pans oddly), positional at the owner for NPCs. */
	void PlayFoleyFor(const AActor* Owner, USoundBase* Sound, float Volume = 1.f) const;

	/** Near-miss crack at the shot's closest approach to the player's head. */
	void PlayFlyby(const FVector& Location);

	/** Brass tinkle, internally delayed by the bank's ShellDelay range. */
	void PlayShellDrop(const FVector& Location);

	/** Grenade-throw foley (2D, player-only), internally delayed to the bank's tuned release frame. */
	void PlayThrowFoley();

	/** One-shot at a world location (bank-sourced cues carry their own attenuation/concurrency). */
	void PlayAt(USoundBase* Sound, const FVector& Location, float Volume = 1.f) const;

	/** Heartbeat loop + DBNO duck mix while the local player is downed. */
	void StartDBNOAudio();
	void StopDBNOAudio();

	/** audio.WorldDebug 1 — every world-audio play prints on screen + log. */
	static bool IsAudioDebugEnabled();

	/** On-screen + log line for a play event (no-op unless audio.WorldDebug). */
	static void DebugPlay(const TCHAR* Tag, const USoundBase* Sound, const FString& Context = FString());

private:
	UPROPERTY(Transient)
	TObjectPtr<const USurfaceAudioBank> Bank;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> HeartbeatComponent;

	bool bDBNOMixPushed = false;

	/** World time of the last accepted PlayThrowFoley — debounces throwable-press spam. */
	double LastThrowFoleyTime = -1000.0;
};
