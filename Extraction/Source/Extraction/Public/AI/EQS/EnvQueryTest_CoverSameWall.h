// EQS filter — rejects cover items not on the same wall as the querier's current CoverTarget BB key.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_CoverSameWall.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryTest_CoverSameWall : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_CoverSameWall();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionDetails() const override;

protected:

	/** Angular tolerance (degrees) for the DirectionToWall dot product same-wall test. */
	UPROPERTY(EditAnywhere, Category = "Test", meta = (ClampMin = "1.0", ClampMax = "90.0"))
	float ToleranceDeg = 20.f;

	/** Maximum lateral offset (cm) from the reference along the wall normal before rejection. */
	UPROPERTY(EditAnywhere, Category = "Test", meta = (ClampMin = "1.0"))
	float NormalBandTolerance = 60.f;

	/** Blackboard key name for the reference cover (Cover type via AICS plugin). */
	UPROPERTY(EditAnywhere, Category = "Test")
	FName CoverTargetKeyName = TEXT("CoverTarget");
};
