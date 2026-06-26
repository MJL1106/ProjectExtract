// BT task -- companion coordinated takedown. Approaches victim (knife: crouched sneak),
// arms the takedown on CompanionCharacter, then waits for the player's commit signal.

#include "BTTask_CompanionTakedown.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "CompanionCharacter.h"
#include "CompanionAIController.h"
#include "EnemyCharacter.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

UBTTask_CompanionTakedown::UBTTask_CompanionTakedown()
{
	NodeName = TEXT("Companion Takedown");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_CompanionTakedown::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!IsValid(AIC)) return EBTNodeResult::Failed;

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	if (!IsValid(Companion)) return EBTNodeResult::Failed;

	AActor* Victim = Cast<AActor>(BB->GetValueAsObject(CommandTargetActorKey.SelectedKeyName));
	if (!IsValid(Victim)) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Victim);
	if (!IsValid(Enemy) || !Enemy->IsTakedownEligible()) return EBTNodeResult::Failed;

	const uint8 MethodRaw = BB->GetValueAsEnum(TakedownMethodKey.SelectedKeyName);
	CachedMethod = static_cast<ETakedownMethod>(MethodRaw);
	CachedVictim = Victim;
	ApproachElapsed = 0.f;
	ArmedHoldElapsed = 0.f;
	bMoveRequestSent = false;
	CachedKnifeAnchor = FVector::ZeroVector;

	Companion->StopWeaponFire();

	// Arm at task start for BOTH methods so the player-commit delegate is bound
	// for the entire approach. Execution is gated on (bPlayerCommitted AND bInPosition).
	Companion->ArmCommandedTakedown(Victim, CachedMethod);

	if (CachedMethod == ETakedownMethod::Knife)
	{
		Companion->Crouch();
		Companion->SetTakedownCrouchApproach(true);

		CachedKnifeAnchor = ComputeKnifeAnchor(Victim);
		const EPathFollowingRequestResult::Type MoveResult = AIC->MoveToLocation(
			CachedKnifeAnchor, KnifeApproachAcceptRadius, /*bStopOnOverlap=*/true,
			/*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true);

		if (MoveResult == EPathFollowingRequestResult::Failed)
		{
			UE_LOG(LogCompanion, Warning, TEXT("CompanionTakedown: knife approach nav failed to %s"), *CachedKnifeAnchor.ToString());
			CleanupTask(Companion);
			return EBTNodeResult::Failed;
		}

		bMoveRequestSent = true;
		Phase = EPhase::Approaching;

		UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: knife approach started toward %s"), *GetNameSafe(Victim));
	}
	else
	{
		Phase = EPhase::Armed;
		Companion->SetTakedownInPosition(true);

		UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: shoot armed + in position on %s"), *GetNameSafe(Victim));
	}

	return EBTNodeResult::InProgress;
}

void UBTTask_CompanionTakedown::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!IsValid(AIC)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	if (!IsValid(Companion)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AActor* Victim = CachedVictim.Get();
	if (!IsValid(Victim))
	{
		UE_LOG(LogCompanion, Warning, TEXT("CompanionTakedown: victim invalidated"));
		CleanupTask(Companion);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Eligibility + timeout only checked pre-execution (Approaching / Armed).
	// Once Executing, the kill is committed — don't re-validate or timeout.
	if (Phase == EPhase::Approaching || Phase == EPhase::Armed)
	{
		AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Victim);
		if (!IsValid(Enemy) || !Enemy->IsTakedownEligible())
		{
			UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: victim no longer eligible (phase=%d)"), (int32)Phase);
			CleanupTask(Companion);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}
	}

	switch (Phase)
	{
	case EPhase::Approaching:
	{
		ApproachElapsed += DeltaSeconds;
		if (ApproachElapsed > KnifeApproachTimeout)
		{
			UE_LOG(LogCompanion, Warning, TEXT("CompanionTakedown: knife approach timed out (%.1fs)"), ApproachElapsed);
			CleanupTask(Companion);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		const float DistToAnchor = FVector::Dist2D(Companion->GetActorLocation(), CachedKnifeAnchor);
		if (DistToAnchor <= KnifeApproachAcceptRadius)
		{
			AIC->StopMovement();
			Companion->SetTakedownCrouchApproach(false);
			Phase = EPhase::Armed;
			ArmedHoldElapsed = 0.f;
			Companion->SetTakedownInPosition(true);

			UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: knife in position on %s"), *GetNameSafe(Victim));
		}
		break;
	}

	case EPhase::Armed:
	{
		// Transition to Executing if CompanionCharacter has started kill logic
		if (Companion->IsCommandedTakedownExecuting())
		{
			Phase = EPhase::Executing;
			UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: execution started, pausing timeout"));
			break;
		}

		// Armed-phase timeout (only while waiting, not during execution)
		ArmedHoldElapsed += DeltaSeconds;
		if (ArmedHoldElapsed > ArmedHoldTimeout)
		{
			UE_LOG(LogCompanion, Warning, TEXT("CompanionTakedown: armed hold timed out (%.1fs)"), ArmedHoldElapsed);
			CleanupTask(Companion);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		// Check if execution completed instantly (e.g. shoot with zero delay)
		if (!Companion->IsCommandedTakedownArmed())
		{
			Phase = EPhase::Done;
			CleanupTask(Companion);
			UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: completed"));
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		break;
	}

	case EPhase::Executing:
	{
		// Kill is committed — just wait for montage/shot to finish
		if (!Companion->IsCommandedTakedownArmed())
		{
			Phase = EPhase::Done;
			CleanupTask(Companion);
			UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: execution completed"));
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		break;
	}

	case EPhase::Done:
	{
		CleanupTask(Companion);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
	}
}

void UBTTask_CompanionTakedown::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!IsValid(AIC)) return;

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	CleanupTask(Companion);
}

EBTNodeResult::Type UBTTask_CompanionTakedown::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (IsValid(AIC))
	{
		ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
		CleanupTask(Companion);
	}

	return EBTNodeResult::Aborted;
}

void UBTTask_CompanionTakedown::CleanupTask(ACompanionCharacter* Companion)
{
	if (IsValid(Companion))
	{
		Companion->DisarmCommandedTakedown();
		Companion->SetTakedownCrouchApproach(false);
		Companion->UnCrouch();
	}

	if (IsValid(Companion))
	{
		if (ACompanionAIController* CompAIC = Cast<ACompanionAIController>(Companion->GetController()))
			CompAIC->ClearActiveCommand();
	}

	CachedVictim.Reset();
	Phase = EPhase::Done;
	bMoveRequestSent = false;
	ApproachElapsed = 0.f;
	ArmedHoldElapsed = 0.f;
	CachedKnifeAnchor = FVector::ZeroVector;
}

// FIX 6: Try +angle, -angle, 0 behind the victim; pick the first nav-reachable anchor
FVector UBTTask_CompanionTakedown::ComputeKnifeAnchor(const AActor* Victim) const
{
	if (!IsValid(Victim)) return FVector::ZeroVector;

	const FVector VictimLoc = Victim->GetActorLocation();
	const FVector Behind = -Victim->GetActorForwardVector();

	UWorld* World = Victim->GetWorld();
	UNavigationSystemV1* NavSys = IsValid(World) ? UNavigationSystemV1::GetCurrent(World) : nullptr;

	// Try preferred side, opposite side, then directly behind
	const float Angles[] = { KnifeAnchorAngleDegrees, -KnifeAnchorAngleDegrees, 0.f };
	for (float Angle : Angles)
	{
		const FVector Candidate = ComputeAnchorCandidate(VictimLoc, Behind, Angle);
		if (IsValid(NavSys))
		{
			FNavLocation NavLoc;
			if (NavSys->ProjectPointToNavigation(Candidate, NavLoc, FVector(200.f, 200.f, 200.f)))
				return NavLoc.Location;
		}
		else
		{
			return Candidate;
		}
	}

	// All failed nav projection — return the preferred side raw
	return ComputeAnchorCandidate(VictimLoc, Behind, KnifeAnchorAngleDegrees);
}

FVector UBTTask_CompanionTakedown::ComputeAnchorCandidate(const FVector& VictimLoc, const FVector& Behind, float AngleDeg) const
{
	const FRotator OffsetRot(0.f, AngleDeg, 0.f);
	const FVector AnchorDir = OffsetRot.RotateVector(Behind).GetSafeNormal2D();
	return VictimLoc + AnchorDir * KnifeAnchorDistance;
}

FString UBTTask_CompanionTakedown::GetStaticDescription() const
{
	return TEXT("Companion coordinated takedown (knife sneak / shoot aim, synced to player commit)");
}
