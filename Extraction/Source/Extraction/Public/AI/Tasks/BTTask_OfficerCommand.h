// BT task — officer holds behind allied centroid and fires opportunistically.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_OfficerCommand.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_OfficerCommand : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_OfficerCommand();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	/** Radius to scan for allied team-1 pawns when computing the formation centroid. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Officer", meta = (ClampMin = "100.0"))
	float AllyScanRadius = 2000.f;

	/** Distance behind the centroid (away from target) the officer holds. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Officer", meta = (ClampMin = "100.0"))
	float HoldOffset = 600.f;

	/** How often (seconds) the officer re-evaluates its hold position. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Officer", meta = (ClampMin = "0.5"))
	float RepositionInterval = 3.f;

	struct FOfficerCommandMemory
	{
		float RepositionTimer = 0.f;
		bool bFiring = false;
	};

	void CleanUp(UBehaviorTreeComponent& OwnerComp, FOfficerCommandMemory* Mem) const;
	bool ComputeHoldPosition(APawn* Pawn, AActor* Target, FVector& OutPos) const;
};
