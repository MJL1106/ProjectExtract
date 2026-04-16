// BT task — runs EQS query for cover, moves companion to best position.

#include "BTTask_MoveToCover.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "EnvironmentQuery/EnvQueryManager.h"

UBTTask_MoveToCover::UBTTask_MoveToCover()
{
	NodeName = TEXT("Move To Cover");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_MoveToCover::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	// If we already have a valid cover position, skip the query
	if (BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName))
		return EBTNodeResult::InProgress;

	// No cover query assigned — skip cover, go straight to combat
	if (!CoverQuery)
		return EBTNodeResult::Succeeded;

	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!Controller || !Controller->GetPawn()) return EBTNodeResult::Failed;

	CachedOwnerComp = &OwnerComp;
	bQueryInProgress = true;

	FEnvQueryRequest QueryRequest(CoverQuery, Controller->GetPawn());
	QueryRequest.Execute(EEnvQueryRunMode::SingleResult,
		FQueryFinishedSignature::CreateUObject(this, &UBTTask_MoveToCover::OnQueryFinished));

	return EBTNodeResult::InProgress;
}

void UBTTask_MoveToCover::OnQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	bQueryInProgress = false;

	if (!CachedOwnerComp) return;

	UBlackboardComponent* BB = CachedOwnerComp->GetBlackboardComponent();
	if (!BB) return;

	if (Result.IsValid() && Result->IsSuccessful() && Result->Items.Num() > 0)
	{
		const FVector CoverLoc = Result->GetItemAsLocation(0);
		BB->SetValueAsVector(CoverLocationKey.SelectedKeyName, CoverLoc);
		BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, true);
	}
	else
	{
		// No cover found — companion will engage from current position
		BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
	}
}

void UBTTask_MoveToCover::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	// Waiting for EQS query
	if (bQueryInProgress) return;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// No cover found — skip to combat
	if (!BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	// Target gone — abort
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	if (!IsValid(Target))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!Controller || !Controller->GetPawn())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const FVector CoverLoc = BB->GetValueAsVector(CoverLocationKey.SelectedKeyName);
	const float DistToCover = FVector::Dist(Controller->GetPawn()->GetActorLocation(), CoverLoc);

	// Arrived at cover
	if (DistToCover <= AcceptableRadius)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	Controller->MoveToLocation(CoverLoc, AcceptableRadius, false, true, false, true);
}

FString UBTTask_MoveToCover::GetStaticDescription() const
{
	return FString::Printf(TEXT("Find cover via EQS (radius: %.0f)"), AcceptableRadius);
}
