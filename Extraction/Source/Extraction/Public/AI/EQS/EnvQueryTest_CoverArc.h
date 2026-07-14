// EQS filter — rejects cover items whose fire arc does not face the combat target.
// Computes dot(GetFireArcForward(Data), direction-to-context) and filters/scores by ArcHalfAngleDeg.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "DataProviders/AIDataProvider.h"
#include "EnvQueryTest_CoverArc.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryTest_CoverArc : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_CoverArc();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionDetails() const override;

	/** Context providing the target location (direction-to).  Default: CombatTarget. */
	UPROPERTY(EditAnywhere, Category = "Arc")
	TSubclassOf<UEnvQueryContext> TargetContext;

	/** Half-angle (degrees) of the acceptable fire arc.  75 = 60 base + 15 slack. */
	UPROPERTY(EditAnywhere, Category = "Arc", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	FAIDataProviderFloatValue ArcHalfAngleDeg;
};
