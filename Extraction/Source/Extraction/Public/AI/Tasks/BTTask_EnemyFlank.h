// BT task (latent) — flanker role: ring-sample a flank point behind target's facing, move and fire.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyFlank.generated.h"

UCLASS()
class EXTRACTION_API UBTTask_EnemyFlank : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemyFlank();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FFlankMemory
	{
		bool bRoleClaimed = false;
		bool bMoveIssued = false;
		bool bFiring = false;
	};

	/** Number of sample points around the target ring. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Flank", meta = (ClampMin = "4", ClampMax = "24"))
	int32 RingSampleCount = 12;

	/** Extra radius past EngageRangeMin for the outer edge of the flank ring. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Flank", meta = (ClampMin = "100.0"))
	float RingOuterPadding = 600.f;

	/** Acceptance radius for the move-to command. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Flank", meta = (ClampMin = "10.0"))
	float ArrivalAcceptance = 120.f;

	bool SolveFlankPoint(APawn* Pawn, AActor* Target, float RangeMin, FVector& OutPoint) const;
	void ReleaseRoleAndStopFire(UBehaviorTreeComponent& OwnerComp, FFlankMemory* Mem) const;
};
