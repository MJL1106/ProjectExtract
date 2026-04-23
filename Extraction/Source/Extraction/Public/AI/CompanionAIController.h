// AI controller for the companion — perception, blackboard, behaviour tree.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CompanionAIController.generated.h"

class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class AExtractionCharacter;

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

	UFUNCTION(BlueprintPure, Category = "Companion|AI")
	AExtractionCharacter* GetPlayerCharacter() const { return CachedPlayerCharacter; }

	void SetPlayerCharacter(AExtractionCharacter* InPlayer) { CachedPlayerCharacter = InPlayer; }

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UPROPERTY(EditDefaultsOnly, Category = "Companion|AI")
	TObjectPtr<UBehaviorTree> CompanionBehaviorTree;

private:
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	UPROPERTY()
	TObjectPtr<AExtractionCharacter> CachedPlayerCharacter;
};
