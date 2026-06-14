// BT task — broken-morale fallback: find deep cover, turtle, and peek-fire rarely.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_EnemyMoveAndShoot.h"
#include "BTTask_EnemyFallback.generated.h"

class AAICoverSlot;

UENUM()
enum class EFallbackPhase : uint8
{
	FindCover,
	Moving,
	Holding,
	PeekFire,
	PeekRecover,
};

UCLASS()
class EXTRACTION_API UBTTask_EnemyFallback : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_EnemyFallback();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FFallbackMemory
	{
		EFallbackPhase Phase = EFallbackPhase::FindCover;
		float PhaseTimer = 0.f;
		float PeekCooldown = 0.f;
		float CoverRetryTimer = 0.f;
		bool bSlotClaimed = false;
		FVector ArrivalPos = FVector::ZeroVector;
		TWeakObjectPtr<AAICoverSlot> LastFailedSlot;

		// Advance-fire sub-loop (mirrors FMoveToCoverMemory)
		EMoveShootFirePhase FirePhase = EMoveShootFirePhase::Acquire;
		float FireTimer = 0.f;
		float FireTickAccum = 0.f;
		bool bFiring = false;
	};

	/** Search radius multiplier applied to the DA CoverSearchRadius. */
	UPROPERTY(EditAnywhere, Category = "Fallback", meta = (ClampMin = "1.0"))
	float CoverSearchRadiusMultiplier = 2.f;

	/** Bonus score per cm of distance from threat (favours deeper cover). */
	UPROPERTY(EditAnywhere, Category = "Fallback", meta = (ClampMin = "0.0"))
	float DistanceFromThreatWeight = 2.f;

	/** Seconds between peek-fire bursts while holding. */
	UPROPERTY(EditAnywhere, Category = "Fallback", meta = (ClampMin = "1.0"))
	float PeekIntervalMin = 5.f;

	UPROPERTY(EditAnywhere, Category = "Fallback", meta = (ClampMin = "1.0"))
	float PeekIntervalMax = 10.f;

	/** How long the peek-fire burst lasts (seconds). */
	UPROPERTY(EditAnywhere, Category = "Fallback", meta = (ClampMin = "0.1"))
	float PeekBurstDuration = 0.6f;

	/** Settle time after re-crouching before returning to hold. */
	UPROPERTY(EditAnywhere, Category = "Fallback", meta = (ClampMin = "0.05"))
	float PeekRecoverDuration = 0.2f;

	/** How often (seconds) to retry finding cover while holding without any. */
	UPROPERTY(EditAnywhere, Category = "Fallback", meta = (ClampMin = "1.0"))
	float CoverRetryInterval = 5.f;

	bool FindDeepCover(UBehaviorTreeComponent& OwnerComp, FFallbackMemory* Mem);
	void StopFallbackFire(UBehaviorTreeComponent& OwnerComp, FFallbackMemory* Mem, bool bKeepFocus) const;
	void StopFireAndCleanUp(UBehaviorTreeComponent& OwnerComp) const;
	void ReleaseClaim(UBlackboardComponent* BB, APawn* Pawn) const;
};
