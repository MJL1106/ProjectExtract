// BT task — moves to InvestigateLocation then performs a yaw-sweep look-around.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemySearchLastKnown.generated.h"

UENUM()
enum class ESearchPhase : uint8
{
	MoveTo,
	Sweep,
};

UCLASS()
class EXTRACTION_API UBTTask_EnemySearchLastKnown : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemySearchLastKnown();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FSearchMemory
	{
		ESearchPhase Phase = ESearchPhase::MoveTo;
		float SweepElapsed = 0.f;
		int32 SweepSegment = 0;
		float BaseYaw = 0.f;
		/** The goal last issued to MoveToLocation — used to detect BB drift. */
		FVector IssuedGoal = FVector::ZeroVector;
	};

	/** If BB_InvestigateLocation drifts more than this from the issued move goal, re-issue the move. */
	UPROPERTY(EditAnywhere, Category = "Search")
	float GoalDriftThreshold = 60.f;

	/** Total sweep duration (seconds) split across SweepSegmentCount segments. */
	static constexpr float SweepDuration = 3.f;
	static constexpr int32 SweepSegmentCount = 3;
	static constexpr float SweepYawRange = 90.f; // total yaw swept
};
