// EQS context — provides the companion's current combat target from the blackboard.

#include "EnvQueryContext_CombatTarget.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h"

void UEnvQueryContext_CombatTarget::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	const AActor* QuerierActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QuerierActor) return;

	const APawn* QuerierPawn = Cast<APawn>(QuerierActor);
	if (!QuerierPawn) return;

	const ACompanionAIController* Controller = Cast<ACompanionAIController>(QuerierPawn->GetController());
	if (!Controller) return;

	const UBlackboardComponent* BB = Controller->GetBlackboardComponent();
	if (!BB) return;

	AActor* CombatTarget = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget));
	if (IsValid(CombatTarget))
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, CombatTarget);
	}
}
