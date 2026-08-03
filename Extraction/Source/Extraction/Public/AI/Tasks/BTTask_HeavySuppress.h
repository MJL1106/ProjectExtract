// BT task — heavy pushes toward the threat firing on the move, anchors inside EngageRangeMin,
// and drives sustained burst fire at target or last-known location.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_HeavySuppress.generated.h"

UENUM()
enum class EHeavySuppressPhase : uint8
{
	Fire,
	Pause,
};

UCLASS()
class EXTRACTION_API UBTTask_HeavySuppress : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_HeavySuppress();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FHeavySuppressMemory
	{
		EHeavySuppressPhase Phase = EHeavySuppressPhase::Fire;
		float PhaseTimer = 0.f;
		bool bAimOverrideActive = false;
		bool bAdvancing = false;
		float RepathTimer = 0.f;
		FVector LastMoveGoal = FVector::ZeroVector;
		/** True once the heavy has had LOS at least once — a never-sighted heavy must never fire. */
		bool bEverHadLOS = false;
		/** Seconds since LOS was last true; fires while <= FireLosLostGrace. */
		float LosLostTimer = 0.f;
	};

	/** Advance/anchor decision: push toward ThreatLoc while outside EngageRangeMin (+hysteresis),
	 *  stop and anchor once inside. bHasAimSource=false always anchors. */
	static void UpdateAdvance(class AAIController* Controller, const class APawn* Pawn,
		const class UEnemyArchetypeData* DA, FHeavySuppressMemory* Mem,
		bool bHasAimSource, const FVector& ThreatLoc, float DeltaSeconds);

	void CleanUp(UBehaviorTreeComponent& OwnerComp, FHeavySuppressMemory* Mem) const;
};
