// UFootstepNoiseComponent — distance-based footsteps: emits AI-hearing noise events (player) and
// plays surface-aware footstep/land/sprint-rattle audio (player + NPCs) as the owner moves.
// Audio cadence is independent of the AI-noise cadence so the two tune separately.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/EngineTypes.h"
#include "FootstepNoiseComponent.generated.h"

class ACharacter;
class UAudioComponent;

UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UFootstepNoiseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFootstepNoiseComponent();

	/** NPCs disable AI-noise emission at construction — their steps are audible flavour, not
	 *  stimuli (enemies must not hear each other; companion stealth is handled elsewhere). */
	void SetEmitAINoise(bool bEmit) { bEmitAINoise = bEmit; }

	/** Owner-ctor volume override (the companion trails the player and reads doubled at parity). */
	void SetAudioVolume(float Volume) { AudioVolume = Volume; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Horizontal distance (cm) travelled between noise emissions. */
	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "50.0"))
	float StepDistance = 350.f;

	/** Owner speed (cm/s) at or above which steps use the sprint loudness/range. Must sit below the
	 *  kit's hold-sprint speed (550 since the sprint/slide rework — the old 750 left the sprint tier
	 *  unreachable) and above the jog base speed (410). Slides land in this tier too: loud is right. */
	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "50.0"))
	float SprintSpeedThreshold = 480.f;

	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float SprintLoudness = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float SprintRange = 1800.f;

	/** Owner speed (cm/s) below which steps use the slow-walk (held-Ctrl) loudness/range instead
	 *  of the jog tier. Sits between a standing idle jitter and the player's held-walk cap so a
	 *  genuine walk never reads as a jog. */
	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float WalkSpeedThreshold = 320.f;

	// Default jog pair — ordinary running is now audible at a meaningful range: a couple of steps
	// near an enemy builds Suspicious (hearing is interest-only; see UEnemyAwarenessComponent's cap).
	// NB the effective radius is HearingRange(2000 on all archetype DAs) x Loudness, capped by
	// Range — 0.5 x 2000 = 1000, so the pair below actually delivers ~10m. The first-pass 0.35
	// only reached 7m and read as "walking is silent" in the 2026-07-11 playtest.
	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float WalkLoudness = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float WalkRange = 1000.f;

	/** Held-Ctrl slow walk — near-silent, the reward for holding the walk key. */
	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float SlowWalkLoudness = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float SlowWalkRange = 250.f;

	/** Used while crouched or prone — audible only at close range (design: stealth movement). */
	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float QuietLoudness = 0.12f;

	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float QuietRange = 150.f;

	// --- Audio ---

	/** Horizontal distance (cm) between audible footsteps. Faster movement covers more distance
	 *  per second, so the step rate scales with speed on its own. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "50.0"))
	float AudioStepDistance = 170.f;

	/** Master volume for this owner's footstep/land audio (NPC cues also attenuate by distance). */
	UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AudioVolume = 1.f;

	/** Walk-cue volume when crouched/prone and the surface has no dedicated crouch cue. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrouchVolumeMult = 0.45f;

	/** Audible-step distance (cm) while crouched/prone — the crouch stride is shorter than the
	 *  standing one, so the shared AudioStepDistance leaves visual steps silent. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "50.0"))
	float QuietAudioStepDistance = 110.f;

	/** Gear-rattle loop volume (x AudioVolume) while crouch/prone-moving — big kit still shifts
	 *  when moving low, just quieter than the sprint rattle. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrouchRattleVolumeMult = 0.3f;

	/** Min speed (cm/s) that counts as "moving" for the crouch gear rattle. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "0.0"))
	float CrouchRattleMinSpeed = 60.f;

	// --- Anim-synced steps (foot-plant tracking) ---

	/** Foot bones tracked for plant detection. Steps fire the moment a foot lands, in sync with the
	 *  actual animation. Missing bones = the distance backstop below carries the cadence alone. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio|FootSync")
	FName LeftFootBone = TEXT("foot_l");

	UPROPERTY(EditDefaultsOnly, Category = "Audio|FootSync")
	FName RightFootBone = TEXT("foot_r");

	/** A foot moving slower than this (cm/s, world space) counts as planted — the stance foot is
	 *  near-stationary while the body passes over it. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio|FootSync", meta = (ClampMin = "0.0"))
	float FootPlantSpeedMax = 75.f;

	/** A plant only makes a step sound if the foot's peak speed since its last plant exceeded this —
	 *  filters IK/idle jitter so standing still never clicks. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio|FootSync", meta = (ClampMin = "0.0"))
	float FootSwingSpeedMin = 150.f;

	/** Min seconds between steps of the SAME foot (half a very fast cadence). */
	UPROPERTY(EditDefaultsOnly, Category = "Audio|FootSync", meta = (ClampMin = "0.0"))
	float FootRetriggerSeconds = 0.2f;

	/** While foot tracking is live, the distance accumulator only backstops (x this on the audible
	 *  step distance) so movement can never go silent if plants stop being detected. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio|FootSync", meta = (ClampMin = "1.0"))
	float BackstopDistanceMult = 1.5f;

	/** Walk-cue volume in the held-Ctrl slow-walk tier. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SlowWalkVolumeMult = 0.6f;

	/** Volume of the gear-rattle accent layered over the surface land cue. */
	UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LandRattleVolume = 0.6f;

	/** Min downward speed (cm/s) at touchdown for land audio. Running over stairs/bumps registers
	 *  engine landings constantly — those micro-drops must not thump ("odd double step"). */
	UPROPERTY(EditDefaultsOnly, Category = "Audio", meta = (ClampMin = "0.0"))
	float LandAudioMinFallSpeed = 380.f;

private:
	enum class EStepTier : uint8 { Quiet, SlowWalk, Walk, Sprint };
	enum class ERattleState : uint8 { None, Crouch, Sprint };

	void TickNoise();
	EStepTier PickTier() const;
	void EmitAINoise(const FVector& Location, EStepTier Tier) const;
	void PlayFootstepAudio(EStepTier Tier) const;
	void UpdateGearRattle(ERattleState NewState);

	/** Foot-plant detection: returns true if either foot landed this tick (step audio played). */
	bool TickFootPlants(const ACharacter& OwnerChar, EStepTier Tier);
	EPhysicalSurface TraceGroundSurface() const;

	UFUNCTION()
	void OnOwnerLanded(const FHitResult& Hit);

	/** Jump push-off foley. ACharacter::OnJumped proved unreliable with the kit's jump flow, so
	 *  the trigger is the ground->falling movement-mode change with upward velocity. */
	UFUNCTION()
	void OnOwnerMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode);

	/** 0.05s, not the old 0.2 — coarse ticks quantized the audio cadence onto tick boundaries and
	 *  produced a galloping double-step (two steps on consecutive ticks, then a gap). AI-noise
	 *  emission is distance-gated so the finer tick does not change its rate. */
	static constexpr float UpdateInterval = 0.05f;

	float AccumulatedDistance = 0.f;
	float AudioAccumulatedDistance = 0.f;
	FVector LastLocation = FVector::ZeroVector;

	// Per-foot plant state (0 = left, 1 = right)
	FVector LastFootLocation[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float FootPeakSwingSpeed[2] = { 0.f, 0.f };
	float LastFootStepTime[2] = { -1e9f, -1e9f };
	bool bFootPlanted[2] = { true, true };
	bool bFootLocationsValid = false;
	/** World time of the last foot-plant step — recent = tracking live, distance path backstops only. */
	float LastFootPlantTime = -1e9f;

	bool bEmitAINoise = true;
	ERattleState RattleState = ERattleState::None;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> SprintRattleComp;

	FTimerHandle NoiseTimerHandle;
};
