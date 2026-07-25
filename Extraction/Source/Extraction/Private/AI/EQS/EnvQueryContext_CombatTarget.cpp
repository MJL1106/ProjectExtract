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

	const AEnemyAIController* EnemyController = Cast<AEnemyAIController>(Controller);
	const UEnemyAwarenessComponent* Awareness = EnemyController ? EnemyController->GetAwarenessComponent() : nullptr;

	if (!IsValid(CombatTarget))
	{
		// Providing NO context is not neutral — every consumer (ProvidesCover's occlusion proof,
		// CoverArc, ParallelToCover, the distance band) bails out having filtered nothing, which
		// degrades the cover query to "any unoccupied peekable point" and walks the enemy into open
		// ground behind nothing. The null-target window opens every time a target dies, goes DBNO, or
		// is dropped between awareness ticks — i.e. exactly when enemies are re-seeking cover.
		// Fall back to the remembered threat point so the geometry tests still have something to
		// filter against. Only a genuinely threat-less enemy (Unaware) gets an empty context.
		if (IsValid(Awareness) && Awareness->GetAwarenessState() >= EEnemyAwarenessState::Searching)
		{
			const FVector LastKnown = Awareness->GetLastKnownLocation();
			if (!LastKnown.IsNearlyZero())
				UEnvQueryItemType_Point::SetContextHelper(ContextData, LastKnown);
		}
		return;
	}

	if (IsValid(Awareness) && Awareness->GetCombatTarget() == CombatTarget && !Awareness->HasLOSToTarget())
	{
		UEnvQueryItemType_Point::SetContextHelper(ContextData, Awareness->GetLastKnownLocation());
		return;
	}

	UEnvQueryItemType_Actor::SetContextHelper(ContextData, CombatTarget);
}
