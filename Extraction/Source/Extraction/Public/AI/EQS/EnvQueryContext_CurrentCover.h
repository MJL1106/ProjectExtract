// EQS context — provides the companion's currently-held cover location (vector) from the blackboard.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_CurrentCover.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryContext_CurrentCover : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
