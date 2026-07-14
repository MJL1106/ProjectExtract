// BT decorator — gates the rusher circle-strafe branch: true when the combat target is inside
// RushStandoffEnterRange. Hysteresis latch (bCircling): once circling, holds until the target opens
// distance past (EnterRange + Hysteresis), then falls back to the cover-advance approach.
// Same tick-observer shape as BTDecorator_ShotgunCommit; latch resets in InitializeMemory.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_RusherStandoff.generated.h"

class AEnemyCharacter;

UCLASS()
class EXTRACTION_API UBTDecorator_RusherStandoff : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_RusherStandoff();

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FStandoffMemory
	{
		// Hysteresis latch — true while inside the standoff band for the current engagement.
		bool bCircling = false;

		// Last value returned by CalculateRawConditionValue so TickNode can detect changes.
		bool bLastCondition = false;

		// Cached character — resolve-on-miss, reset in InitializeMemory/CleanupMemory.
		TWeakObjectPtr<AEnemyCharacter> CachedEnemy;
	};

	// Resolves and caches AEnemyCharacter from the controller pawn. Returns nullptr on failure.
	AEnemyCharacter* ResolveEnemy(UBehaviorTreeComponent& OwnerComp, FStandoffMemory* Mem) const;
};
