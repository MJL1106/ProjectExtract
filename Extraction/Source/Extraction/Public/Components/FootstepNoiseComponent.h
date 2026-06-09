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

	/** Owner speed (cm/s) at or above which steps use the sprint loudness/range. */
	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "50.0"))
	float SprintSpeedThreshold = 500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float SprintLoudness = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float SprintRange = 1200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float WalkLoudness = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float WalkRange = 400.f;

	/** Used while crouched or prone — audible only at close range (design: stealth movement). */
	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float QuietLoudness = 0.12f;

	UPROPERTY(EditDefaultsOnly, Category = "Noise", meta = (ClampMin = "0.0"))
	float QuietRange = 150.f;

private:
	void TickNoise();
	void PickNoiseProfile(float& OutLoudness, float& OutRange) const;

	static constexpr float UpdateInterval = 0.2f;

	float AccumulatedDistance = 0.f;
	FVector LastLocation = FVector::ZeroVector;

	FTimerHandle NoiseTimerHandle;
};
