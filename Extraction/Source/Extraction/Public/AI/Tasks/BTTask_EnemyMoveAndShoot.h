// BT task (latent) — cover-anchored strafe-while-firing: orbit the anchor point, fire in bursts,
// suppress-gate movement, LOS-validate strafe picks. Self-gates on DA->bUsesMoveAndShoot + range band.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyMoveAndShoot.generated.h"

/** Fire sub-loop phase for the move-and-shoot task. */
UENUM()
enum class EMoveShootFirePhase : uint8
{
	Acquire,
	Firing,
	Pause,
};

/** Strafe sub-loop phase. */
UENUM()
enum class EMoveShootStrafePhase : uint8
{
	/** Waiting for the next strafe interval to elapse. */
	Waiting,
	/** Moving to a solved strafe point. */
	Moving,
};

UCLASS()
class EXTRACTION_API UBTTask_EnemyMoveAndShoot : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemyMoveAndShoot();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FMoveAndShootMemory
	{
		// Anchor point (cover slot location or pawn start position; refreshed each strafe repick)
		FVector AnchorPoint = FVector::ZeroVector;

		// --- Fire sub-loop ---
		EMoveShootFirePhase FirePhase = EMoveShootFirePhase::Acquire;
		float FireTimer = 0.f;
		bool bFiring = false;

		// --- Strafe sub-loop ---
		EMoveShootStrafePhase StrafePhase = EMoveShootStrafePhase::Waiting;
		float StrafeTimer = 0.f;
		int32 ConsecutiveMoveFailures = 0;

		// --- No-LOS give-up accumulator ---
		float NoLosAccumulator = 0.f;

		// --- Pursue mode (bMoveAndShootPursue archetypes only) ---
		bool bPursuing = false;
		float PursueRePathTimer = 0.f;

		// --- LOS fire gate (Pattern B) ---
		/** Seconds since LOS was last true; fires while <= FireLosLostGrace. */
		float LosLostTimer = 0.f;
	};

	/** Acceptance radius for strafe MoveToLocation commands. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveAndShoot", meta = (ClampMin = "10.0"))
	float StrafeArrivalAcceptance = 80.f;

	/** Consecutive MoveToLocation failures before aborting the task. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveAndShoot", meta = (ClampMin = "1"))
	int32 MaxConsecutiveMoveFailures = 3;

	/** Eye height offset for LOS traces from strafe candidate points. */
	UPROPERTY(EditAnywhere, Category = "Combat|MoveAndShoot", meta = (ClampMin = "0.0"))
	float EyeHeightOffset = 150.f;

	bool SolveStrafePoint(APawn* Pawn, AActor* Target, const FMoveAndShootMemory* Mem,
		const class UEnemyArchetypeData* DA, FVector& OutPoint) const;

	void CleanUp(UBehaviorTreeComponent& OwnerComp, FMoveAndShootMemory* Mem) const;
};
