// BT task — companion approaches the player's traversed obstacle and plays its own traversal.

#include "BTTask_MirrorPlayerTraversal.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "AI/Cover/AICoverSlot.h"
#include "Companion/CompanionCharacter.h"
#include "Movement/TraversalComponent.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
	bool TryTeleportToLanding(UBehaviorTreeComponent& OwnerComp, FMirrorMemory* Mem)
	{
		if (!Mem) return false;
		ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
		if (!AIC) return false;
		APawn* Pawn = AIC->GetPawn();
		UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
		if (!Pawn || !BB) return false;

		const FVector Landing = BB->GetValueAsVector(ACompanionAIController::BB_PlayerTraversalLanding);
		if (Landing.IsNearlyZero()) return false;

		// Use the raw Landing — that's where the player actually ended their traversal,
		// so it's a guaranteed-valid spot. Projection has been observed to drag the
		// destination to the floor beside the obstacle and then fail TeleportTo.
		FVector Destination = Landing;

		// Cancel any in-flight traversal first (defence — usually not in one here).
		if (UTraversalComponent* OwnTrav = Pawn->FindComponentByClass<UTraversalComponent>())
		{
			if (OwnTrav->IsInTraversal())
				OwnTrav->CancelTraversal();
		}

		// Slight Z lift so we don't spawn intersecting geometry. Pawn settles via gravity.
		Destination.Z += 20.f;

		const bool bTeleported = Pawn->TeleportTo(Destination, Pawn->GetActorRotation());
		UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] Per-obstacle fallback teleport: landing=%s dest=%s -> %s"),
			*Landing.ToCompactString(), *Destination.ToCompactString(), bTeleported ? TEXT("OK") : TEXT("FAIL"));
		return bTeleported;
	}
}

UBTTask_MirrorPlayerTraversal::UBTTask_MirrorPlayerTraversal()
{
	NodeName = TEXT("Mirror Player Traversal");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	// bCreateNodeInstance left false: per-instance state lives on FMirrorMemory.
}

EBTNodeResult::Type UBTTask_MirrorPlayerTraversal::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// All guards run BEFORE placement-new — synchronous Failed paths skip OnTaskFinished,
	// which would leak FMirrorMemory's non-trivial members (TWeakObjectPtr, FDelegateHandle).
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	const UCompanionTuningDataAsset* T = AIC ? AIC->GetTuning() : nullptr;
	if (!T) return EBTNodeResult::Failed;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	if (!Companion) return EBTNodeResult::Failed;

	UTraversalComponent* OwnTraversal = Companion->GetTraversalComponent();
	if (!OwnTraversal) return EBTNodeResult::Failed;

	// Suppress mirror while companion is occupying a cover slot — vaulting the cover
	// wall mid-approach breaks the cover workflow. Matches BTService_TraversalProbe guard.
	AAICoverSlot* CoverSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(ACompanionAIController::BB_CoverSlot));
	if (IsValid(CoverSlot) && CoverSlot->IsClaimedBy(Companion))
	{
		UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] Suppressed: cover slot active"));
		return EBTNodeResult::Succeeded;
	}

	// All guards passed — safe to allocate per-instance memory.
	FMirrorMemory* Mem = new (NodeMemory) FMirrorMemory();
	Mem->OwnTraversalComp = OwnTraversal;

	// Approach along the player's traversal axis, not from the companion's offset position —
	// keeps the trace square-on with the wall so the clearance trace inside TryStartTraversal
	// actually hits the obstacle.
	const FVector Landing = BB->GetValueAsVector(ACompanionAIController::BB_PlayerTraversalLanding);
	const FVector Obstacle = BB->GetValueAsVector(ACompanionAIController::BB_PlayerTraversalObstacle);
	const FVector PawnLoc = Companion->GetActorLocation();
	FVector PathDir = (Landing - Obstacle).GetSafeNormal2D();

	// If the player's traversal axis collapsed to zero (Landing≈Obstacle), fall back to
	// approaching from the companion's current position. If THAT's also zero, abort —
	// we're standing on the obstacle and can't compute a meaningful approach.
	if (PathDir.IsNearlyZero())
	{
		PathDir = (PawnLoc - Obstacle).GetSafeNormal2D();
		if (PathDir.IsNearlyZero())
		{
			UE_LOG(LogCompanionAI, Warning, TEXT("[MirrorTraversal] Approach direction collapsed (Landing≈Obstacle≈PawnLoc) — failing task."));
			Mem->~FMirrorMemory();
			return EBTNodeResult::Failed;
		}
	}

	FVector ApproachPoint = Obstacle - PathDir * T->MirrorEdgeApproachOffset;
	ApproachPoint.Z = PawnLoc.Z;
	Mem->ApproachPoint = ApproachPoint;
	Mem->Phase = FMirrorMemory::EPhase::Approach;
	Mem->Elapsed = 0.f;

	// Bind own traversal-ended → finish task succeeded. Capture a weak ref to the BT
	// component so a late broadcast (after the BT component is GC'd / level-transitioned)
	// can't dereference invalid memory. We also unbind in OnTaskFinished.
	// Capture MemPtr and gate on Phase==WaitForTraversalEnd so a stale TraversalEnded
	// broadcast from a traversal we did NOT start (e.g. one kicked off by the probe
	// service in the same frame the BT switched to this branch) can't finish us early.
	FMirrorMemory* MemPtr = Mem;
	TWeakObjectPtr<UBehaviorTreeComponent> WeakOwner(&OwnerComp);
	Mem->EndedHandle = OwnTraversal->OnTraversalEnded.AddLambda([WeakOwner, this, MemPtr]()
	{
		if (!MemPtr || MemPtr->Phase != FMirrorMemory::EPhase::WaitForTraversalEnd)
			return;
		if (UBehaviorTreeComponent* SafeOwner = WeakOwner.Get())
			FinishLatentTask(*SafeOwner, EBTNodeResult::Succeeded);
	});

	// Sprint to catch up — mirrors BTTask_FollowPlayer's pattern.
	Companion->SetSprinting(true);

	UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] task started — Obstacle=%s ApproachPoint=%s"),
		*Obstacle.ToCompactString(), *Mem->ApproachPoint.ToCompactString());

	// Pre-project to navmesh with generous extent so the mantle goal (which is at the wall
	// base where navmesh ends) doesn't silently fail to compute a path.
	if (UWorld* World = GetWorld())
	{
		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
		{
			FNavLocation OutLoc;
			if (NavSys->ProjectPointToNavigation(Mem->ApproachPoint, OutLoc, FVector(500.f, 500.f, 500.f)))
			{
				Mem->ApproachPoint = OutLoc.Location;
				UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] ApproachPoint projected to nav: %s"), *Mem->ApproachPoint.ToCompactString());
			}
			else
			{
				UE_LOG(LogCompanionAI, Warning, TEXT("[MirrorTraversal] ApproachPoint failed nav projection — trying anyway."));
			}
		}
	}

	const EPathFollowingRequestResult::Type MoveResult = AIC->MoveToLocation(
		Mem->ApproachPoint,
		T->MirrorReachToleranceXY,
		/*bStopOnOverlap*/ false,
		/*bUsePathfinding*/ true,
		/*bProjectDestinationToNavigation*/ true,
		/*bCanStrafe*/ true,
		nullptr,
		/*bAllowPartialPath*/ true);

	const FString MoveResultStr =
		MoveResult == EPathFollowingRequestResult::RequestSuccessful ? TEXT("Successful") :
		MoveResult == EPathFollowingRequestResult::AlreadyAtGoal ? TEXT("AlreadyAtGoal") :
		MoveResult == EPathFollowingRequestResult::Failed ? TEXT("Failed") : TEXT("Unknown");
	UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] MoveToLocation result: %s (DistToApproach=%.0f)"),
		*MoveResultStr, FVector::Dist2D(Companion->GetActorLocation(), Mem->ApproachPoint));

	return EBTNodeResult::InProgress;
}

void UBTTask_MirrorPlayerTraversal::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	// BT framework always allocates GetInstanceMemorySize() bytes — Mem cannot be null.
	FMirrorMemory* Mem = reinterpret_cast<FMirrorMemory*>(NodeMemory);

	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	const UCompanionTuningDataAsset* T = AIC ? AIC->GetTuning() : nullptr;
	if (!T) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Re-assert sprint every tick during approach — FollowPlayer's OnTaskFinished can
	// clear it after its abort even though we set it in ExecuteTask.
	if (Mem->Phase == FMirrorMemory::EPhase::Approach)
	{
		if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn))
			Companion->SetSprinting(true);
	}

	Mem->Elapsed += DeltaSeconds;

	Mem->DebugLogAccumulator += DeltaSeconds;
	if (Mem->DebugLogAccumulator >= 1.0f)
	{
		Mem->DebugLogAccumulator = 0.f;
		const FVector PawnLocNow = Pawn->GetActorLocation();
		const float DistToApproach = FVector::Dist2D(PawnLocNow, Mem->ApproachPoint);
		const FVector Velocity = Pawn->GetVelocity();
		const float Speed = Velocity.Size2D();
		const FString PathState = AIC->GetMoveStatus() == EPathFollowingStatus::Moving ? TEXT("Moving")
			: AIC->GetMoveStatus() == EPathFollowingStatus::Idle ? TEXT("Idle")
			: AIC->GetMoveStatus() == EPathFollowingStatus::Paused ? TEXT("Paused") : TEXT("Waiting");
		UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] Heartbeat: Elapsed=%.1f Phase=%d DistToApproach=%.0f Speed=%.0f Path=%s"),
			Mem->Elapsed, (int32)Mem->Phase, DistToApproach, Speed, *PathState);
	}

	// Catch-up timeout — fail and let warp safety net recover.
	if (Mem->Elapsed > T->CatchUpTimeout)
	{
		UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] CatchUpTimeout (%.2fs) — trying landing-teleport fallback."), Mem->Elapsed);
		if (TryTeleportToLanding(OwnerComp, Mem))
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	if (Mem->Phase != FMirrorMemory::EPhase::Approach)
		return;

	const FVector PawnLoc = Pawn->GetActorLocation();
	const float DistXY = FVector::Dist2D(PawnLoc, Mem->ApproachPoint);

	const EPathFollowingStatus::Type MoveStatus = AIC->GetMoveStatus();
	const bool bWithinTolerance = DistXY < T->MirrorReachToleranceXY;
	const bool bPathEndedNearby = (MoveStatus == EPathFollowingStatus::Idle && DistXY < 200.f && Mem->Elapsed > 0.5f);

	if (!bWithinTolerance && !bPathEndedNearby)
		return;

	if (bPathEndedNearby && !bWithinTolerance)
	{
		UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] Path ended at DistXY=%.0f (>tolerance) — forcing TryStartTraversal anyway."), DistXY);
	}

	UTraversalComponent* OwnTraversal = Mem->OwnTraversalComp.Get();
	if (!OwnTraversal) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Face the obstacle so the forward-wall trace inside TryStartTraversal hits it.
	// SetFocalPoint > SetActorRotation because CMC's bOrientRotationToMovement
	// overrides direct actor rotation when the character is settling.
	if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
	{
		const FVector ObstacleNow = BB->GetValueAsVector(ACompanionAIController::BB_PlayerTraversalObstacle);
		AIC->SetFocalPoint(ObstacleNow, EAIFocusPriority::Move);
		// Also snap actor rotation as backup — covers the same-frame trace before focus pulls.
		FVector ToObstacle = ObstacleNow - Pawn->GetActorLocation();
		ToObstacle.Z = 0.f;
		if (!ToObstacle.IsNearlyZero())
		{
			const FRotator FaceRot(0.f, ToObstacle.Rotation().Yaw, 0.f);
			Pawn->SetActorRotation(FaceRot);
		}
	}
	UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] Pre-traversal yaw=%.1f, pawn=%s"), Pawn->GetActorRotation().Yaw, *Pawn->GetActorLocation().ToCompactString());

	UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] Approach reached (DistXY=%.1f) — TryStartTraversal"), DistXY);

	const bool bStarted = OwnTraversal->TryStartTraversal(/*bWasSprinting*/ true);
	if (!bStarted)
	{
		UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] TryStartTraversal returned false — trying landing-teleport fallback."));
		if (TryTeleportToLanding(OwnerComp, Mem))
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	Mem->Phase = FMirrorMemory::EPhase::WaitForTraversalEnd;
}

void UBTTask_MirrorPlayerTraversal::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	// Mem is guaranteed non-null here: OnTaskFinished only runs for tasks that returned
	// InProgress (placement-new ran in ExecuteTask after all guards passed).
	FMirrorMemory* Mem = reinterpret_cast<FMirrorMemory*>(NodeMemory);

	UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] OnTaskFinished (result=%d)"), (int32)TaskResult);

	if (UTraversalComponent* OwnTraversal = Mem->OwnTraversalComp.Get())
	{
		if (Mem->EndedHandle.IsValid())
			OwnTraversal->OnTraversalEnded.Remove(Mem->EndedHandle);
	}
	Mem->EndedHandle.Reset();

	if (ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner()))
	{
		AIC->ClearFocus(EAIFocusPriority::Move);
		AIC->StopMovement();
		if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn()))
			Companion->SetSprinting(false);
		AIC->ClearTraversalBlackboard();
	}

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);

	// Destroy the placement-newed memory struct (FDelegateHandle / TWeakObjectPtr have non-trivial dtors).
	Mem->~FMirrorMemory();
}

FString UBTTask_MirrorPlayerTraversal::GetStaticDescription() const
{
	return TEXT("Mirror Player Traversal");
}
