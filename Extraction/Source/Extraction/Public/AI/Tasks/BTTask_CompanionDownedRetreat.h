// BT task — the downed companion crawls to the safest nearby cover point and holds there in the
// downed idle until revived. Latent forever: the IsDowned branch decorator abort is the only exit.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_CompanionDownedRetreat.generated.h"

class ACompanionCharacter;

UCLASS()
class EXTRACTION_API UBTTask_CompanionDownedRetreat : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CompanionDownedRetreat();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual FString GetStaticDescription() const override;

protected:
	/** Search radius (cm) for the crawl-destination cover query. Crawl pace is ~100 cm/s — keep
	 *  this short enough that the retreat completes well inside the bleedout window. */
	UPROPERTY(EditAnywhere, Category = "Downed", meta = (ClampMin = "0.0"))
	float CoverSearchRadius = 1200.f;

	/** Arrival distance (cm) to the chosen hunker position. */
	UPROPERTY(EditAnywhere, Category = "Downed", meta = (ClampMin = "10.0"))
	float ArrivalRadius = 60.f;

	/** Max perceived threats scored per cover candidate (body-shield traces are the cost). */
	UPROPERTY(EditAnywhere, Category = "Downed", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MaxThreatsScored = 6;

private:
	/** Best shielded-then-nearest cover hunker position, or the pawn's own location when no
	 *  available cover exists in radius (idle where downed). */
	FVector PickRetreatDestination(ACompanionCharacter& Companion) const;

	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;
	FVector RetreatDestination = FVector::ZeroVector;
	bool bMoveIssued = false;
	bool bArrived = false;
	/** True while movement is frozen for an active revive hold. */
	bool bHeldForRevive = false;
	/** Low-rate accumulator for the crawl-clamp re-assert + arrival checks. */
	float SlowTickAccumulator = 0.f;
	/** Re-issues of a dying move request before giving up and idling in place. */
	int32 MoveRetries = 0;
};
