// BTTask_EnemyPatrol — walks the APatrolRoute assigned in BB, loops or ping-pongs.

#include "BTTask_EnemyPatrol.h"
#include "CoreGlobals.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "PatrolRoute.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"

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

	UE_LOG(LogEnemyAI, Warning, TEXT("[PATROL] %s ExecuteTask: route=%s points=%d"),
		*Pawn->GetName(), Route ? *Route->GetName() : TEXT("NULL"),
		IsValid(Route) ? Route->NumPoints() : -1);

	// Nav diagnostic: does the RUNTIME nav system have data for this pawn, here, now?
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Pawn->GetWorld()))
	{
		const FNavAgentProperties& Agent = Pawn->GetNavAgentPropertiesRef();
		const ANavigationData* NavData = NavSys->GetNavDataForProps(Agent, Pawn->GetActorLocation());
		FNavLocation Projected;
		const bool bOnNav = NavSys->ProjectPointToNavigation(Pawn->GetActorLocation(), Projected, FVector(200.f, 200.f, 500.f));
		UE_LOG(LogEnemyAI, Warning, TEXT("[NAV] %s agentR=%.1f agentH=%.1f NavDataForProps=%s onRuntimeNav=%d"),
			*Pawn->GetName(), Agent.AgentRadius, Agent.AgentHeight, *GetNameSafe(NavData), bOnNav ? 1 : 0);
	}
	else
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("[NAV] %s NavigationSystem is NULL at runtime"), *Pawn->GetName());
	}

	// Set patrol speed
	if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn))
		Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Patrol);

	// No route: guard post — stay InProgress (idle); BT aborts when combat triggers
	if (!IsValid(Route) || Route->NumPoints() == 0)
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("[PATROL] %s no route/points -> guard post (idle)"), *Pawn->GetName());
		return EBTNodeResult::InProgress;
	}

	const FVector Goal = Route->GetWorldPoint(Mem->CurrentIndex);
	const float StartDist = FVector::Dist(Pawn->GetActorLocation(), Goal);
	if (StartDist <= 50.f)
	{
		// Already at the first point — start waiting
		UE_LOG(LogEnemyAI, Warning, TEXT("[PATROL] %s already at P%d (dist %.0f) -> wait"),
			*Pawn->GetName(), Mem->CurrentIndex, StartDist);
		Mem->bWaiting = true;
		return EBTNodeResult::InProgress;
	}

	const EPathFollowingRequestResult::Type MoveResult = Controller->MoveToLocation(Goal, 50.f, false, true, false, true);
	UE_LOG(LogEnemyAI, Warning, TEXT("[PATROL] %s MoveTo P%d (%.0f,%.0f,%.0f) dist=%.0f result=%d (0=Failed 1=AlreadyAtGoal 2=RequestSuccessful)"),
		*Pawn->GetName(), Mem->CurrentIndex, Goal.X, Goal.Y, Goal.Z, StartDist, (int32)MoveResult);
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
			const EPathFollowingRequestResult::Type MoveResult = Controller->MoveToLocation(NextGoal, 50.f, false, true, false, true);
			UE_LOG(LogEnemyAI, Warning, TEXT("[PATROL] %s advance -> P%d (%.0f,%.0f,%.0f) result=%d"),
				*Pawn->GetName(), Mem->CurrentIndex, NextGoal.X, NextGoal.Y, NextGoal.Z, (int32)MoveResult);
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

	// Heartbeat (~2x/sec) so we can see whether the pawn is actually translating while a move is active.
	if ((GFrameCounter % 30) == 0)
		UE_LOG(LogEnemyAI, Warning, TEXT("[PATROL] %s moving -> P%d status=%d (0=Idle 3=Moving) dist=%.0f vel=%.0f"),
			*Pawn->GetName(), Mem->CurrentIndex, (int32)Status, DistToGoal, Pawn->GetVelocity().Size());

	if (Status == EPathFollowingStatus::Idle || DistToGoal <= 50.f)
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("[PATROL] %s reached/idle P%d status=%d dist=%.0f vel=%.0f -> wait"),
			*Pawn->GetName(), Mem->CurrentIndex, (int32)Status, DistToGoal, Pawn->GetVelocity().Size());
		Mem->bMoveIssued = false;
		Mem->bWaiting = true;
		Mem->WaitElapsed = 0.f;
	}
}

EBTNodeResult::Type UBTTask_EnemyPatrol::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AAIController* Controller = OwnerComp.GetAIOwner())
	{
		if (const APawn* P = Controller->GetPawn())
			UE_LOG(LogEnemyAI, Warning, TEXT("[PATROL] %s AbortTask -> StopMovement (branch interrupted)"), *P->GetName());
		Controller->StopMovement();
	}
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
