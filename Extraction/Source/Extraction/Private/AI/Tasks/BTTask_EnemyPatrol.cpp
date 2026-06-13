// BTTask_EnemyPatrol — walks the APatrolRoute assigned in BB, loops or ping-pongs.

#include "BTTask_EnemyPatrol.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "PatrolRoute.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"

UBTTask_EnemyPatrol::UBTTask_EnemyPatrol()
{
	NodeName = TEXT("Enemy Patrol");
	bNotifyTick = true;
}

uint16 UBTTask_EnemyPatrol::GetInstanceMemorySize() const
{
	return sizeof(FPatrolMemory);
}

EBTNodeResult::Type UBTTask_EnemyPatrol::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FPatrolMemory* Mem = reinterpret_cast<FPatrolMemory*>(NodeMemory);
	new (Mem) FPatrolMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	APatrolRoute* Route = Cast<APatrolRoute>(BB->GetValueAsObject(AEnemyAIController::BB_PatrolRoute));

	// Set patrol speed
	if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn))
		Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Patrol);

	// No route: guard post — stay InProgress (idle); BT aborts when combat triggers
	if (!IsValid(Route) || Route->NumPoints() == 0)
		return EBTNodeResult::InProgress;

	const FVector Goal = Route->GetWorldPoint(Mem->CurrentIndex);
	if (FVector::Dist(Pawn->GetActorLocation(), Goal) <= 50.f)
	{
		// Already at the first point — start waiting
		Mem->bWaiting = true;
		return EBTNodeResult::InProgress;
	}

	Controller->MoveToLocation(Goal, 50.f, false, true, false, true);
	Mem->bMoveIssued = true;
	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyPatrol::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FPatrolMemory* Mem = reinterpret_cast<FPatrolMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	APatrolRoute* Route = Cast<APatrolRoute>(BB->GetValueAsObject(AEnemyAIController::BB_PatrolRoute));
	if (!IsValid(Route) || Route->NumPoints() == 0)
		return; // guard post — idle until BT aborts

	// Wait phase
	if (Mem->bWaiting)
	{
		Mem->WaitElapsed += DeltaSeconds;
		if (Mem->WaitElapsed >= Route->WaitAtPointSeconds)
		{
			Mem->bWaiting = false;
			Mem->WaitElapsed = 0.f;
			AdvanceIndex(*Mem, *Route);

			const FVector NextGoal = Route->GetWorldPoint(Mem->CurrentIndex);
			Controller->MoveToLocation(NextGoal, 50.f, false, true, false, true);
			Mem->bMoveIssued = true;
		}
		return;
	}

	// Move phase
	if (!Mem->bMoveIssued) return;

	UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
	if (!PF) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const FVector Goal = Route->GetWorldPoint(Mem->CurrentIndex);
	const float DistToGoal = FVector::Dist(Pawn->GetActorLocation(), Goal);

	const EPathFollowingStatus::Type Status = PF->GetStatus();
	if (Status == EPathFollowingStatus::Idle || DistToGoal <= 50.f)
	{
		Mem->bMoveIssued = false;
		Mem->bWaiting = true;
		Mem->WaitElapsed = 0.f;
	}
}

EBTNodeResult::Type UBTTask_EnemyPatrol::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AAIController* Controller = OwnerComp.GetAIOwner())
		Controller->StopMovement();
	return EBTNodeResult::Aborted;
}

void UBTTask_EnemyPatrol::AdvanceIndex(FPatrolMemory& Mem, const APatrolRoute& Route) const
{
	const int32 Num = Route.NumPoints();
	if (Num <= 1) return;

	if (Route.bLoop)
	{
		Mem.CurrentIndex = (Mem.CurrentIndex + 1) % Num;
	}
	else
	{
		// Ping-pong
		Mem.CurrentIndex += Mem.Direction;
		if (Mem.CurrentIndex >= Num - 1) { Mem.CurrentIndex = Num - 1; Mem.Direction = -1; }
		else if (Mem.CurrentIndex <= 0)  { Mem.CurrentIndex = 0;       Mem.Direction =  1; }
	}
}

FString UBTTask_EnemyPatrol::GetStaticDescription() const
{
	return TEXT("Walk patrol route from BB_PatrolRoute (loop/ping-pong)");
}
