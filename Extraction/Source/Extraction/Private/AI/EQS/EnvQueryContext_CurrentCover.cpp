// EQS context — provides the companion's currently-held cover location (vector) from the blackboard.

#include "EnvQueryContext_CurrentCover.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h"

void UEnvQueryContext_CurrentCover::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	const AActor* QuerierActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QuerierActor) return;

	const APawn* QuerierPawn = Cast<APawn>(QuerierActor);
	if (!QuerierPawn) return;

	const ACompanionAIController* Controller = Cast<ACompanionAIController>(QuerierPawn->GetController());
	if (!Controller) return;

	const UBlackboardComponent* BB = Controller->GetBlackboardComponent();
	if (!BB) return;

	const bool bHasCover = BB->GetValueAsBool(ACompanionAIController::BB_HasCoverPosition);
	if (!bHasCover) return;

	const FVector CoverLocation = BB->GetValueAsVector(ACompanionAIController::BB_CoverLocation);
	UEnvQueryItemType_Point::SetContextHelper(ContextData, CoverLocation);
}
