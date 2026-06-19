// BT decorator — gates the rush branch: true when the combat target is within commit range AND health is above the abort fraction.
// Uses a hysteresis latch (bRushing) so the switch requires opening distance past (CommitRange + Hysteresis) before it reverts to hold.
// CalculateRawConditionValue is the live source of truth (latch-aware). TickNode fires aborts on change.
// bRushing is reset in InitializeMemory (runs on every subtree re-push) so every new engagement starts in hold.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_ShotgunCommit.generated.h"

class AEnemyCharacter;
class UHealthComponent;
class UEnemyArchetypeData;

UCLASS()
class EXTRACTION_API UBTDecorator_ShotgunCommit : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_ShotgunCommit();

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FShotgunCommitMemory
	{
		// Hysteresis latch — true while the enemy is committed to a rush within the current engagement.
		// Reset in InitializeMemory (runs on every subtree re-push) so re-entry always starts in hold.
		bool bRushing = false;

		// Tracks the last value returned by CalculateRawConditionValue so TickNode can detect changes.
		bool bLastCondition = false;

		// One-shot flag: DA validation warning has been emitted for this instance.
		bool bValidationWarningEmitted = false;

		// Cached character — resolve-on-miss, reset in InitializeMemory/CleanupMemory.
		TWeakObjectPtr<AEnemyCharacter> CachedEnemy;
	};

	// Resolves and caches AEnemyCharacter from the controller pawn. Returns nullptr on failure.
	AEnemyCharacter* ResolveEnemy(UBehaviorTreeComponent& OwnerComp, FShotgunCommitMemory* Mem) const;
};
