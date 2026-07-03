// EQS context — provides the querier's current combat target from the blackboard.
// Works for both enemy and companion AI controllers.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_CombatTarget.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryContext_CombatTarget : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
