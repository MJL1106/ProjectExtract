// BT task — companion faces enemy, fires in bursts with settling inaccuracy, reloads.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_CompanionCombat.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_CompanionCombat : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionCombat();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual FString GetStaticDescription() const override;

protected:
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	/** Seconds of continuous fire per burst */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.1"))
	float FireBurstDuration = 1.5f;

	/** Seconds of pause between bursts */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.1"))
	float FirePauseDuration = 0.5f;

	/** Toggle verbose exit-gate logs + debug draw under LogCompanionAI. Enable per-instance in BT. */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDebugLogging = false;

private:
	float BurstTimer = 0.0f;
	bool bIsFiringBurst = false;
};
