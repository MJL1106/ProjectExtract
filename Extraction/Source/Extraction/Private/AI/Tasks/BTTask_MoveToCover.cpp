// BT task — queries the cover registry, claims a slot, and moves the companion to it.

#include "BTTask_MoveToCover.h"
#include "AI/CompanionDiag.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "CoverRegistrySubsystem.h"
#include "AICoverSlot.h"
#include "CompanionCharacter.h"
#include "TraversalComponent.h"
#include "DrawDebugHelpers.h"

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

#if ENABLE_DRAW_DEBUG
	if (bDrawCoverSearchRadius)
		DrawDebugCircle(Pawn->GetWorld(), Pawn->GetActorLocation(), SearchRadius, 48, FColor::Cyan, false, 3.0f, 0, 3.0f,
			FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
#endif

	UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover ENTER querier=%s hasCoverPos=%d combatTarget=%s"),
		*Pawn->GetName(), *Pawn->GetName(),
		(int32)BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName),
		*GetNameSafe(Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName))));
	if (bDebugLogging) UE_LOG(LogCompanionDiag, Log, TEXT("%s: MOVE-TO-COVER-ENTER hasCoverPos=%d combatTarget=%s"),
		*Pawn->GetName(),
		(int32)BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName),
		*GetNameSafe(Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName))));

	// Cover-switch re-flow: the service pre-claimed a better slot and wrote it to BB_NextCoverSlot.
	// Take it directly — skip the picker to prevent proximity-bias re-selecting the old slot.
	AAICoverSlot* NextSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(NextCoverSlotKey.SelectedKeyName));
	if (IsValid(NextSlot))
	{
		if (NextSlot->IsClaimedBy(Pawn))
		{
			const FVector PawnLocNext = Pawn->GetActorLocation();
			const float AlphaNext = NextSlot->GetAlphaFromLocation(PawnLocNext);
			FVector ArrivalPosNext = NextSlot->GetLocationAtAlpha(AlphaNext);
			ArrivalPosNext.Z = PawnLocNext.Z;

			BB->SetValueAsObject(CoverSlotKey.SelectedKeyName, NextSlot);
			BB->SetValueAsVector(CoverLocationKey.SelectedKeyName, ArrivalPosNext);
			BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, true);
			BB->SetValueAsObject(NextCoverSlotKey.SelectedKeyName, nullptr);

			UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover using pre-claimed slot=%s from CoverSwitchMonitor"), *Pawn->GetName(), *NextSlot->GetName());

			if (FVector::Dist(PawnLocNext, ArrivalPosNext) <= AcceptableRadius)
				return EBTNodeResult::Succeeded;

			StartMoveTo(ArrivalPosNext, Controller, Pawn);
			return EBTNodeResult::InProgress;
		}

		// Stale hint (e.g. someone else claimed it between service write and task execute) — clear and fall through.
		UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover NextCoverSlot=%s not claimed by us — discarding hint"), *Pawn->GetName(), *NextSlot->GetName());
		BB->SetValueAsObject(NextCoverSlotKey.SelectedKeyName, nullptr);
	}

	// If we already have a claimed slot, check if we can just use it.
	AAICoverSlot* ExistingSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	if (IsValid(ExistingSlot))
	{
		if (ExistingSlot->IsClaimedBy(Pawn))
		{
			const FVector PawnLoc = Pawn->GetActorLocation();
			const float Alpha = ExistingSlot->GetAlphaFromLocation(PawnLoc);
			FVector ArrivalPos = ExistingSlot->GetLocationAtAlpha(Alpha);
			ArrivalPos.Z = PawnLoc.Z;
			BB->SetValueAsVector(CoverLocationKey.SelectedKeyName, ArrivalPos);

			if (FVector::Dist(PawnLoc, ArrivalPos) <= AcceptableRadius)
				return EBTNodeResult::Succeeded;

			StartMoveTo(ArrivalPos, Controller, Pawn);
			return EBTNodeResult::InProgress;
		}

		// Slot exists but is claimed by someone else — clear stale state and fall through to picker.
		UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover ExistingSlot=%s claimed by other — discarding"),
			*Pawn->GetName(), *ExistingSlot->GetName());
		BB->SetValueAsObject(CoverSlotKey.SelectedKeyName, nullptr);
		BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
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

	// P4 — honor the post-vacate cooldown here too: this fresh-picker path is the abort re-entry that
	// would otherwise proximity-bias straight back to the just-vacated slot. Cast for the tunable; if the
	// controller/tuning is absent, cooldown defaults to 0 (no exclusion) and behaviour is unchanged.
	const ACompanionAIController* CompanionController = Cast<ACompanionAIController>(Controller);
	const UCompanionTuningDataAsset* Tuning = CompanionController ? CompanionController->GetTuning() : nullptr;
	const float PostVacateCooldown = Tuning ? Tuning->CoverSwitchPostVacateCooldown : 0.f;

	const float EffectiveRadius = Tuning ? Tuning->CoverSearchRadius : SearchRadius;
	AAICoverSlot* Slot = Registry->FindBestCoverFor(Pawn->GetActorLocation(), Target, EffectiveRadius, nullptr, Pawn, PostVacateCooldown);
	if (!Slot)
	{
		UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover no slot found within radius=%.0f"), *Pawn->GetName(), EffectiveRadius);
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

	const FVector PawnLoc = Pawn->GetActorLocation();
	const float Alpha = Slot->GetAlphaFromLocation(PawnLoc);
	FVector ArrivalPos = Slot->GetLocationAtAlpha(Alpha);
	ArrivalPos.Z = PawnLoc.Z;

	BB->SetValueAsObject(CoverSlotKey.SelectedKeyName, Slot);
	BB->SetValueAsVector(CoverLocationKey.SelectedKeyName, ArrivalPos);
	BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, true);

	UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover claimed slot=%s alpha=%.2f loc=%s"), *Pawn->GetName(), *Slot->GetName(), Alpha, *ArrivalPos.ToString());

	if (FVector::Dist(PawnLoc, ArrivalPos) <= AcceptableRadius)
		return EBTNodeResult::Succeeded;

	StartMoveTo(ArrivalPos, Controller, Pawn);
	return EBTNodeResult::InProgress;
}

void UBTTask_MoveToCover::StartMoveTo(const FVector& ArrivalPos, AAIController* Controller, APawn* Pawn)
{
	UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover GOTO loc=%s"), *GetNameSafe(Pawn), *ArrivalPos.ToString());
	LastTickDist = Pawn ? FVector::Dist(Pawn->GetActorLocation(), ArrivalPos) : 0.f;
	Controller->MoveToLocation(ArrivalPos, AcceptableRadius, false, true, false, true);
	bMoveIssued = true;
	if (bDebugLogging) UE_LOG(LogCompanionDiag, Log, TEXT("%s: MOVE-TO-COVER-GOTO loc=%s dist=%.0f"),
		*GetNameSafe(Pawn), *ArrivalPos.ToString(), LastTickDist);
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

	const TCHAR* StatusStr =
		(MoveStatus == EPathFollowingStatus::Moving)  ? TEXT("Moving") :
		(MoveStatus == EPathFollowingStatus::Idle)    ? TEXT("Idle") :
		(MoveStatus == EPathFollowingStatus::Waiting) ? TEXT("Waiting") :
		                                                 TEXT("Paused");

	if (MoveStatus == EPathFollowingStatus::Moving)
	{
		const FVector ArrivalPosMoving = BB->GetValueAsVector(CoverLocationKey.SelectedKeyName);
		const float DistMoving = FVector::Dist(Pawn->GetActorLocation(), ArrivalPosMoving);

		if (bDebugLogging)
		{
			const float Delta = LastTickDist - DistMoving;
			LastTickDist = DistMoving;
			const float Vel = Pawn->GetVelocity().Size();
			UE_LOG(LogCompanionDiag, VeryVerbose, TEXT("%s: MOVE-TO-COVER-TICK status=Moving dist=%.0f delta=%.1f vel=%.1f"),
				*Pawn->GetName(), DistMoving, Delta, Vel);
		}

		// Start crouching before arrival so the transition into crouch cover is smooth.
		// Guard: skip if mid-traversal — capsule resize during root motion can clip the ground.
		ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn);
		AAICoverSlot* ApproachSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
		UTraversalComponent* TC = IsValid(Companion) ? Companion->GetTraversalComponent() : nullptr;
		const bool bTraversing = IsValid(TC) && TC->GetActiveType() != ETraversalType::None;
		if (IsValid(Companion) && IsValid(ApproachSlot) &&
			ApproachSlot->Height == ECoverHeight::Crouch &&
			DistMoving <= EarlyCrouchDistance &&
			!Companion->bIsCrouched &&
			!bTraversing)
		{
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CrouchCall] t=%.3f site=MoveToCoverEarlyCrouch action=Crouch"),
				*Companion->GetName(), Companion->GetWorld() ? Companion->GetWorld()->GetTimeSeconds() : 0.f);
			Companion->Crouch();
			UE_LOG(LogCompanionDiag, Log, TEXT("%s: MOVE-TO-COVER-EARLY-CROUCH dist=%.0f"), *Companion->GetName(), DistMoving);
		}

		return;
	}

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(CoverSlotKey.SelectedKeyName));
	const FVector ArrivalPos = BB->GetValueAsVector(CoverLocationKey.SelectedKeyName);
	const FVector StandPos = (ArrivalPos != FVector::ZeroVector) ? ArrivalPos : (IsValid(Slot) ? Slot->GetActorLocation() : Pawn->GetActorLocation());
	const bool bArrived = FVector::Dist(Pawn->GetActorLocation(), StandPos) <= AcceptableRadius;

	const float DistToCover = FVector::Dist(Pawn->GetActorLocation(), StandPos);

	if (bArrived)
	{
		UE_LOG(LogCompanionAI, Log, TEXT("%s: MoveToCover ARRIVED result=S status=Idle dist=%.0f radius=%.0f reason=ReachedGoal"),
			*Pawn->GetName(), DistToCover, AcceptableRadius);
		if (bDebugLogging) UE_LOG(LogCompanionDiag, Log, TEXT("%s: MOVE-TO-COVER-DONE result=Succeeded status=%s dist=%.0f reason=ReachedGoal"),
			*Pawn->GetName(), StatusStr, DistToCover);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	// Idle-not-arrived = path stopped short. Accept if CompanionCombat can teleport-snap the gap.
	if (MoveStatus == EPathFollowingStatus::Idle && DistToCover <= MaxStoppedShortTolerance)
	{
		UE_LOG(LogCompanionAI, Log,
			TEXT("%s: MoveToCover ARRIVED result=S status=Idle dist=%.0f radius=%.0f reason=StoppedShortAccepted"),
			*Pawn->GetName(), DistToCover, AcceptableRadius);
		if (bDebugLogging) UE_LOG(LogCompanionDiag, Log, TEXT("%s: MOVE-TO-COVER-DONE result=Succeeded status=Idle dist=%.0f reason=StoppedShortAccepted"),
			*Pawn->GetName(), DistToCover);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	// Idle beyond tolerance = truly unreachable. Waiting/Paused = external interruption.
	const TCHAR* ReasonStr =
		(MoveStatus == EPathFollowingStatus::Idle)    ? TEXT("Unreachable") :
		(MoveStatus == EPathFollowingStatus::Waiting) ? TEXT("Waiting") :
		(MoveStatus == EPathFollowingStatus::Paused)  ? TEXT("Paused") :
		                                                 TEXT("StillMoving");
	UE_LOG(LogCompanionAI, Log,
		TEXT("%s: MoveToCover ARRIVED result=S status=%s dist=%.0f radius=%.0f reason=%s"),
		*Pawn->GetName(), StatusStr, DistToCover, AcceptableRadius, ReasonStr);
	if (bDebugLogging) UE_LOG(LogCompanionDiag, Log, TEXT("%s: MOVE-TO-COVER-DONE result=Succeeded status=%s dist=%.0f reason=%s"),
		*Pawn->GetName(), StatusStr, DistToCover, ReasonStr);
	// Path couldn't reach this slot — stamp the post-vacate cooldown so the open-engage re-seek and the
	// picker stop re-selecting an unreachable slot every interval (anti-thrash). Idle-only: Waiting/Paused
	// are transient interruptions, not unreachability.
	if (MoveStatus == EPathFollowingStatus::Idle && IsValid(Slot))
		Slot->MarkVacatedForSwitch(Pawn);
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
}

FString UBTTask_MoveToCover::GetStaticDescription() const
{
	return FString::Printf(TEXT("Find cover via Registry (radius: %.0f, accept: %.0f)"), SearchRadius, AcceptableRadius);
}
