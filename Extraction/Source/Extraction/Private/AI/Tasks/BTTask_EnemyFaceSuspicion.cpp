// BTTask_EnemyFaceSuspicion — stop, face the stimulus, hold post while the meter decides.

#include "BTTask_EnemyFaceSuspicion.h"
#include "EnemyAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"

UBTTask_EnemyFaceSuspicion::UBTTask_EnemyFaceSuspicion()
{
	NodeName = TEXT("Enemy Face Suspicion");
	bNotifyTick = true;
}

uint16 UBTTask_EnemyFaceSuspicion::GetInstanceMemorySize() const
{
	return sizeof(FFaceSuspicionMemory);
}

EBTNodeResult::Type UBTTask_EnemyFaceSuspicion::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FFaceSuspicionMemory* Mem = reinterpret_cast<FFaceSuspicionMemory*>(NodeMemory);
	new (Mem) FFaceSuspicionMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!BB || !Controller) return EBTNodeResult::Failed;

	Controller->StopMovement();

	Mem->FocalPoint = BB->GetValueAsVector(AEnemyAIController::BB_InvestigateLocation);
	Controller->SetFocalPoint(Mem->FocalPoint, EAIFocusPriority::Gameplay);

	// Latent hold — the Suspicious decorator (observer aborts) owns the exit.
	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyFaceSuspicion::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FFaceSuspicionMemory* Mem = reinterpret_cast<FFaceSuspicionMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!BB || !Controller) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const FVector Latest = BB->GetValueAsVector(AEnemyAIController::BB_InvestigateLocation);
	if (!Latest.Equals(Mem->FocalPoint, 50.f))
	{
		Mem->FocalPoint = Latest;
		Controller->SetFocalPoint(Latest, EAIFocusPriority::Gameplay);
	}
}

EBTNodeResult::Type UBTTask_EnemyFaceSuspicion::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AAIController* Controller = OwnerComp.GetAIOwner())
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	return EBTNodeResult::Aborted;
}

FString UBTTask_EnemyFaceSuspicion::GetStaticDescription() const
{
	return TEXT("Hold post and face BB_InvestigateLocation while Suspicious");
}
