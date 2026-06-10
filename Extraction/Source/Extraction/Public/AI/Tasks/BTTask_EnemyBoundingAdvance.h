// BT task (latent) — bounding flanker: compute bound point toward target, move at combat speed firing.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyBoundingAdvance.generated.h"

class UEnemySquad;

UCLASS()
class EXTRACTION_API UBTTask_EnemyBoundingAdvance : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemyBoundingAdvance();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FBoundingAdvanceMemory
	{
		bool bMoveIssued = false;
		bool bFiring = false;
		FVector BoundPoint = FVector::ZeroVector;
		TWeakObjectPtr<UEnemySquad> CachedSquad;
	};

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Bounding", meta = (ClampMin = "200.0"))
	float HopDistance = 750.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Bounding", meta = (ClampMin = "10.0"))
	float ArrivalAcceptance = 120.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Bounding", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SurvivalHealthFraction = 0.35f;

	/** Slack added to ArrivalAcceptance when verifying move-end proximity. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Bounding", meta = (ClampMin = "0.0"))
	float ArrivalSlack = 200.f;

	bool SolveBoundPoint(APawn* Pawn, AActor* Target, FVector& OutPoint) const;
	void StopFireAndAim(UBehaviorTreeComponent& OwnerComp, FBoundingAdvanceMemory* Mem) const;
	UEnemySquad* ResolveSquad(class AEnemyCharacter* Enemy, FBoundingAdvanceMemory* Mem) const;
	void NotifyArrived(class AEnemyCharacter* Enemy, FBoundingAdvanceMemory* Mem) const;
	void NotifyBlocked(class AEnemyCharacter* Enemy, FBoundingAdvanceMemory* Mem) const;
};
