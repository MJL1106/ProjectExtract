// BT decorator — skips the decorated branch while ManeuverHoldUntil is in the future
// (grunt is in its post-maneuver engage-in-place window).

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_RecentManeuverHold.generated.h"

UCLASS()
class EXTRACTION_API UBTDecorator_RecentManeuverHold : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_RecentManeuverHold();

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;
};
