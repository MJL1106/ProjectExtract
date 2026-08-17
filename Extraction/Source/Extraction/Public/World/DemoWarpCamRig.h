// ADemoWarpCamRig — demo filming helper: one roaming camera cycled through placed warp points.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoWarpCamRig.generated.h"

class ACameraActor;
class AEnemyCharacter;
class UInputAction;

/**
 * Placed once in a demo level. Next/Prev input warps RoamingCamera to the matching entry in
 * WarpPoints (location + rotation) and makes it the view target. The points are plain placed
 * actors (TargetPoints) — duplicate one, aim it, drag it into the array; array order is the
 * cycle order. Everything is assigned per-instance in the details panel, so which camera roams
 * and which keys drive it are level data, not code.
 */
UCLASS()
class EXTRACTION_API ADemoWarpCamRig : public AActor
{
	GENERATED_BODY()

public:
	ADemoWarpCamRig();

protected:
	virtual void BeginPlay() override;

	/** The single camera the rig moves. Any CameraActor works (CineCameraActor included). */
	UPROPERTY(EditAnywhere, Category = "Demo|Warp")
	TObjectPtr<ACameraActor> RoamingCamera;

	/** Vantage points in cycle order — each point's rotation is the camera facing. */
	UPROPERTY(EditAnywhere, Category = "Demo|Warp")
	TArray<TObjectPtr<AActor>> WarpPoints;

	UPROPERTY(EditAnywhere, Category = "Demo|Warp")
	TObjectPtr<UInputAction> NextAction;

	UPROPERTY(EditAnywhere, Category = "Demo|Warp")
	TObjectPtr<UInputAction> PrevAction;

	/** View blend seconds on warp. 0 = hard cut (matches the numbered demo cams). */
	UPROPERTY(EditAnywhere, Category = "Demo|Warp", meta = (ClampMin = "0.0"))
	float BlendTime = 0.f;

	// --- Showcase fire/reload: drive the enemy nearest the roaming camera ---

	/** Toggles the showcased enemy firing down its own facing (real fire path, AI-noise-muted). */
	UPROPERTY(EditAnywhere, Category = "Demo|Showcase")
	TObjectPtr<UInputAction> FireToggleAction;

	/** Triggers a reload on the showcased enemy (stops an active showcase burst first). */
	UPROPERTY(EditAnywhere, Category = "Demo|Showcase")
	TObjectPtr<UInputAction> ReloadAction;

	/** Max distance (cm) from the roaming camera to the enemy the showcase keys drive. */
	UPROPERTY(EditAnywhere, Category = "Demo|Showcase", meta = (ClampMin = "100.0"))
	float ShowcaseRadius = 1500.f;

	/** Seconds the enemy holds the aim pose before the sustained fire starts. */
	UPROPERTY(EditAnywhere, Category = "Demo|Showcase", meta = (ClampMin = "0.0"))
	float ShowcaseAimDelay = 1.0f;

private:
	void WarpNext();
	void WarpPrev();
	void WarpToIndex(int32 Index);

	void ToggleShowcaseFire();
	void ShowcaseReload();
	void StopShowcaseFire();
	void BeginShowcaseFire();
	void TickShowcaseFirePulse();
	AEnemyCharacter* FindShowcaseEnemy() const;

	int32 CurrentIndex = -1;

	/** Enemy currently burst-firing for the showcase; cleared on toggle-off, warp, or death. */
	TWeakObjectPtr<AEnemyCharacter> ActiveShowcaseEnemy;

	/** ADS-hold delay before fire, then the semi-auto re-trigger pulse. */
	FTimerHandle ShowcaseAimDelayHandle;
	FTimerHandle ShowcaseFirePulseHandle;
};
