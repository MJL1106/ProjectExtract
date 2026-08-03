// BT task — the rusher charge phase: claims a squad rush token (cap = MaxSimultaneousRushers),
// closes on the combat target along a per-slot spread bearing (chargers approach from different
// sides, converging only at melee range), fires on the move, melees on contact.
// No token available → Failed, so the selector falls through to the circle-strafe branch.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_RusherAdvance.generated.h"

class AEnemyCharacter;

UCLASS()
class EXTRACTION_API UBTTask_RusherAdvance : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_RusherAdvance();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FRusherMemory
	{
		float RePathTimer = 0.f;

		// Rush-token slot (spread bearing side), INDEX_NONE when squadless/uncapped.
		int32 SlotIndex = INDEX_NONE;

		/** Seconds since LOS was last true; fires while <= FireLosLostGrace. */
		float LosLostTimer = 0.f;
	};

	/** Issues the next move: spread-bearing approach point while far, direct MoveToActor for the
	 *  final stretch (or when nav projection fails). */
	void IssueApproachMove(class AAIController* Controller, AEnemyCharacter* Enemy, AActor* Target, const FRusherMemory* Mem) const;

	/** bReleaseToken=false on the melee-success path — the charger keeps its token through the melee
	 *  loop (the task re-executes immediately) so a waiting squadmate cannot steal the slot mid-fight. */
	void CleanUp(UBehaviorTreeComponent& OwnerComp, bool bReleaseToken) const;
};
