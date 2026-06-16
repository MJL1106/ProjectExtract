// BT task — walks the assigned APatrolRoute (loop/ping-pong/shuffle); wanders near points;
// randomises wait duration; glances around while loitering.
// Route-less enemies return to their guard post then sweep.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "EnemyTypes.h"
#include "BTTask_EnemyPatrol.generated.h"

class AEnemyCharacter;
class APatrolRoute;
class UEnemyArchetypeData;

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
		int8 Direction = 1;           // +1 forward, -1 backward (ping-pong)
		bool bWaiting = false;
		float WaitElapsed = 0.f;
		bool bMoveIssued = false;

		// Shuffle-bag: one bit per waypoint (supports ≤32 points; above 32 falls back to pure-random-no-repeat)
		uint32 VisitedMask = 0;

		// The resolved (possibly jittered) world-space move goal; stored once at move-issue time
		FVector CurrentGoal = FVector::ZeroVector;

		// Wait duration rolled for the current stop
		float WaitTarget = 0.f;

		// Guard-post state (route-less enemies) — also reused for loiter glance on routed patrols
		EGuardPhase GuardPhase = EGuardPhase::Return;
		bool bGuardScanActive = false;
		float GuardBaseYaw = 0.f;
		float GuardSweepTimer = 0.f;
		int32 GuardSweepSegment = 0;
		float ReturnElapsed = 0.f;    // Watchdog accumulator for the Return phase

		// Cached per-execute — avoids BB lookup + Cast every tick
		TWeakObjectPtr<APatrolRoute> CachedRoute;
		TWeakObjectPtr<AEnemyCharacter> CachedEnemy;
		TWeakObjectPtr<const UEnemyArchetypeData> CachedDA;
	};

	/** Distance within which the pawn is considered to have reached the guard post (cm). */
	static constexpr float GuardPostAcceptanceRadius = 80.f;

	/** Max seconds allowed for the return-to-post move before degrading to in-place scan. */
	static constexpr float GuardReturnTimeout = 10.f;

	/** Pick the next waypoint index according to PatrolOrder and update Mem accordingly. */
	void PickNextIndex(FPatrolMemory& Mem, const APatrolRoute& Route) const;

	/** Resolve a world-space move goal for Index, applying WanderRadius if set. */
	FVector ResolveGoal(const APawn& Pawn, const APatrolRoute& Route, int32 Index) const;

	/** Resolve goal, store it in Mem.CurrentGoal, and issue MoveToLocation. */
	void IssueMoveToCurrentGoal(
		AAIController& Controller, const APawn& Pawn, FPatrolMemory& Mem, const APatrolRoute& Route) const;

	/** Roll and store Mem.WaitTarget from the route's wait range. */
	void RollWaitTarget(FPatrolMemory& Mem, const APatrolRoute& Route) const;

	/** Tick the guard/loiter yaw sweep — shared between guard-post scan and routed loiter glance. */
	void TickYawSweep(AAIController& Controller, const APawn& Pawn, const UEnemyArchetypeData& DA,
		float BaseYaw, float& Timer, int32& Segment, float DeltaSeconds) const;

	void BeginGuardScan(FPatrolMemory& Mem, float BaseYaw) const;
};
