// AI controller for enemy characters — perception, team 1, blackboard, behaviour tree.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "EnemyTypes.h"
#include "EnemyAIController.generated.h"

class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UEnemyAwarenessComponent;
class UEnemyMoraleComponent;
enum class EMoraleState : uint8;

DECLARE_LOG_CATEGORY_EXTERN(LogEnemyAI, Log, All);

UCLASS()
class EXTRACTION_API AEnemyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AEnemyAIController();

	// Blackboard key names (match BB_Enemy asset exactly)
	static const FName BB_CombatTarget;
	static const FName BB_LastKnownLocation;
	static const FName BB_InvestigateLocation;
	static const FName BB_AwarenessState;
	static const FName BB_HasLineOfSight;
	static const FName BB_TargetInRange;
	static const FName BB_CoverSlot;
	static const FName BB_HasCover;
	static const FName BB_PatrolRoute;
	static const FName BB_MoraleState;

	UFUNCTION(BlueprintPure, Category = "Enemy|AI")
	UEnemyAwarenessComponent* GetAwarenessComponent() const { return AwarenessComponent; }

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Enemy|AI")
	TObjectPtr<UBehaviorTree> EnemyBehaviorTree;

private:
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	UPROPERTY()
	TObjectPtr<UEnemyAwarenessComponent> AwarenessComponent;

	void ReleaseCoverSlotIfClaimed();

	UFUNCTION()
	void HandlePawnDeath();

	/** Called when the awareness component broadcasts a state change. Releases cover and clears focus on Combat exit. */
	UFUNCTION()
	void HandleAwarenessStateChanged(EEnemyAwarenessState OldState, EEnemyAwarenessState NewState);

	UFUNCTION()
	void HandleMoraleStateChanged(EMoraleState OldState, EMoraleState NewState);
};
