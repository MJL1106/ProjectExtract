// BT task — queries the cover registry, claims a slot, and moves the companion to it.

#include "BTTask_MoveToCover.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "CompanionAIController.h"
#include "CoverRegistrySubsystem.h"
#include "AICoverSlot.h"

UBTTask_MoveToCover::UBTTask_MoveToCover()
{
	NodeName = TEXT("Move To Cover");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_MoveToCover::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Controller || !Pawn) return EBTNodeResult::Failed;

	bMoveIssued = false;
	CachedOwnerComp = &OwnerComp;

	UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover ENTER querier=%s hasCoverPos=%d combatTarget=%s"),
		*Pawn->GetName(), *Pawn->GetName(),
		(int32)BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName),
		*GetNameSafe(Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName))));

	// If we already have a claimed slot, check if we can just use it.
	AAICoverSlot* ExistingSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	if (IsValid(ExistingSlot) && ExistingSlot->IsClaimed())
	{
		const FVector StandPos = ExistingSlot->GetStandPosition();
		if (FVector::Dist(Pawn->GetActorLocation(), StandPos) <= AcceptableRadius)
			return EBTNodeResult::Succeeded;

		StartMoveTo(ExistingSlot, Controller, Pawn);
		return EBTNodeResult::InProgress;
	}

	// No target — no need for cover; fall through to open-engage.
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	if (!IsValid(Target))
	{
		BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
		BB->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
		return EBTNodeResult::Succeeded;
	}

	UCoverRegistrySubsystem* Registry = Pawn->GetWorld()->GetSubsystem<UCoverRegistrySubsystem>();
	if (!Registry) return EBTNodeResult::Failed;

	AAICoverSlot* Slot = Registry->FindBestCoverFor(Pawn->GetActorLocation(), Target, SearchRadius);
	if (!Slot)
	{
		UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover no slot found within radius=%.0f"), *Pawn->GetName(), SearchRadius);
		BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
		BB->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
		return EBTNodeResult::Succeeded;
	}

	if (!Slot->TryClaim(Pawn))
	{
		UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover slot=%s already claimed — falling to open-engage"), *Pawn->GetName(), *Slot->GetName());
		BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
		BB->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
		return EBTNodeResult::Succeeded;
	}

	const FVector StandPos = Slot->GetStandPosition();
	BB->SetValueAsObject(CoverSlotKey.SelectedKeyName, Slot);
	BB->SetValueAsVector(CoverLocationKey.SelectedKeyName, StandPos);
	BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, true);

	UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover claimed slot=%s loc=%s"), *Pawn->GetName(), *Slot->GetName(), *StandPos.ToString());

	if (FVector::Dist(Pawn->GetActorLocation(), StandPos) <= AcceptableRadius)
		return EBTNodeResult::Succeeded;

	StartMoveTo(Slot, Controller, Pawn);
	return EBTNodeResult::InProgress;
}

void UBTTask_MoveToCover::StartMoveTo(AAICoverSlot* Slot, AAIController* Controller, APawn* Pawn)
{
	const FVector StandPos = Slot->GetStandPosition();
	UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover GOTO slot=%s loc=%s"), *GetNameSafe(Pawn), *Slot->GetName(), *StandPos.ToString());
	Controller->MoveToLocation(StandPos, AcceptableRadius, false, true, false, true);
	bMoveIssued = true;
}

void UBTTask_MoveToCover::ReleaseClaim(UBlackboardComponent* BB, APawn* Pawn)
{
	if (!BB) return;
	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	if (IsValid(Slot))
	{
		Slot->Release(Pawn);
		UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover released claim on slot=%s"), *GetNameSafe(Pawn), *Slot->GetName());
	}
	BB->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
	BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
}

void UBTTask_MoveToCover::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Controller || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	if (!IsValid(Target))
	{
		ReleaseClaim(BB, Pawn);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	if (!bMoveIssued) return;

	UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
	if (!PF) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const EPathFollowingStatus::Type MoveStatus = PF->GetStatus();
	if (MoveStatus == EPathFollowingStatus::Moving) return;

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	const FVector StandPos = IsValid(Slot) ? Slot->GetStandPosition() : BB->GetValueAsVector(CoverLocationKey.SelectedKeyName);
	const bool bArrived = FVector::Dist(Pawn->GetActorLocation(), StandPos) <= AcceptableRadius;

	const float DistToCover = FVector::Dist(Pawn->GetActorLocation(), StandPos);

	if (bArrived)
	{
		UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover ARRIVED result=S status=Idle dist=%.0f radius=%.0f reason=ReachedGoal"),
			*Pawn->GetName(), DistToCover, AcceptableRadius);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	// Idle-not-arrived = path stopped short. Accept if CompanionCombat can teleport-snap the gap.
	if (MoveStatus == EPathFollowingStatus::Idle && DistToCover <= MaxStoppedShortTolerance)
	{
		UE_LOG(LogCompanionAI, Log,
			TEXT("%s: MoveToCover ARRIVED result=S status=Idle dist=%.0f radius=%.0f reason=StoppedShortAccepted"),
			*Pawn->GetName(), DistToCover, AcceptableRadius);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	const TCHAR* StatusStr =
		(MoveStatus == EPathFollowingStatus::Idle)    ? TEXT("Idle") :
		(MoveStatus == EPathFollowingStatus::Waiting) ? TEXT("Waiting") :
		(MoveStatus == EPathFollowingStatus::Paused)  ? TEXT("Paused") :
		                                                 TEXT("Moving");
	// Idle beyond tolerance = truly unreachable. Waiting/Paused = external interruption.
	const TCHAR* ReasonStr =
		(MoveStatus == EPathFollowingStatus::Idle)    ? TEXT("Unreachable") :
		(MoveStatus == EPathFollowingStatus::Waiting) ? TEXT("Waiting") :
		(MoveStatus == EPathFollowingStatus::Paused)  ? TEXT("Paused") :
		                                                 TEXT("StillMoving");
	UE_LOG(LogCompanionAI, Log,
		TEXT("%s: MoveToCover ARRIVED result=S status=%s dist=%.0f radius=%.0f reason=%s"),
		*Pawn->GetName(), StatusStr, DistToCover, AcceptableRadius, ReasonStr);
	ReleaseClaim(BB, Pawn);
	return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
}

EBTNodeResult::Type UBTTask_MoveToCover::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (Controller) Controller->StopMovement();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ReleaseClaim(BB, Pawn);

	bMoveIssued = false;
	CachedOwnerComp = nullptr;
	return EBTNodeResult::Aborted;
}

void UBTTask_MoveToCover::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	if (TaskResult != EBTNodeResult::Succeeded)
	{
		AAIController* Controller = OwnerComp.GetAIOwner();
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
		ReleaseClaim(BB, Pawn);
	}

	bMoveIssued = false;
	CachedOwnerComp = nullptr;
}

FString UBTTask_MoveToCover::GetStaticDescription() const
{
	return FString::Printf(TEXT("Find cover via Registry (radius: %.0f, accept: %.0f)"), SearchRadius, AcceptableRadius);
}
