// EQS filter — rejects cover items with no usable exposure direction for the querier's stance.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_CoverPeekable.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryTest_CoverPeekable : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_CoverPeekable();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionDetails() const override;
};
