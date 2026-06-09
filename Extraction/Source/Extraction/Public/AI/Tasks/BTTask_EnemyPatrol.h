// BT task — walks the assigned APatrolRoute; loops or ping-pongs; waits at each point.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "EnemyTypes.h"
#include "BTTask_EnemyPatrol.generated.h"

class APatrolRoute;

UCLASS()
class EXTRACTION_API UBTTask_EnemyPatrol : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemyPatrol();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FPatrolMemory
	{
		int32 CurrentIndex = 0;
		int8 Direction = 1;     // +1 forward, -1 backward (ping-pong)
		bool bWaiting = false;
		float WaitElapsed = 0.f;
		bool bMoveIssued = false;
	};

	void AdvanceIndex(FPatrolMemory& Mem, const APatrolRoute& Route) const;
};
