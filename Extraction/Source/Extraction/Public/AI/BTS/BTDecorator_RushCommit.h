// BT decorator — gates the rusher charge branch: true when a squad rush token is available/held,
// the target is inside charge-launch range, and a vulnerability trigger fires (target reloading with
// LoS, target low health, LoS lost — target hiding, point-blank override, or a small base chance).
// Uses a hysteresis latch (bCharging) so a committed charge holds until the target opens distance past
// (ChargeMaxRange + Hysteresis). Latch resets in InitializeMemory so each engagement starts uncommitted.
// Token CLAIM happens in BTTask_RusherAdvance (race-safe) — this decorator only reads availability.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_RushCommit.generated.h"

class AEnemyCharacter;

UCLASS()
class EXTRACTION_API UBTDecorator_RushCommit : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_RushCommit();

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FRushCommitMemory
	{
		// Hysteresis latch — true while committed to a charge within the current engagement.
		bool bCharging = false;

		// Last value returned by CalculateRawConditionValue so TickNode can detect changes.
		bool bLastCondition = false;

		// World time of the next trigger evaluation (reload/low-health/LoS-lost/base-chance).
		float NextTriggerEvalTime = 0.f;

		// World time LoS to the target was last present — drives the LoS-lost (hiding) trigger.
		float LastLosTime = 0.f;

		// Cached character — resolve-on-miss, reset in InitializeMemory/CleanupMemory.
		TWeakObjectPtr<AEnemyCharacter> CachedEnemy;
	};

	// Resolves and caches AEnemyCharacter from the controller pawn. Returns nullptr on failure.
	AEnemyCharacter* ResolveEnemy(UBehaviorTreeComponent& OwnerComp, FRushCommitMemory* Mem) const;

	// Latch-aware condition body; CalculateRawConditionValue wraps it to mirror the result to the
	// RusherCharging BB flag. Return value always equals Mem->bCharging on exit.
	bool ComputeCondition(UBehaviorTreeComponent& OwnerComp, FRushCommitMemory* Mem) const;

	// True when any vulnerability trigger fires this evaluation. bHasLOS gates the reload read.
	bool EvaluateTriggers(const AEnemyCharacter* Enemy, const AActor* Target, bool bHasLOS, float LosLostSeconds) const;

	// True when the target is visibly mid-reload (player weapon component or companion).
	static bool IsTargetReloading(const AActor* Target);
};
