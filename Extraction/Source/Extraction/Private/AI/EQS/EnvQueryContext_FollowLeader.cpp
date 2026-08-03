// EQS context — provides the actor the querying companion forms up on: the player for a primary
// companion, the primary companion for the armed extractee VIP.

#include "EnvQueryContext_FollowLeader.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "AIController.h"
#include "CompanionAIController.h"

void UEnvQueryContext_FollowLeader::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	const AActor* QuerierActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QuerierActor) return;

	const APawn* QuerierPawn = Cast<APawn>(QuerierActor);
	if (!QuerierPawn) return;

	const ACompanionAIController* Controller = Cast<ACompanionAIController>(QuerierPawn->GetController());
	if (!Controller) return;

	// Read through the controller rather than off BB_FollowLeader directly. The mirror key is authored
	// in-engine and may not exist yet; the accessor already owns the player fallback, and it re-tests a
	// downed leader at read time, which a raw blackboard read cannot. A PRIMARY companion's leader is
	// the player, so this hands back exactly the actor UEnvQueryContext_Player would.
	AActor* Leader = Controller->GetFollowLeader();
	if (IsValid(Leader))
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, Leader);
	}
}
