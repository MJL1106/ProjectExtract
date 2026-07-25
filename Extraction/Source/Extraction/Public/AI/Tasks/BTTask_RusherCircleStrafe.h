// BT task — rusher standoff phase: orbits the combat target on the RushStandoffRadius ring while
// firing. Tangential nav-point picks stepped RushCircleStepDeg per repick (Strafe* DA cadence),
// direction flips on nav/LoS failure. Self-exits (Failed) when the target leaves the standoff band
// so the selector falls back to the cover-advance branch.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_RusherCircleStrafe.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_RusherCircleStrafe : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_RusherCircleStrafe();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FCircleMemory
	{
		// +1 / -1 -- current orbit direction around the target; flips when a pick fails.
		float OrbitSign = 1.f;

		// World time of the next orbit-point repick.
		float NextRepickTime = 0.f;

		/** Seconds since LOS was last true; fires while <= FireLosLostGrace. */
		float LosLostTimer = 0.f;
	};

	/** Picks the next nav-valid, LoS-valid point on the standoff ring. Flips Mem->OrbitSign when the
	 *  preferred side fails. Returns false when no candidate validates this repick. */
	bool PickOrbitPoint(class AEnemyCharacter* Enemy, const AActor* Target, FCircleMemory* Mem, FVector& OutPoint) const;

	void CleanUp(UBehaviorTreeComponent& OwnerComp) const;
};
