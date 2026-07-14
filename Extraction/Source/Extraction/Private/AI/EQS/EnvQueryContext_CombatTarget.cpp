// EQS context — provides the querier's current combat target from the blackboard.
// Works for both enemy and companion AI controllers.
// Enemy queriers with LOS lost get the frozen last-known POINT instead of the live actor
// (honest knowledge — cover queries must not track a hidden player through walls).

#include "EnvQueryContext_CombatTarget.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "EnemyAIController.h"
#include "EnemyAwarenessComponent.h"
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
	if (!IsValid(CombatTarget)) return;

	if (const AEnemyAIController* EnemyController = Cast<AEnemyAIController>(Controller))
	{
		const UEnemyAwarenessComponent* Awareness = EnemyController->GetAwarenessComponent();
		if (IsValid(Awareness) && Awareness->GetCombatTarget() == CombatTarget && !Awareness->HasLOSToTarget())
		{
			UEnvQueryItemType_Point::SetContextHelper(ContextData, Awareness->GetLastKnownLocation());
			return;
		}
	}

	UEnvQueryItemType_Actor::SetContextHelper(ContextData, CombatTarget);
}
