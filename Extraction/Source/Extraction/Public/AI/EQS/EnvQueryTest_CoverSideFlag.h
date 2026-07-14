// EQS scoring test -- scores cover items by whether they carry a stance-matched side flag
// on the threat-facing side.  Threat-facing = 1.0, opposite-only = OppositeSideScore, none = 0.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "DataProviders/AIDataProvider.h"
#include "EnvQueryTest_CoverSideFlag.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryTest_CoverSideFlag : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_CoverSideFlag();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionDetails() const override;

	/** Context providing the threat location (direction source).  Default: CombatTarget. */
	UPROPERTY(EditAnywhere, Category = "SideFlag")
	TSubclassOf<UEnvQueryContext> TargetContext;

	/** Score returned when the item has only the opposite-side flag (no threat-facing flag). */
	UPROPERTY(EditAnywhere, Category = "SideFlag")
	FAIDataProviderFloatValue OppositeSideScore;
};
