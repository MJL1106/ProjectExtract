// EQS scoring test -- scores cover items with the SAME shared formula the C++ picker paths use
// (UCoverScoringStatics::ScoreCandidate), with weights resolved from the querier's archetype
// DataAsset at run time. Replaces the old per-test scoring spread (side-flag / distance /
// parallel scoring) so EQS-picked and C++-picked covers agree.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_CoverScore.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryTest_CoverScore : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_CoverScore();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionDetails() const override;

	/** Context providing the threat location. Default: CombatTarget. */
	UPROPERTY(EditAnywhere, Category = "CoverScore")
	TSubclassOf<UEnvQueryContext> TargetContext;
};
