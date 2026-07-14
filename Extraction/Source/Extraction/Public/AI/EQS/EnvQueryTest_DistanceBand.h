// EQS test -- scores/filters items by 2D distance with a BAND preference (peak inside [BandMin, BandMax]).

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "DataProviders/AIDataProvider.h"
#include "EnvQueryTest_DistanceBand.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryTest_DistanceBand : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_DistanceBand();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionTitle() const override;
	virtual FText GetDescriptionDetails() const override;

	/** Context to measure 2D distance against. Multiple context locations: uses minimum distance. */
	UPROPERTY(EditDefaultsOnly, Category = "Distance")
	TSubclassOf<UEnvQueryContext> DistanceTo;

	/** Inner edge of the preferred band (cm). */
	UPROPERTY(EditAnywhere, Category = "Distance")
	FAIDataProviderFloatValue BandMin;

	/** Outer edge of the preferred band (cm). */
	UPROPERTY(EditAnywhere, Category = "Distance")
	FAIDataProviderFloatValue BandMax;
};
