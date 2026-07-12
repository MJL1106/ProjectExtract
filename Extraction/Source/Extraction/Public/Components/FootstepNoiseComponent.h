// UFootstepNoiseComponent — emits AI-hearing noise events as the owner moves, scaled by speed and stance.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FootstepNoiseComponent.generated.h"

UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UFootstepNoiseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFootstepNoiseComponent();

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

private:
	void TickNoise();
	void PickNoiseProfile(float& OutLoudness, float& OutRange, FName& OutTag) const;

	static constexpr float UpdateInterval = 0.2f;

	float AccumulatedDistance = 0.f;
	FVector LastLocation = FVector::ZeroVector;

	FTimerHandle NoiseTimerHandle;
};
