// BT service — ticks to update all companion blackboard keys (DBNO, combat target, etc).

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Companion/CompanionTypes.h"
#include "BTService_UpdateCompanionState.generated.h"

UCLASS()
class EXTRACTION_API UBTService_UpdateCompanionState : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateCompanionState();

	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerActorKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlayerNeedsReviveKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CombatTargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasCoverPositionKey;

	/** Toggle verbose perception/target logs under LogCompanionAI. Enable per-instance in BT. */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDebugLogging = false;

	UPROPERTY(EditAnywhere, Category = "Posture", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float ExploreReturnDelay = 3.f;

private:
	float OutOfCombatTimer = 0.f;
};
