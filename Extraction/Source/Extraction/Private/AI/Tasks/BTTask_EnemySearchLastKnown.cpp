// BTTask_EnemySearchLastKnown — move to InvestigateLocation, then yaw-sweep look-around.

#include "BTTask_EnemySearchLastKnown.h"
#include "EnemyAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"

UBTTask_EnemySearchLastKnown::UBTTask_EnemySearchLastKnown()
{
	NodeName = TEXT("Enemy Search Last Known");
	bNotifyTick = true;
}

uint16 UBTTask_EnemySearchLastKnown::GetInstanceMemorySize() const
{
	return sizeof(FSearchMemory);
}

EBTNodeResult::Type UBTTask_EnemySearchLastKnown::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FSearchMemory* Mem = reinterpret_cast<FSearchMemory*>(NodeMemory);
	new (Mem) FSearchMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	const FVector InvestigateLoc = BB->GetValueAsVector(AEnemyAIController::BB_InvestigateLocation);
	if (!BB->IsVectorValueSet(AEnemyAIController::BB_InvestigateLocation)) return EBTNodeResult::Succeeded;

	if (FVector::Dist(Pawn->GetActorLocation(), InvestigateLoc) <= 80.f)
	{
		// Already there — go straight to sweep
		Mem->Phase = ESearchPhase::Sweep;
		Mem->BaseYaw = Pawn->GetActorRotation().Yaw;
		return EBTNodeResult::InProgress;
	}

	Mem->IssuedGoal = InvestigateLoc;
	const EPathFollowingRequestResult::Type MoveResult = Controller->MoveToLocation(InvestigateLoc, 80.f, false, true, false, true);
	UE_LOG(LogEnemyAI, Verbose, TEXT("[SEARCH] %s MoveTo (%.0f,%.0f,%.0f) dist=%.0f result=%d (0=Failed 1=AlreadyAtGoal 2=RequestSuccessful)"),
		*Pawn->GetName(), InvestigateLoc.X, InvestigateLoc.Y, InvestigateLoc.Z,
		FVector::Dist(Pawn->GetActorLocation(), InvestigateLoc), (int32)MoveResult);
	Mem->Phase = ESearchPhase::MoveTo;
	return EBTNodeResult::InProgress;
}

void UBTTask_EnemySearchLastKnown::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FSearchMemory* Mem = reinterpret_cast<FSearchMemory*>(NodeMemory);

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Controller || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	if (Mem->Phase == ESearchPhase::MoveTo)
	{
		UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
		if (!PF) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

		// Re-poll BB_InvestigateLocation — the awareness component tracks ragdoll drift.
		UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
		if (IsValid(BB) && PF->GetStatus() != EPathFollowingStatus::Idle)
		{
			const FVector CurrentGoal = BB->GetValueAsVector(AEnemyAIController::BB_InvestigateLocation);
			if (FVector::Dist(CurrentGoal, Mem->IssuedGoal) > GoalDriftThreshold)
			{
				Mem->IssuedGoal = CurrentGoal;
				Controller->MoveToLocation(CurrentGoal, 80.f, false, true, false, true);
			}
		}

		if (PF->GetStatus() == EPathFollowingStatus::Idle)
		{
			UE_LOG(LogEnemyAI, Verbose, TEXT("[SEARCH] %s moveto idle -> sweep (vel=%.0f)"), *Pawn->GetName(), Pawn->GetVelocity().Size());
			// Arrived (or path failed) — start sweep
			Mem->Phase = ESearchPhase::Sweep;
			Mem->BaseYaw = Pawn->GetActorRotation().Yaw;
			Mem->SweepElapsed = 0.f;
			Mem->SweepSegment = 0;
		}
		return;
	}

	// Sweep phase: rotate through 3 segments over SweepDuration seconds
	Mem->SweepElapsed += DeltaSeconds;
	const float SegmentDuration = SweepDuration / SweepSegmentCount;

	// Alternate sweep directions: 0=left, 1=right, 2=center
	const float YawOffsets[] = { -SweepYawRange * 0.5f, SweepYawRange * 0.5f, 0.f };
	const float TargetYaw = Mem->BaseYaw + YawOffsets[FMath::Min(Mem->SweepSegment, SweepSegmentCount - 1)];
	const FRotator CurrentRot = Pawn->GetActorRotation();
	const FRotator TargetRot(CurrentRot.Pitch, TargetYaw, CurrentRot.Roll);
	const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaSeconds, 80.f);

	Controller->SetControlRotation(NewRot);

	if (Mem->SweepElapsed >= SegmentDuration)
	{
		Mem->SweepElapsed -= SegmentDuration;
		++Mem->SweepSegment;

		if (Mem->SweepSegment >= SweepSegmentCount)
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}

EBTNodeResult::Type UBTTask_EnemySearchLastKnown::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AAIController* Controller = OwnerComp.GetAIOwner())
		Controller->StopMovement();
	return EBTNodeResult::Aborted;
}

FString UBTTask_EnemySearchLastKnown::GetStaticDescription() const
{
	return TEXT("Move to InvestigateLocation then sweep-look for target");
}
