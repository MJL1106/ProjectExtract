// Minimal cover-pose state component — drives animation layer reads. No tick, no replication.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/Cover/CoverPoseTypes.h"
#include "CoverPoseComponent.generated.h"

UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UCoverPoseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCoverPoseComponent();

	// --- Read-only state for animation / gameplay queries ---

	UPROPERTY(BlueprintReadOnly, Category = "Cover|Pose")
	bool bInCover = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cover|Pose")
	ECoverHeight CoverHeight = ECoverHeight::Stand;

	UPROPERTY(BlueprintReadOnly, Category = "Cover|Pose")
	ECoverLean LeanDirection = ECoverLean::None;

	UPROPERTY(BlueprintReadOnly, Category = "Cover|Pose")
	bool bBlindFiring = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cover|Pose")
	bool bPeeking = false;

	// --- Setters (BlueprintCallable so BTs and gameplay code can drive state) ---

	UFUNCTION(BlueprintCallable, Category = "Cover|Pose")
	void SetInCover(bool bNewInCover, ECoverHeight NewHeight);

	UFUNCTION(BlueprintCallable, Category = "Cover|Pose")
	void SetLean(ECoverLean NewLean);

	UFUNCTION(BlueprintCallable, Category = "Cover|Pose")
	void SetBlindFiring(bool bNewBlindFiring);

	UFUNCTION(BlueprintCallable, Category = "Cover|Pose")
	void SetPeeking(bool bNewPeeking);

	UFUNCTION(BlueprintCallable, Category = "Cover|Pose")
	void ResetCoverPose();
};
