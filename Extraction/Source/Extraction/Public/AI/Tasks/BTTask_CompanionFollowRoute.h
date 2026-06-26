// BT task — walks a pre-authored ACompanionRoute waypoint-by-waypoint.
// One-shot ordered traversal with per-waypoint stance, aim, dwell, and wait-for-player.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Companion/CompanionRouteTypes.h"
#include "BTTask_CompanionFollowRoute.generated.h"

class ACompanionRoute;
class ACompanionCharacter;
class ACompanionAIController;

UCLASS()
class EXTRACTION_API UBTTask_CompanionFollowRoute : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionFollowRoute();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	enum class ERoutePhase : uint8
	{
		MovingToStart,
		Moving,
		WaitingForPlayer,
		Dwelling,
		Holding,
	};

	struct FRouteMemory
	{
		TWeakObjectPtr<ACompanionRoute> CachedRoute;
		TWeakObjectPtr<ACompanionCharacter> CachedCharacter;
		TWeakObjectPtr<ACompanionAIController> CachedController;

		int32 CurrentIndex = 0;
		ERoutePhase Phase = ERoutePhase::MovingToStart;
		bool bMoveIssued = false;
		bool bCompletedBroadcast = false;
		bool bAnyReached = false;

		float DwellElapsed = 0.f;
		float DwellTarget = 0.f;
		float WaitElapsed = 0.f;
		float WaitTimeout = 0.f;
		float WaitRadiusSq = 0.f;

		int32 MoveFailCount = 0;

		// Snapshots at task start, restored on cleanup
		float OriginalMaxWalkSpeed = 0.f;
		float OriginalMaxWalkSpeedCrouched = 0.f;

		// Cached focal point to skip redundant SetFocalPoint calls
		FVector LastFocalPoint = FVector::ZeroVector;
	};

	// Stance-driven walk speeds (cm/s)
	static constexpr float RelaxedSpeed = 200.f;
	static constexpr float AlertSpeed = 150.f;
	static constexpr float CrouchSpeed = 120.f;

	// Look-ahead distance projected along the path for procedural aim (cm)
	static constexpr float AimLookAheadDistance = 300.f;

	// Max consecutive MoveToLocation failures before skipping a waypoint
	static constexpr int32 MaxMoveFailsPerWaypoint = 3;

	// Epsilon for focal-point dedup (squared distance, cm^2)
	static constexpr float FocalPointEpsilonSq = 4.f;

	/** Issue a MoveToLocation for the given waypoint index. Returns false if the route is stale or move failed too many times. */
	bool IssueMoveToWaypoint(AAIController& Controller, FRouteMemory& Mem, int32 Index) const;

	/** Apply stance locomotion (speed, crouch, aim, low-ready) for the current leg. */
	void ApplyStance(FRouteMemory& Mem, ECompanionRouteStance Stance, float SpeedOverride) const;

	/** Update aim focus point each tick based on stance and waypoint overrides. */
	void UpdateAimFocus(FRouteMemory& Mem, int32 TargetIndex) const;

	/** Set focal point with dedup against the cached value. */
	void SetFocalPointCached(FRouteMemory& Mem, ACompanionAIController& Controller, const FVector& Point) const;

	/** Advance to the next waypoint or finalise the route. Returns Succeeded/Failed/InProgress. */
	EBTNodeResult::Type AdvanceToNext(UBehaviorTreeComponent& OwnerComp, FRouteMemory& Mem) const;

	/** Handle arrival at the final waypoint. */
	EBTNodeResult::Type HandleReachFinal(UBehaviorTreeComponent& OwnerComp, FRouteMemory& Mem) const;

	/** Restore default locomotion state (uncrouch, clear focus, restore walk speed, clear scripted aim). Idempotent. */
	void RestoreDefaults(FRouteMemory& Mem) const;
};
