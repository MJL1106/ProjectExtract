// EQS filter — rejects cover items whose straight line from the querier crosses a CLOSED door.
// A candidate behind a closed door means retreating out of the fight space (auto-opening the
// door en route, or shoving against it when locked) — never a valid pick. Open doors pass.
// Pure math via UDoorRegistrySubsystem::AnyClosedDoorBlocksSegment — no traces.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_CoverDoorCrossing.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryTest_CoverDoorCrossing : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_CoverDoorCrossing();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionDetails() const override;
};
