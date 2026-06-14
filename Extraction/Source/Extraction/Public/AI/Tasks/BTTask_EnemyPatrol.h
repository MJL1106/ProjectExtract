// BT task — walks the assigned APatrolRoute; loops or ping-pongs; waits at each point.
// Route-less enemies return to their guard post then sweep.

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

	enum class EGuardPhase : uint8
	{
		Return,  // Moving back to the guard post
		Scan,    // Yaw sweep at the post
	};

	struct FPatrolMemory
	{
		// Routed patrol state
		int32 CurrentIndex = 0;
		int8 Direction = 1;     // +1 forward, -1 backward (ping-pong)
		bool bWaiting = false;
		float WaitElapsed = 0.f;
		bool bMoveIssued = false;

		// Guard-post state (route-less enemies)
		EGuardPhase GuardPhase = EGuardPhase::Return;
		bool bGuardScanActive = false;
		float GuardBaseYaw = 0.f;
		float GuardSweepTimer = 0.f;
		int32 GuardSweepSegment = 0;
		float ReturnElapsed = 0.f;   // Watchdog accumulator for the Return phase
	};

	/** Distance within which the pawn is considered to have reached the guard post (cm). */
	static constexpr float GuardPostAcceptanceRadius = 80.f;

	/** Max seconds allowed for the return-to-post move before degrading to in-place scan. */
	static constexpr float GuardReturnTimeout = 10.f;

	void AdvanceIndex(FPatrolMemory& Mem, const APatrolRoute& Route) const;
	void BeginGuardScan(FPatrolMemory& Mem, float BaseYaw) const;
};
