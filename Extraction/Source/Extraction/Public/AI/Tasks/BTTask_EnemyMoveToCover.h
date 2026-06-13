// BT task — queries the cover registry, claims a slot, and moves the enemy to it.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyMoveToCover.generated.h"

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
		bool bMoveIssued = false;
		bool bSlotClaimed = false;
	};

	void ReleaseClaim(UBlackboardComponent* BB, APawn* Pawn) const;

	class AAICoverSlot* ScoreSlotsWithSpacing(APawn* Pawn, const class AEnemyCharacter* Enemy,
		AActor* Target, const class UEnemyArchetypeData* DA, class UCoverRegistrySubsystem* Registry) const;
};
