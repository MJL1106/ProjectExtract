// BTTask_EnemyPatrol — walks the APatrolRoute assigned in BB, loops or ping-pongs.
// Route-less enemies return to their guard post then perform a yaw sweep.

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

void UBTTask_EnemyPatrol::BeginGuardScan(FPatrolMemory& Mem, float BaseYaw) const
{
	Mem.GuardPhase = EGuardPhase::Scan;
	Mem.bGuardScanActive = true;
	Mem.GuardBaseYaw = BaseYaw;
	Mem.GuardSweepTimer = 0.f;
	Mem.GuardSweepSegment = 0;
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
	Mem->CachedRoute = Route;

	UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s ExecuteTask: route=%s points=%d"),
		*Pawn->GetName(), Route ? *Route->GetName() : TEXT("NULL"),
		IsValid(Route) ? Route->NumPoints() : -1);

	// Nav diagnostic: does the RUNTIME nav system have data for this pawn, here, now?
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Pawn->GetWorld()))
	{
		const FNavAgentProperties& Agent = Pawn->GetNavAgentPropertiesRef();
		const ANavigationData* NavData = NavSys->GetNavDataForProps(Agent, Pawn->GetActorLocation());
		FNavLocation Projected;
		const bool bOnNav = NavSys->ProjectPointToNavigation(Pawn->GetActorLocation(), Projected, FVector(200.f, 200.f, 500.f));
		UE_LOG(LogEnemyAI, Verbose, TEXT("[NAV] %s agentR=%.1f agentH=%.1f NavDataForProps=%s onRuntimeNav=%d"),
			*Pawn->GetName(), Agent.AgentRadius, Agent.AgentHeight, *GetNameSafe(NavData), bOnNav ? 1 : 0);
	}
	else
	{
		UE_LOG(LogEnemyAI, Verbose, TEXT("[NAV] %s NavigationSystem is NULL at runtime"), *Pawn->GetName());
	}

	// Set patrol speed
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	Mem->CachedEnemy = Enemy;
	Mem->CachedDA = IsValid(Enemy) ? Enemy->GetArchetypeData() : nullptr;
	if (IsValid(Enemy))
		Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Patrol);

	// No route: guard post — return to post, then sweep
	if (!IsValid(Route) || Route->NumPoints() == 0)
	{
		Controller->ClearFocus(EAIFocusPriority::Gameplay);

		// Read post location from BB; fallback to pawn API if the key is unset (zero vector)
		FVector PostLocation = BB->GetValueAsVector(AEnemyAIController::BB_PostLocation);
		if (PostLocation.IsZero() && IsValid(Enemy))
			PostLocation = Enemy->GetGuardPostLocation();

		const float DistToPost = FVector::Dist(Pawn->GetActorLocation(), PostLocation);
		UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s no route -> guard post at (%.0f,%.0f,%.0f) dist=%.0f"),
			*Pawn->GetName(), PostLocation.X, PostLocation.Y, PostLocation.Z, DistToPost);

		if (DistToPost <= GuardPostAcceptanceRadius)
		{
			// Already at post — go straight to scan
			const float BaseYaw = IsValid(Enemy) ? Enemy->GetInitialPostYaw() : Pawn->GetActorRotation().Yaw;
			BeginGuardScan(*Mem, BaseYaw);
			UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s already at post -> scan baseYaw=%.0f"), *Pawn->GetName(), Mem->GuardBaseYaw);
		}
		else
		{
			// Issue move to post — project to nav so an off-mesh override snaps to the nearest point
			Mem->GuardPhase = EGuardPhase::Return;
			const EPathFollowingRequestResult::Type MoveResult = Controller->MoveToLocation(
				PostLocation, GuardPostAcceptanceRadius, false, true, true, true);
			Mem->bMoveIssued = true;
			UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s returning to post result=%d"), *Pawn->GetName(), (int32)MoveResult);

			if (MoveResult == EPathFollowingRequestResult::Failed)
			{
				// Path unavailable — degrade to in-place scan rather than looping failure
				const float BaseYaw = IsValid(Enemy) ? Enemy->GetInitialPostYaw() : Pawn->GetActorRotation().Yaw;
				BeginGuardScan(*Mem, BaseYaw);
				UE_LOG(LogEnemyAI, Warning, TEXT("[PATROL] %s MoveToPost failed -> scan in place"), *Pawn->GetName());
			}
		}

		return EBTNodeResult::InProgress;
	}

	// For Shuffle, pick a random starting point; Loop/PingPong always start at index 0.
	// CurrentIndex was zeroed by placement-new above, so Loop/PingPong need no change here.
	if (Route->PatrolOrder == EPatrolOrder::Shuffle && Route->NumPoints() > 1)
		Mem->CurrentIndex = FMath::RandRange(0, Route->NumPoints() - 1);

	// Resolve and check the starting goal (may be jittered for wander)
	Mem->CurrentGoal = ResolveGoal(*Pawn, *Route, Mem->CurrentIndex);
	const float StartDist = FVector::Dist(Pawn->GetActorLocation(), Mem->CurrentGoal);

	if (StartDist <= 50.f)
	{
		// Already at the starting point — enter wait immediately
		UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s already at P%d (dist %.0f) -> wait"),
			*Pawn->GetName(), Mem->CurrentIndex, StartDist);
		Mem->bWaiting = true;
		RollWaitTarget(*Mem, *Route);
		Mem->GuardBaseYaw = Pawn->GetActorRotation().Yaw;
		Mem->GuardSweepTimer = 0.f;
		Mem->GuardSweepSegment = 0;
		return EBTNodeResult::InProgress;
	}

	IssueMoveToCurrentGoal(*Controller, *Pawn, *Mem, *Route);
	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyPatrol::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FPatrolMemory* Mem = reinterpret_cast<FPatrolMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	APatrolRoute* Route = Mem->CachedRoute.Get();
	if (!IsValid(Route) || Route->NumPoints() == 0)
	{
		// ---- Guard post tick ----

		if (Mem->GuardPhase == EGuardPhase::Return)
		{
			// Waiting for the move-to-post to complete
			UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
			if (!PF) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

			Mem->ReturnElapsed += DeltaSeconds;

			const AEnemyCharacter* Enemy = Mem->CachedEnemy.Get();
			FVector PostLocation = BB->GetValueAsVector(AEnemyAIController::BB_PostLocation);
			if (PostLocation.IsZero() && IsValid(Enemy))
				PostLocation = Enemy->GetGuardPostLocation();

			const float DistToPostSq = FVector::DistSquared(Pawn->GetActorLocation(), PostLocation);
			const EPathFollowingStatus::Type Status = PF->GetStatus();

			const bool bArrived = Status == EPathFollowingStatus::Idle || DistToPostSq <= FMath::Square(GuardPostAcceptanceRadius);
			const bool bTimedOut = Mem->ReturnElapsed >= GuardReturnTimeout;

			if (bArrived || bTimedOut)
			{
				if (bTimedOut && !bArrived)
				{
					UE_LOG(LogEnemyAI, Warning, TEXT("[PATROL] %s return-to-post timed out (%.1fs) -> scan in place"), *Pawn->GetName(), Mem->ReturnElapsed);
				}
				else
				{
					UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s reached post (distSq=%.0f status=%d) -> scan baseYaw=%.0f"),
						*Pawn->GetName(), DistToPostSq, (int32)Status, Mem->GuardBaseYaw);
				}

				const float BaseYaw = IsValid(Enemy) ? Enemy->GetInitialPostYaw() : Pawn->GetActorRotation().Yaw;
				BeginGuardScan(*Mem, BaseYaw);
				Controller->ClearFocus(EAIFocusPriority::Gameplay);
			}
			return;
		}

		// ---- Scan phase ----
		if (!Mem->bGuardScanActive) return;

		const AEnemyCharacter* Enemy = Mem->CachedEnemy.Get();
		const UEnemyArchetypeData* DA = Mem->CachedDA.Get();
		if (!IsValid(DA) || DA->GuardScanYawRange <= 0.f) return;

		TickYawSweep(*Controller, *Pawn, *DA, Mem->GuardBaseYaw,
			Mem->GuardSweepTimer, Mem->GuardSweepSegment, DeltaSeconds);
		return;
	}

	// ---- Routed patrol tick ----

	// Wait phase
	if (Mem->bWaiting)
	{
		// Loiter glance: use guard sweep fields reused for routed loiter
		const AEnemyCharacter* LoiterEnemy = Mem->CachedEnemy.Get();
		const UEnemyArchetypeData* LoiterDA = Mem->CachedDA.Get();
		if (IsValid(LoiterDA) && LoiterDA->GuardScanYawRange > 0.f)
		{
			TickYawSweep(*Controller, *Pawn, *LoiterDA, Mem->GuardBaseYaw,
				Mem->GuardSweepTimer, Mem->GuardSweepSegment, DeltaSeconds);
		}

		Mem->WaitElapsed += DeltaSeconds;
		if (Mem->WaitElapsed >= Mem->WaitTarget)
		{
			Mem->bWaiting = false;
			Mem->WaitElapsed = 0.f;
			PickNextIndex(*Mem, *Route);
			IssueMoveToCurrentGoal(*Controller, *Pawn, *Mem, *Route);
		}
		return;
	}

	// Move phase
	if (!Mem->bMoveIssued) return;

	UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
	if (!PF) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Use the stored goal — not GetWorldPoint — so wander doesn't re-roll each tick
	const float DistToGoalSq = FVector::DistSquared(Pawn->GetActorLocation(), Mem->CurrentGoal);

	const EPathFollowingStatus::Type Status = PF->GetStatus();

	if ((GFrameCounter % 30) == 0)
		UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s moving -> P%d status=%d (0=Idle 3=Moving) distSq=%.0f vel=%.0f"),
			*Pawn->GetName(), Mem->CurrentIndex, (int32)Status, DistToGoalSq, Pawn->GetVelocity().Size());

	if (Status == EPathFollowingStatus::Idle || DistToGoalSq <= FMath::Square(50.f))
	{
		UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s reached/idle P%d status=%d distSq=%.0f vel=%.0f -> wait"),
			*Pawn->GetName(), Mem->CurrentIndex, (int32)Status, DistToGoalSq, Pawn->GetVelocity().Size());
		Mem->bMoveIssued = false;
		Mem->bWaiting = true;
		Mem->WaitElapsed = 0.f;

		// Roll randomised wait duration
		RollWaitTarget(*Mem, *Route);

		// Capture current yaw as base for loiter glance; reset sweep state
		Mem->GuardBaseYaw = Pawn->GetActorRotation().Yaw;
		Mem->GuardSweepTimer = 0.f;
		Mem->GuardSweepSegment = 0;
	}
}

EBTNodeResult::Type UBTTask_EnemyPatrol::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AAIController* Controller = OwnerComp.GetAIOwner())
	{
		if (const APawn* P = Controller->GetPawn())
			UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s AbortTask -> StopMovement (branch interrupted)"), *P->GetName());
		Controller->StopMovement();
	}
	return EBTNodeResult::Aborted;
}

void UBTTask_EnemyPatrol::PickNextIndex(FPatrolMemory& Mem, const APatrolRoute& Route) const
{
	const int32 Num = Route.NumPoints();
	if (Num <= 1) return;

	switch (Route.PatrolOrder)
	{
	case EPatrolOrder::Loop:
		Mem.CurrentIndex = (Mem.CurrentIndex + 1) % Num;
		break;

	case EPatrolOrder::PingPong:
		Mem.CurrentIndex += Mem.Direction;
		if (Mem.CurrentIndex >= Num - 1) { Mem.CurrentIndex = Num - 1; Mem.Direction = -1; }
		else if (Mem.CurrentIndex <= 0)  { Mem.CurrentIndex = 0;       Mem.Direction =  1; }
		break;

	case EPatrolOrder::Shuffle:
	{
		if (Num > 32)
		{
			// Above 32 points: pure-random excluding current (no mask — would overflow uint32)
			int32 Next = FMath::RandRange(0, Num - 2);
			if (Next >= Mem.CurrentIndex) ++Next;
			Mem.CurrentIndex = Next;
			break;
		}

		// Mark current as visited
		Mem.VisitedMask |= (1u << Mem.CurrentIndex);

		// Build the set of not-yet-visited indices (current already excluded via mask)
		// If the mask covers everything, reset it — but keep current excluded for this draw
		const uint32 FullMask = (Num < 32) ? ((1u << Num) - 1u) : 0xFFFFFFFFu;
		if ((Mem.VisitedMask & FullMask) == FullMask)
			Mem.VisitedMask = (1u << Mem.CurrentIndex);  // reset, keep current excluded

		// Collect candidate indices into a fixed-size stack buffer
		int32 Candidates[32];
		int32 CandidateCount = 0;
		for (int32 i = 0; i < Num; ++i)
		{
			if (!(Mem.VisitedMask & (1u << i)))
				Candidates[CandidateCount++] = i;
		}

		if (CandidateCount > 0)
			Mem.CurrentIndex = Candidates[FMath::RandRange(0, CandidateCount - 1)];
		break;
	}
	}
}

FVector UBTTask_EnemyPatrol::ResolveGoal(const APawn& Pawn, const APatrolRoute& Route, int32 Index) const
{
	const FVector Centre = Route.GetWorldPoint(Index);
	if (Route.WanderRadius <= 0.f) return Centre;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Pawn.GetWorld());
	if (!NavSys) return Centre;

	FNavLocation NavLoc;
	if (NavSys->GetRandomReachablePointInRadius(Centre, Route.WanderRadius, NavLoc))
		return NavLoc.Location;

	UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s WanderRadius %.0f: no reachable point near P%d -> using exact point"),
		*Pawn.GetName(), Route.WanderRadius, Index);
	return Centre;
}

void UBTTask_EnemyPatrol::IssueMoveToCurrentGoal(
	AAIController& Controller, const APawn& Pawn, FPatrolMemory& Mem, const APatrolRoute& Route) const
{
	Mem.CurrentGoal = ResolveGoal(Pawn, Route, Mem.CurrentIndex);
	const EPathFollowingRequestResult::Type Result =
		Controller.MoveToLocation(Mem.CurrentGoal, 50.f, false, true, false, true);
	Mem.bMoveIssued = true;
	UE_LOG(LogEnemyAI, Verbose, TEXT("[PATROL] %s MoveTo P%d (%.0f,%.0f,%.0f) result=%d (0=Failed 1=AlreadyAtGoal 2=RequestSuccessful)"),
		*Pawn.GetName(), Mem.CurrentIndex, Mem.CurrentGoal.X, Mem.CurrentGoal.Y, Mem.CurrentGoal.Z, (int32)Result);
}

void UBTTask_EnemyPatrol::RollWaitTarget(FPatrolMemory& Mem, const APatrolRoute& Route) const
{
	if (Route.WaitAtPointMaxSeconds > Route.WaitAtPointSeconds)
		Mem.WaitTarget = FMath::RandRange(Route.WaitAtPointSeconds, Route.WaitAtPointMaxSeconds);
	else
		Mem.WaitTarget = Route.WaitAtPointSeconds;
}

void UBTTask_EnemyPatrol::TickYawSweep(AAIController& Controller, const APawn& Pawn,
	const UEnemyArchetypeData& DA, float BaseYaw, float& Timer, int32& Segment, float DeltaSeconds) const
{
	constexpr int32 GuardSweepSegmentCount = 3;

	Timer += DeltaSeconds;
	if (Timer >= DA.GuardScanInterval)
	{
		Timer -= DA.GuardScanInterval;
		Segment = (Segment + 1) % GuardSweepSegmentCount;
	}

	const float YawOffsets[] = { -DA.GuardScanYawRange * 0.5f, DA.GuardScanYawRange * 0.5f, 0.f };
	const float TargetYaw = BaseYaw + YawOffsets[FMath::Min(Segment, GuardSweepSegmentCount - 1)];

	const FRotator CurrentRot = Pawn.GetActorRotation();
	const FRotator TargetRot(CurrentRot.Pitch, TargetYaw, CurrentRot.Roll);
	const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaSeconds, DA.GuardScanTurnSpeed);
	Controller.SetControlRotation(NewRot);
}

FString UBTTask_EnemyPatrol::GetStaticDescription() const
{
	return TEXT("Walk patrol route from BB_PatrolRoute (loop/ping-pong/shuffle, wander radius, randomised wait, loiter glance). Route-less: return to guard post then sweep.");
}
