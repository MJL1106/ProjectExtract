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

UBTTask_CompanionBreach::UBTTask_CompanionBreach()
{
	NodeName = TEXT("Companion Breach");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_CompanionBreach::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Phase = EBreachPhase::MovingToDoor;
	bHasStandPoint = false;
	bRepositionDeclined = false;
	AlignElapsed = 0.f;
	PhaseElapsed = 0.f;
	MontageLength = 0.f;
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

	APawn* Pawn = AIC->GetPawn();
	if (!IsValid(Pawn))
	{
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	CachedDoor = Door;
	bHadCombatTargetAtStart = IsValid(Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget)));

	// Stand point: where the breach montage was authored to play from. Doors provide one; a
	// breachable with none falls back to "in front of its origin, facing it".
	bHasStandPoint = IBreachable::Execute_GetBreachStandPoint(Door, Pawn, StandLocation, StandFacing);
	if (!bHasStandPoint)
	{
		const FVector ToDoor = Door->GetActorLocation() - Pawn->GetActorLocation();
		StandFacing = FRotator(0.f, ToDoor.Rotation().Yaw, 0.f);
		StandLocation = Door->GetActorLocation() - ToDoor.GetSafeNormal2D() * 110.f;
		StandLocation.Z = Pawn->GetActorLocation().Z;
	}

	const EPathFollowingRequestResult::Type MoveResult =
		AIC->MoveToLocation(StandLocation, StandPointAcceptRadius, /*bStopOnOverlap*/ true,
			/*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true);

	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogCompanionBreach, Warning, TEXT("ExecuteTask: move to stand point failed for %s"), *GetNameSafe(Door));
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	UE_LOG(LogCompanionBreach, Log, TEXT("ExecuteTask: moving to stand point of %s (standPoint=%d)"),
		*GetNameSafe(Door), bHasStandPoint ? 1 : 0);
	return EBTNodeResult::InProgress;
}

void UBTTask_CompanionBreach::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	APawn* Pawn = IsValid(AIC) ? AIC->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AActor* Door = CachedDoor.Get();
	if (!IsValid(Door))
	{
		// Destroyed after the swing = the breach already happened; the command still succeeded.
		if (Phase == EBreachPhase::Holding || Phase == EBreachPhase::Repositioning)
		{
			if (Phase == EBreachPhase::Repositioning) AIC->StopMovement();
			AIC->ClearActiveCommand();
			CachedDoor.Reset();
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			return;
		}

		UE_LOG(LogCompanionBreach, Warning, TEXT("TickTask: door destroyed mid-task"));
		if (Phase == EBreachPhase::PlayingMontage)
			if (ACharacter* Character = Cast<ACharacter>(Pawn)) Character->StopAnimMontage();
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Combat breaks off an in-flight breach: the fight takes priority, and the player re-pings
	// the door afterwards. Only a NEWLY acquired target breaks off — a ping issued while already
	// fighting is a deliberate override and proceeds. Once the montage has started
	// (PlayingMontage/Holding) it completes — it's under a second and aborting mid-kick looks broken.
	const UBlackboardComponent* TickBB = OwnerComp.GetBlackboardComponent();
	const bool bInCombat = IsValid(TickBB)
		&& IsValid(Cast<AActor>(TickBB->GetValueAsObject(ACompanionAIController::BB_CombatTarget)));

	if (bInCombat && !bHadCombatTargetAtStart
		&& (Phase == EBreachPhase::MovingToDoor || Phase == EBreachPhase::Aligning))
	{
		UE_LOG(LogCompanionBreach, Log, TEXT("TickTask: combat target acquired — breaking off breach of %s"), *GetNameSafe(Door));
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	switch (Phase)
	{
	case EBreachPhase::MovingToDoor:
	{
		// Door may have been opened by something else while we were walking over.
		if (!IBreachable::Execute_CanBreach(Door))
		{
			UE_LOG(LogCompanionBreach, Log, TEXT("TickTask: %s no longer breachable"), *GetNameSafe(Door));
			FailAndClear(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}

		if (AIC->GetMoveStatus() != EPathFollowingStatus::Idle) return;

		const float Dist = FVector::Dist2D(Pawn->GetActorLocation(), StandLocation);
		if (Dist > MaxAlignSnapDistance)
		{
			UE_LOG(LogCompanionBreach, Warning, TEXT("TickTask: path completed but too far from stand point (%.0f > %.0f)"),
				Dist, MaxAlignSnapDistance);
			FailAndClear(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}

		// Re-query the stand point from where the pawn actually ended up: nav projection can land
		// the move on the far side of the door, and the door flips the stand normal onto the
		// breacher's CURRENT side — so the align target is never through the closed panel.
		if (bHasStandPoint)
			IBreachable::Execute_GetBreachStandPoint(Door, Pawn, StandLocation, StandFacing);

		StartAlign(Pawn);
		return;
	}

	case EBreachPhase::Aligning:
	{
		// Opened elsewhere during the align beat — don't play a breach anim at an open door.
		if (!IBreachable::Execute_CanBreach(Door))
		{
			FailAndClear(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}

		AlignElapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(AlignElapsed / AlignDuration, 0.f, 1.f);
		const float Eased = FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f);

		FVector NewLoc = FMath::Lerp(AlignStartLocation, StandLocation, Eased);
		NewLoc.Z = Pawn->GetActorLocation().Z; // keep the capsule's own floor height
		const float NewYaw = AlignStartYaw + FMath::FindDeltaAngleDegrees(AlignStartYaw, StandFacing.Yaw) * Eased;
		Pawn->SetActorLocation(NewLoc, /*bSweep*/ true); // don't shove through the player/door
		Pawn->SetActorRotation(FRotator(0.f, NewYaw, 0.f));

		if (Alpha < 1.f) return;

		// Aligned — start the montage; the door swings at the contact time.
		// Breach type follows the companion's mode AT THIS MOMENT (Combat=Loud kick,
		// Stealth=Quiet, Normal=Tactical) — switching mode while the companion walks over
		// counts. BB_BreachType (written at confirm, same mapping in ConfirmBreach) is only
		// the fallback when the pawn isn't a companion.
		ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn);
		if (Companion)
		{
			switch (Companion->GetMode())
			{
			case ECompanionMode::Combat:  PendingBreachType = EBreachType::Loud;  break;
			case ECompanionMode::Stealth: PendingBreachType = EBreachType::Quiet; break;
			default:                      PendingBreachType = EBreachType::Tactical; break;
			}
		}
		else
		{
			const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
			PendingBreachType = BB
				? static_cast<EBreachType>(BB->GetValueAsEnum(ACompanionAIController::BB_BreachType))
				: EBreachType::Tactical;
		}

		MontageLength = Companion ? Companion->PlayBreachMontage(PendingBreachType) : 0.f;

		const UCompanionTuningDataAsset* Tuning = AIC->GetTuning();
		const float* TunedDelay = Tuning ? Tuning->BreachOpenDelay.Find(PendingBreachType) : nullptr;
		OpenDelay = TunedDelay ? *TunedDelay : DefaultOpenDelay;
		if (MontageLength > 0.f) OpenDelay = FMath::Min(OpenDelay, MontageLength);

		Phase = EBreachPhase::PlayingMontage;
		PhaseElapsed = 0.f;
		UE_LOG(LogCompanionBreach, Log, TEXT("TickTask: aligned at %s — montage len=%.2fs, door opens in %.2fs (type=%d)"),
			*GetNameSafe(Door), MontageLength, MontageLength > 0.f ? OpenDelay : 0.f, static_cast<int32>(PendingBreachType));
		return;
	}

	case EBreachPhase::PlayingMontage:
	{
		PhaseElapsed += DeltaSeconds;
		if (MontageLength > 0.f && PhaseElapsed < OpenDelay) return;

		OpenDoorNow(AIC, Door);
		Phase = EBreachPhase::Holding;
		PhaseElapsed = 0.f;
		return;
	}

	case EBreachPhase::Holding:
	{
		PhaseElapsed += DeltaSeconds;
		const float MontageRemainder = FMath::Max(0.f, MontageLength - OpenDelay);
		if (PhaseElapsed < MontageRemainder) return;

		// Montage done — clear the doorway. Loud walks in past the swung leaf; Tactical/Quiet
		// sidestep outside beside the frame. Attempted once; a decline falls back to the hold.
		// A firefight that started during the montage skips the reposition entirely — never walk
		// deeper into a room that just started shooting.
		if (!bRepositionDeclined && !(bInCombat && !bHadCombatTargetAtStart))
		{
			FVector PostPoint;
			const bool bEnterRoom = (PendingBreachType == EBreachType::Loud);
			if (IBreachable::Execute_GetPostBreachPoint(Door, Pawn, bEnterRoom, PostPoint))
			{
				// The enter point must be genuinely reachable: a partial path "succeeds" by
				// stopping in the doorway — exactly where the companion must not stand.
				FAIMoveRequest MoveReq(PostPoint);
				MoveReq.SetAcceptanceRadius(RepositionAcceptRadius);
				MoveReq.SetAllowPartialPath(!bEnterRoom);
				MoveReq.SetProjectGoalLocation(true);
				MoveReq.SetUsePathfinding(true);

				if (AIC->MoveTo(MoveReq) != EPathFollowingRequestResult::Failed)
				{
					UE_LOG(LogCompanionBreach, Log, TEXT("TickTask: repositioning (%s) after breach of %s"),
						bEnterRoom ? TEXT("enter") : TEXT("sidestep"), *GetNameSafe(Door));
					Phase = EBreachPhase::Repositioning;
					PhaseElapsed = 0.f;
					return;
				}
			}
			bRepositionDeclined = true;
		}

		// No point / move failed: hold in place as before — a fresh firefight cuts the hold short.
		const float HoldTime = (bInCombat && !bHadCombatTargetAtStart) ? 0.f : PostBreachWaitTime;
		if (PhaseElapsed < MontageRemainder + HoldTime) return;

		AIC->ClearActiveCommand();
		CachedDoor.Reset();
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	case EBreachPhase::Repositioning:
	{
		PhaseElapsed += DeltaSeconds;
		// Finish on arrival, on timeout, or immediately when a fresh firefight starts — the
		// breach itself already succeeded, the combat brain should take over.
		const bool bFreshCombat = bInCombat && !bHadCombatTargetAtStart;
		if (!bFreshCombat && AIC->GetMoveStatus() != EPathFollowingStatus::Idle && PhaseElapsed < RepositionTimeout)
			return;

		if (bFreshCombat) AIC->StopMovement();
		AIC->ClearActiveCommand();
		CachedDoor.Reset();
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}
	}
}

void UBTTask_CompanionBreach::StartAlign(APawn* Pawn)
{
	if (ACompanionAIController* AIC = Cast<ACompanionAIController>(Pawn->GetController()))
	{
		AIC->StopMovement();
		AIC->ClearFocus(EAIFocusPriority::Gameplay); // nothing may fight the breach facing
	}

	AlignStartLocation = Pawn->GetActorLocation();
	AlignStartYaw = Pawn->GetActorRotation().Yaw;
	AlignElapsed = 0.f;
	Phase = EBreachPhase::Aligning;
}

void UBTTask_CompanionBreach::OpenDoorNow(ACompanionAIController* AIC, AActor* Door)
{
	// The door may have been opened by something else during the wind-up — swing/noise only once.
	if (!IBreachable::Execute_CanBreach(Door))
	{
		UE_LOG(LogCompanionBreach, Log, TEXT("OpenDoorNow: %s opened elsewhere during wind-up — skipping swing"), *GetNameSafe(Door));
		return;
	}

	IBreachable::Execute_Breach(Door, AIC->GetPawn());

	const UCompanionTuningDataAsset* Tuning = AIC->GetTuning();
	const FBreachNoiseProfile* Profile = Tuning ? Tuning->BreachNoise.Find(PendingBreachType) : nullptr;
	if (Profile && Profile->Loudness > 0.f && Profile->MaxRange > 0.f)
	{
		UAISense_Hearing::ReportNoiseEvent(AIC->GetWorld(), Door->GetActorLocation(),
			Profile->Loudness, AIC->GetPawn(), Profile->MaxRange, TEXT("Breach"));
	}

	UE_LOG(LogCompanionBreach, Log, TEXT("OpenDoorNow: breached %s (type=%d)"),
		*GetNameSafe(Door), static_cast<int32>(PendingBreachType));
}

EBTNodeResult::Type UBTTask_CompanionBreach::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// A replacement command should not leave the breach anim playing — the montage remainder
	// still runs into the Holding phase, so cover both.
	const float MontageRemainder = FMath::Max(0.f, MontageLength - OpenDelay);
	const bool bMontageStillPlaying = (Phase == EBreachPhase::PlayingMontage)
		|| (Phase == EBreachPhase::Holding && PhaseElapsed < MontageRemainder);
	if (bMontageStillPlaying)
	{
		if (AAIController* AIC = OwnerComp.GetAIOwner())
			if (ACharacter* Character = Cast<ACharacter>(AIC->GetPawn())) Character->StopAnimMontage();
	}

	FailAndClear(OwnerComp);
	return EBTNodeResult::Aborted;
}

FString UBTTask_CompanionBreach::GetStaticDescription() const
{
	return FString::Printf(TEXT("Breach: align to stand point (accept %.0f cm), montage, door at contact"), StandPointAcceptRadius);
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
	Phase = EBreachPhase::MovingToDoor;
	bHasStandPoint = false;
	bRepositionDeclined = false;
	AlignElapsed = 0.f;
	PhaseElapsed = 0.f;
	MontageLength = 0.f;
}
