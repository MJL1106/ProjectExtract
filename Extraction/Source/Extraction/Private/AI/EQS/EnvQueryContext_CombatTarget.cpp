// EQS context — provides the querier's current combat target from the blackboard.
// Works for both enemy and companion AI controllers.

#include "EnvQueryContext_CombatTarget.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "EnemyAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"

void UEnvQueryContext_CombatTarget::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	const APawn* QuerierPawn = Cast<APawn>(QueryInstance.Owner.Get());
	if (!QuerierPawn) return;

	const AAIController* Controller = Cast<AAIController>(QuerierPawn->GetController());
	if (!Controller) return;

	const UBlackboardComponent* BB = Controller->GetBlackboardComponent();
	if (!BB) return;

	// Single source of truth: AEnemyAIController::BB_CombatTarget (both enemy and companion use the same literal)
	AActor* CombatTarget = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (IsValid(CombatTarget))
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, CombatTarget);
	}
}
