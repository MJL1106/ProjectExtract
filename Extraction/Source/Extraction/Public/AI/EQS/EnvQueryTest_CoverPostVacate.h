// EQS filter — rejects cover items still on post-vacate cooldown for the querier.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "DataProviders/AIDataProvider.h"
#include "EnvQueryTest_CoverPostVacate.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryTest_CoverPostVacate : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_CoverPostVacate();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionDetails() const override;

	/** Seconds after vacating a cover before it becomes eligible again. */
	UPROPERTY(EditAnywhere, Category = "Test")
	FAIDataProviderFloatValue CooldownSeconds;
};
