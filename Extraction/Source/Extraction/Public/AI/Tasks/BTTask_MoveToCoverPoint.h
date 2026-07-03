// BT task (latent) — moves to an AICS cover point, with optional advance-fire sub-FSM + stall detection.
// Shared by enemy and companion BTs. Does NOT occupy/unoccupy AICS covers (plugin service handles that).

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BTTask_EnemyMoveAndShoot.h"
#include "AI/Cover/CoverPoseTypes.h"
#include "CoverSystemPublicData.h"
#include "BTTask_MoveToCoverPoint.generated.h"

class AEnemyCharacter;
class UEnemyArchetypeData;
class UCoverPoseComponent;

UCLASS()
class EXTRACTION_API UBTTask_MoveToCoverPoint : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_MoveToCoverPoint();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual FString GetStaticDescription() const override;
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

protected:

	/** When true, the enemy fires at the combat target while advancing (uses advance-fire DA fields). */
	UPROPERTY(EditAnywhere, Category = "Cover")
	bool bFireWhileAdvancing = false;

	/** BB key holding the Cover type (FCover via AICS plugin). */
	UPROPERTY(EditAnywhere, Category = "Cover")
	FBlackboardKeySelector CoverTargetKey;

	/** Optional BB key (bool) — set TRUE on successful arrival. */
	UPROPERTY(EditAnywhere, Category = "Cover")
	FBlackboardKeySelector HasCoverKey;

	/** Fallback standoff padding when the pawn is not an AEnemyCharacter (cm). */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.0"))
	float DefaultStandoffPadding = 25.f;

private:

	struct FMoveToCoverPointMemory
	{
		FVector ArrivalPos = FVector::ZeroVector;
		FCoverHandle CoverHandle;
		FCoverData CachedCoverData;
		bool bMoveIssued = false;

		// Cached per-execute
		TWeakObjectPtr<AEnemyCharacter> CachedEnemy;
		TWeakObjectPtr<const UEnemyArchetypeData> CachedDA;

		// Advance-fire sub-loop (ported from BTTask_EnemyMoveToCover)
		EMoveShootFirePhase FirePhase = EMoveShootFirePhase::Acquire;
		float FireTimer = 0.f;
		float FireTickAccum = 0.f;
		bool bFiring = false;

		// Stall detection
		float StallBestDist = TNumericLimits<float>::Max();
		float StallAccum = 0.f;

		void Reset()
		{
			ArrivalPos = FVector::ZeroVector;
			CoverHandle = FCoverHandle();
			CachedCoverData = FCoverData();
			bMoveIssued = false;
			CachedEnemy.Reset();
			CachedDA.Reset();
			FirePhase = EMoveShootFirePhase::Acquire;
			FireTimer = 0.f;
			FireTickAccum = 0.f;
			bFiring = false;
			StallBestDist = TNumericLimits<float>::Max();
			StallAccum = 0.f;
		}
	};

	void StopAdvanceFire(UBehaviorTreeComponent& OwnerComp, FMoveToCoverPointMemory* Mem, bool bKeepFocus) const;
	void HandleArrival(UBehaviorTreeComponent& OwnerComp, FMoveToCoverPointMemory* Mem,
		UBlackboardComponent* BB, APawn* Pawn, const FCoverData& Data) const;
	void HandleFailure(UBehaviorTreeComponent& OwnerComp, FMoveToCoverPointMemory* Mem,
		UBlackboardComponent* BB, AAIController* Controller) const;
};
