//// Copyright, (c) Sami Kangasmaa 2023

#pragma once

#include "CoreMinimal.h"
#include "CoverSystemPublicData.h"

#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"

#include "ManualCoverPoint.generated.h"

/**
* Manual cover point that can be hand-placed to the level.
* Registers to CoverSystem which takes this into generation.
*/
UCLASS(Blueprintable, Placeable)
class AICOVERSYSTEM_API AManualCoverPoint : public AActor
{
	GENERATED_BODY()
	
public:

	AManualCoverPoint();

	virtual void BeginPlay() override;

	virtual void EndPlay(EEndPlayReason::Type Reason) override;

public:

	/**
	 * Returns cover data of this cover point.
	 * This is injected into octree in cover generation
	 */
	UFUNCTION(BlueprintPure, Category = "Manual Cover Point")
	FCoverData GetCoverData() const;

protected:

	/** Is it a Left cover (can lean on left) */
	UPROPERTY(Category = "Manual Cover Point", EditAnywhere, BlueprintReadOnly)
	bool bLeftCoverStanding = false;

	/** Is it a Right cover (can lean on Right) */
	UPROPERTY(Category = "Manual Cover Point", EditAnywhere, BlueprintReadOnly)
	bool bRightCoverStanding = false;

	/** Is it a Left cover (can lean on left) */
	UPROPERTY(Category = "Manual Cover Point", EditAnywhere, BlueprintReadOnly)
	bool bLeftCoverCrouched = false;

	/** Is it a Right cover (can lean on Right) */
	UPROPERTY(Category = "Manual Cover Point", EditAnywhere, BlueprintReadOnly)
	bool bRightCoverCrouched = false;

	/** Is it a front cover, so crouch gives cover and while standing there's line of sight for shooting */
	UPROPERTY(Category = "Manual Cover Point", EditAnywhere, BlueprintReadOnly)
	bool bFrontCoverCrouched = true;

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manual Cover Point")
	USceneComponent* RootSceneComponent;

#if WITH_EDITORONLY_DATA

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Manual Cover Point")
	UCapsuleComponent* PreviewCapsule;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Manual Cover Point")
	UArrowComponent* PreviewArrow;

#endif

private:

	void RegisterToCoverSystem();
	void UnregisterFromCoverSystem();

	// Is this point registered to the cover system?
	bool bIsRegistered = false;

	// Cover data that was cached
	FCoverData CachedCoverData;
	bool bHasCachedCoverData = false;
	
	// Formats cover data from properties configured in this point
	FCoverData BakeCoverData() const;
};
