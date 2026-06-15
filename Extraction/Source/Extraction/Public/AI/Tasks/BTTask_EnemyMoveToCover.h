// BT task — queries the cover registry, claims a slot, and moves the enemy to it.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyMoveAndShoot.h"
#include "BTTask_EnemyMoveToCover.generated.h"

class AEnemyCharacter;
class UEnemyArchetypeData;

UCLASS()
class EXTRACTION_API UBTTask_EnemyMoveToCover : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemyMoveToCover();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FMoveToCoverMemory
	{
		FVector ArrivalPos = FVector::ZeroVector;
		bool bMoveIssued = false;
		bool bSlotClaimed = false;

		// Cached per-execute — avoids Cast + GetArchetypeData every tick
		TWeakObjectPtr<AEnemyCharacter> CachedEnemy;
		TWeakObjectPtr<const UEnemyArchetypeData> CachedDA;

		// Advance-fire sub-loop
		EMoveShootFirePhase FirePhase = EMoveShootFirePhase::Acquire;
		float FireTimer = 0.f;
		float FireTickAccum = 0.f;
		bool bFiring = false;

		// Stall detection — abandon a cover move that stops making progress (pinned).
		float StallBestDist = TNumericLimits<float>::Max();
		float StallAccum = 0.f;
	};

	void ReleaseClaim(UBlackboardComponent* BB, APawn* Pawn) const;

	/** Stops weapon, clears aim target and extra spread. Optionally keeps focus for handoff. */
	void StopAdvanceFire(UBehaviorTreeComponent& OwnerComp, FMoveToCoverMemory* Mem, bool bKeepFocus) const;

	class AAICoverSlot* ScoreSlotsWithSpacing(APawn* Pawn, const class AEnemyCharacter* Enemy,
		AActor* Target, const class UEnemyArchetypeData* DA, class UCoverRegistrySubsystem* Registry) const;
};
