// EQS context — provides the actor the querying companion forms up on: the player for a primary
// companion, the primary companion for the armed extractee VIP.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_FollowLeader.generated.h"

UCLASS()
class EXTRACTION_API UEnvQueryContext_FollowLeader : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
