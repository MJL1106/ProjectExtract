// BT task — companion breach execution.

#include "AI/Tasks/BTTask_CompanionBreach.h"
#include "AIController.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "Companion/CompanionCharacter.h"
#include "World/Breachable.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AISense_Hearing.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompanionBreach, Log, All);

// Distance from pawn to the nearest point on the door's collision bounds (not its actor origin).
// This makes the range check robust to large doors whose pivot sits at the hinge edge.
static float DistanceFromPawnToDoor(const APawn* Pawn, const AActor* Door)
{
	if (!IsValid(Pawn) || !IsValid(Door)) return TNumericLimits<float>::Max();
	const FBox Bounds = Door->GetComponentsBoundingBox(false);
	if (!Bounds.IsValid) return FVector::Dist(Pawn->GetActorLocation(), Door->GetActorLocation());
	return FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(Pawn->GetActorLocation()));
}

// Emits the per-breach-type hearing noise at the door. Missing tuning/profile, or a profile with
// Loudness/MaxRange <= 0 (Quiet's default), stays silent.
static void ReportBreachNoise(ACompanionAIController* AIC, const AActor* Door, EBreachType Type)
{
	if (!IsValid(AIC) || !IsValid(Door)) return;

	const UCompanionTuningDataAsset* Tuning = AIC->GetTuning();
	const FBreachNoiseProfile* Profile = Tuning ? Tuning->BreachNoise.Find(Type) : nullptr;
	if (!Profile || Profile->Loudness <= 0.f || Profile->MaxRange <= 0.f) return;

	UAISense_Hearing::ReportNoiseEvent(AIC->GetWorld(), Door->GetActorLocation(),
		Profile->Loudness, AIC->GetPawn(), Profile->MaxRange, TEXT("Breach"));
}

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
	BreachWaitElapsed = 0.f;
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
		const float Dist = DistanceFromPawnToDoor(Pawn, Door);
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
	// Post-breach: hold at the door for PostBreachWaitTime, then finish so Follow resumes.
	if (bBreachTriggered)
	{
		BreachWaitElapsed += DeltaSeconds;
		if (BreachWaitElapsed >= PostBreachWaitTime)
		{
			if (ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner()))
			{
				AIC->ClearActiveCommand();
			}
			CachedDoor.Reset();
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		return;
	}

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

	const float Dist = DistanceFromPawnToDoor(Pawn, Door);
	const bool bPathDone = (AIC->GetMoveStatus() == EPathFollowingStatus::Idle);

	// Breach when genuinely adjacent (nav reached the door), OR when the path has completed and
	// we're within the generous arrival cap (free-standing door / nav gap — got as close as we can).
	const bool bShouldBreach = (Dist <= InteractionRange) || (bPathDone && Dist <= ArrivalBreachRange);

	if (!bShouldBreach)
	{
		// If the path is done but we're still beyond the arrival cap, the companion genuinely
		// couldn't reach the door — fail. Otherwise keep moving/waiting.
		if (bMoveRequested && bPathDone)
		{
			UE_LOG(LogCompanionBreach, Warning, TEXT("TickTask: path completed but too far to breach (%.0f > %.0f)"),
				Dist, ArrivalBreachRange);
			FailAndClear(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}
		return;
	}

	// Within range (or arrived as close as possible) — execute breach.
	AIC->StopMovement();
	bBreachTriggered = true;

	// Breach type was written by SetBreachType at confirm time (mode-derived).
	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	const EBreachType BreachType = BB
		? static_cast<EBreachType>(BB->GetValueAsEnum(ACompanionAIController::BB_BreachType))
		: EBreachType::Tactical;

	if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn))
		Companion->PlayBreachMontage(BreachType);

	IBreachable::Execute_Breach(Door, Pawn);
	ReportBreachNoise(AIC, Door, BreachType);

	UE_LOG(LogCompanionBreach, Log, TEXT("TickTask: breached %s (type=%d) at dist=%.0f (pathDone=%d), holding %.1fs"),
		*GetNameSafe(Door), static_cast<int32>(BreachType), Dist, bPathDone ? 1 : 0, PostBreachWaitTime);
	BreachWaitElapsed = 0.f;
	return;
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
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (IsValid(AIC))
	{
		AIC->StopMovement();

		// Guarded clear: an abort caused by a fresh replacement command (takedown/loot) must not
		// wipe the command it was replaced by.
		const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
		const ECompanionCommand Active = IsValid(BB)
			? static_cast<ECompanionCommand>(BB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand))
			: ECompanionCommand::None;
		if (!IsValid(BB) || Active == ECompanionCommand::Breach)
			AIC->ClearActiveCommand();
	}

	CachedDoor.Reset();
	bMoveRequested = false;
	bBreachTriggered = false;
	BreachWaitElapsed = 0.f;
}
