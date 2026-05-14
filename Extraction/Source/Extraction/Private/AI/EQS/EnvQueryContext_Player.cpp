// EQS context — provides the human player actor from the companion's blackboard.

#include "EnvQueryContext_Player.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h"

void UEnvQueryContext_Player::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	const AActor* QuerierActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QuerierActor) return;

	const APawn* QuerierPawn = Cast<APawn>(QuerierActor);
	if (!QuerierPawn) return;

	const ACompanionAIController* Controller = Cast<ACompanionAIController>(QuerierPawn->GetController());
	if (!Controller) return;

	const UBlackboardComponent* BB = Controller->GetBlackboardComponent();
	if (!BB) return;

	AActor* PlayerActor = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_PlayerActor));
	if (IsValid(PlayerActor))
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, PlayerActor);
	}
}
