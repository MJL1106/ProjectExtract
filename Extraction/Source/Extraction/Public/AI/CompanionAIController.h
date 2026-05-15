// AI controller for the companion — perception, blackboard, behaviour tree.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Movement/TraversalTypes.h"
#include "CompanionAIController.generated.h"

class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class AExtractionCharacter;
class UCompanionTuningDataAsset;
class UTraversalComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogCompanionAI, Log, All);

UCLASS()
class EXTRACTION_API ACompanionAIController : public AAIController
{
	GENERATED_BODY()

public:
	ACompanionAIController();

	// Blackboard key names
	static const FName BB_PlayerActor;
	static const FName BB_PlayerNeedsRevive;
	static const FName BB_CombatTarget;
	static const FName BB_CoverLocation;
	static const FName BB_HasCoverPosition;
	static const FName BB_PlayerTraversalActive;
	static const FName BB_PlayerTraversalObstacle;
	static const FName BB_PlayerTraversalLanding;
	static const FName BB_PlayerTraversalType;
	static const FName BB_Posture;                       // enum: ECompanionPosture
	static const FName BB_ScoringWeight_LoSPlayer;       // float
	static const FName BB_ScoringWeight_AvoidEnemy;      // float
	static const FName BB_ScoringWeight_CoverFromTarget; // float
	static const FName BB_CoverSlot;                     // Object (AAICoverSlot)

	UFUNCTION(BlueprintPure, Category = "Companion|AI")
	AExtractionCharacter* GetPlayerCharacter() const { return CachedPlayerCharacter; }

	void SetPlayerCharacter(AExtractionCharacter* InPlayer) { CachedPlayerCharacter = InPlayer; }

	const UCompanionTuningDataAsset* GetTuning() const { return Tuning; }

	/** Resets the BB_PlayerTraversal* keys. Public so BT tasks can call on early-finish paths. */
	void ClearTraversalBlackboard();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|AI")
	TObjectPtr<UBehaviorTree> CompanionBehaviorTree;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|Tuning")
	TObjectPtr<UCompanionTuningDataAsset> Tuning;

private:
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	UPROPERTY()
	TObjectPtr<AExtractionCharacter> CachedPlayerCharacter;

	// Traversal mirror coupling
	TWeakObjectPtr<UTraversalComponent> PlayerTraversalComp;
	FDelegateHandle TraversalStartedHandle;
	FDelegateHandle TraversalEndedHandle;

	// Warp safety net
	FTimerHandle WarpStuckTimer;
	FTimerHandle BindRetryTimer;
	float TimeSinceClosedToPlayer = 0.f;
	float TimeOnDifferentLevel = 0.f;
	int32 BindAttempts = 0;

	void TryBindToPlayerTraversal();
	void OnPlayerTraversalStarted(ETraversalType Type, float PlayRate, FVector ObstacleLocation, FVector LandingLocation);
	void OnPlayerTraversalEnded();
	void TickWarpFallback();
	bool IsCompanionRecentlyRendered() const;
	bool ShouldWarp() const;
	void ExecuteWarpBehindPlayer();
};
