// BT decorator — gates the flanker branch: passes only while squad bounding is active AND suppression is live.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_SuppressionLive.generated.h"

class UEnemySquad;

UCLASS()
class EXTRACTION_API UBTDecorator_SuppressionLive : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_SuppressionLive();

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual uint16 GetInstanceMemorySize() const override;
	virtual FString GetStaticDescription() const override;

private:

	struct FSuppressionLiveMemory
	{
		bool bLastCondition = false;
		TWeakObjectPtr<UEnemySquad> CachedSquad;
	};

	UEnemySquad* ResolveSquad(UBehaviorTreeComponent& OwnerComp, FSuppressionLiveMemory* Mem) const;
};
