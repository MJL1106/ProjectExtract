// EQS filter — rejects covers claimed by someone else: intended-by-other (any side, mirrors the
// C++ picker loops) or within MinHostileDistance of a hostile pawn / hostile-intended cover.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "DataProviders/AIDataProvider.h"
#include "EnvQueryTest_CoverIntent.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryTest_CoverIntent : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_CoverIntent();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionDetails() const override;

	/** Min 2D distance (cm) from a cover a hostile is moving to (declared intent). Kills claim
	 *  collisions with the companion. Enemy queriers read the archetype DA instead (MinHostileCoverDistance);
	 *  this is the companion/fallback value. 0 = off. */
	UPROPERTY(EditAnywhere, Category = "Test")
	FAIDataProviderFloatValue MinHostileDistance;

	/** Min 2D distance (cm) from a living hostile pawn. Smaller than the cover radius so tight
	 *  rooms don't starve the query. Enemy queriers read the archetype DA (MinHostilePawnDistance). 0 = off. */
	UPROPERTY(EditAnywhere, Category = "Test")
	FAIDataProviderFloatValue MinHostilePawnDistance;
};
