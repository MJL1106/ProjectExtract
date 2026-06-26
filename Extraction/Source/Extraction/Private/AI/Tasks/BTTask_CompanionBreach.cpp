// BT task — companion breach execution.

#include "AI/Tasks/BTTask_CompanionBreach.h"
#include "AIController.h"
#include "AI/CompanionAIController.h"
#include "World/Breachable.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Navigation/PathFollowingComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompanionBreach, Log, All);

UBTTask_CompanionBreach::UBTTask_CompanionBreach()
{
	NodeName = TEXT("Companion Breach");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_CompanionBreach::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	bMoveRequested = false;
	bBreachTriggered = false;
	CachedDoor.Reset();

	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(AIC))
	{
		UE_LOG(LogCompanionBreach, Warning, TEXT("ExecuteTask: no CompanionAIController"));
		return EBTNodeResult::Failed;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!IsValid(BB))
	{
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	AActor* Door = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CommandTargetActor));
	if (!IsValid(Door))
	{
		UE_LOG(LogCompanionBreach, Warning, TEXT("ExecuteTask: BB_CommandTargetActor is null or invalid"));
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	// Validate the door implements IBreachable and is in a breachable state.
	if (!Door->GetClass()->ImplementsInterface(UBreachable::StaticClass()))
	{
		UE_LOG(LogCompanionBreach, Warning, TEXT("ExecuteTask: %s does not implement IBreachable"), *GetNameSafe(Door));
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	if (!IBreachable::Execute_CanBreach(Door))
	{
		UE_LOG(LogCompanionBreach, Log, TEXT("ExecuteTask: %s cannot be breached (already open?)"), *GetNameSafe(Door));
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	CachedDoor = Door;

	// Check if already within range.
	APawn* Pawn = AIC->GetPawn();
	if (IsValid(Pawn))
	{
		const float Dist = FVector::Dist(Pawn->GetActorLocation(), Door->GetActorLocation());
		if (Dist <= InteractionRange)
		{
			// Already close enough — breach immediately in TickTask.
			UE_LOG(LogCompanionBreach, Log, TEXT("ExecuteTask: already within range (%.0f <= %.0f) of %s"),
				Dist, InteractionRange, *GetNameSafe(Door));
			return EBTNodeResult::InProgress;
		}
	}

	// Issue move-to.
	const EPathFollowingRequestResult::Type MoveResult =
		AIC->MoveToActor(Door, MoveAcceptRadius, true, true, false, nullptr, true);

	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogCompanionBreach, Warning, TEXT("ExecuteTask: MoveToActor failed for %s"), *GetNameSafe(Door));
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	bMoveRequested = true;
	UE_LOG(LogCompanionBreach, Log, TEXT("ExecuteTask: moving to %s (range=%.0f)"), *GetNameSafe(Door), InteractionRange);
	return EBTNodeResult::InProgress;
}

void UBTTask_CompanionBreach::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	if (bBreachTriggered) return;

	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(AIC))
	{
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AActor* Door = CachedDoor.Get();
	if (!IsValid(Door))
	{
		UE_LOG(LogCompanionBreach, Warning, TEXT("TickTask: door destroyed mid-task"));
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Re-check breachability — door may have been opened by something else.
	if (!IBreachable::Execute_CanBreach(Door))
	{
		UE_LOG(LogCompanionBreach, Log, TEXT("TickTask: %s no longer breachable"), *GetNameSafe(Door));
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	APawn* Pawn = AIC->GetPawn();
	if (!IsValid(Pawn))
	{
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Check if the move request failed asynchronously (path blocked, etc.).
	if (bMoveRequested && AIC->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		const float Dist = FVector::Dist(Pawn->GetActorLocation(), Door->GetActorLocation());
		if (Dist > InteractionRange)
		{
			UE_LOG(LogCompanionBreach, Warning, TEXT("TickTask: path completed but still out of range (%.0f > %.0f)"),
				Dist, InteractionRange);
			FailAndClear(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}
	}

	// Distance check.
	const float Dist = FVector::Dist(Pawn->GetActorLocation(), Door->GetActorLocation());
	if (Dist > InteractionRange) return;

	// Within range — execute breach.
	AIC->StopMovement();
	bBreachTriggered = true;
	IBreachable::Execute_Breach(Door, Pawn);

	UE_LOG(LogCompanionBreach, Log, TEXT("TickTask: breached %s at dist=%.0f"), *GetNameSafe(Door), Dist);

	AIC->ClearActiveCommand();
	CachedDoor.Reset();
	FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
}

EBTNodeResult::Type UBTTask_CompanionBreach::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FailAndClear(OwnerComp);
	return EBTNodeResult::Aborted;
}

FString UBTTask_CompanionBreach::GetStaticDescription() const
{
	return FString::Printf(TEXT("Breach door within %.0f cm"), InteractionRange);
}

void UBTTask_CompanionBreach::FailAndClear(UBehaviorTreeComponent& OwnerComp)
{
	if (ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner()))
	{
		AIC->StopMovement();
		AIC->ClearActiveCommand();
	}

	CachedDoor.Reset();
	bMoveRequested = false;
	bBreachTriggered = false;
}
