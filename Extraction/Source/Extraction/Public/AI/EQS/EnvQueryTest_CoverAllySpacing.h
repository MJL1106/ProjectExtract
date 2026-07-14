// EQS test — scores/filters cover items by minimum 2D distance to squad ally positions + intended covers.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "DataProviders/AIDataProvider.h"
#include "EnvQueryTest_CoverAllySpacing.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryTest_CoverAllySpacing : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_CoverAllySpacing();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionDetails() const override;

	/** Minimum spacing (cm) from any ally position. Items closer than this score 0.2; farther score 1.0. */
	UPROPERTY(EditAnywhere, Category = "Test")
	FAIDataProviderFloatValue MinSpacing;
};
