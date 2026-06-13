// EQS context — provides the companion's current combat target from the blackboard.

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
