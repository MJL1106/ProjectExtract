// BT task -- companion coordinated takedown. Approaches victim (knife: crouched sneak),
// arms the takedown on CompanionCharacter, then waits for the player's commit signal.

#include "BTTask_CompanionTakedown.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "CompanionCharacter.h"
#include "CompanionAIController.h"
#include "EnemyCharacter.h"
#include "TakedownVolume.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "HealthComponent.h"
#include "EngineUtils.h"

UBTTask_CompanionTakedown::UBTTask_CompanionTakedown()
{
	NodeName = TEXT("Companion Takedown");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_CompanionTakedown::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UE_LOG(LogCompanion, Warning, TEXT("[TakedownTask] ExecuteTask CALLED (BT command branch consumed the command)"));

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!IsValid(AIC)) return EBTNodeResult::Failed;

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	if (!IsValid(Companion)) return EBTNodeResult::Failed;

	AActor* Victim = Cast<AActor>(BB->GetValueAsObject(CommandTargetActorKey.SelectedKeyName));
	if (!IsValid(Victim)) { UE_LOG(LogCompanion, Warning, TEXT("[TakedownTask] no victim in BB (key='%s') -> Fail"), *CommandTargetActorKey.SelectedKeyName.ToString()); return EBTNodeResult::Failed; }

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Victim);
	if (!IsValid(Enemy) || !Enemy->IsTakedownEligible()) { UE_LOG(LogCompanion, Warning, TEXT("[TakedownTask] %s not eligible at execute -> Fail"), *GetNameSafe(Victim)); return EBTNodeResult::Failed; }

	UE_LOG(LogCompanion, Warning, TEXT("[TakedownTask] proceeding on %s"), *GetNameSafe(Victim));

	const uint8 MethodRaw = BB->GetValueAsEnum(TakedownMethodKey.SelectedKeyName);
	CachedMethod = static_cast<ETakedownMethod>(MethodRaw);
	CachedVictim = Victim;
	ApproachElapsed = 0.f;
	ArmedHoldElapsed = 0.f;
	bMoveRequestSent = false;
	CachedKnifeAnchor = FVector::ZeroVector;

	// Decide autonomous vs synced: union eligible enemies across all volumes containing this
	// victim. 2+ eligible sharing a volume = synced double-takedown; lone = autonomous solo.
	bAutonomous = true;
	{
		TSet<AEnemyCharacter*> EligibleShared;
		if (UWorld* W = Companion->GetWorld())
			for (TActorIterator<ATakedownVolume> It(W); It; ++It)
				if (It->ContainsEnemy(Enemy))
					It->AppendEligibleEnemies(EligibleShared);
		if (EligibleShared.Num() >= 2)
			bAutonomous = false;
	}
	AutonomousShootElapsed = 0.f;
	AutonomousShootDelay = FMath::RandRange(AutonomousShootDelayMin, AutonomousShootDelayMax);

	UE_LOG(LogCompanion, Warning, TEXT("[TakedownTask] %s method=%d autonomous=%d (shootDelay=%.2f)"),
		*GetNameSafe(Victim), (int32)CachedMethod, (int32)bAutonomous, AutonomousShootDelay);

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
	if ((Phase == EPhase::Approaching || Phase == EPhase::Armed)
		&& !Companion->IsCommandedTakedownExecuting())
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

		const float DistToVictim = FVector::Dist2D(Companion->GetActorLocation(), Victim->GetActorLocation());

		// "In position" is gated on proximity to the VICTIM, not to the behind-anchor.
		// KnifeAnchorDistance is comfortably under the enemy's takedown accept range, so
		// being within it guarantees ExecuteTakedown won't range-reject the knife.
		if (DistToVictim <= KnifeAnchorDistance)
		{
			AIC->StopMovement();
			Companion->SetTakedownCrouchApproach(false);
			Phase = EPhase::Armed;
			ArmedHoldElapsed = 0.f;
			Companion->SetTakedownInPosition(true);
			UE_LOG(LogCompanion, Warning, TEXT("CompanionTakedown: knife in position on %s (distToVictim=%.0f)"),
				*GetNameSafe(Victim), DistToVictim);
			break;
		}

		// The behind-anchor move finished (reached or stalled) but the companion is still
		// out of knife range — close the remaining gap by pathing straight at the victim.
		// Re-issued only once the prior move has settled, so it doesn't thrash every frame.
		const bool bMoveSettled = bMoveRequestSent
			&& AIC->GetMoveStatus() == EPathFollowingStatus::Idle
			&& ApproachElapsed > 0.25f;
		if (bMoveSettled)
		{
			UE_LOG(LogCompanion, Verbose, TEXT("CompanionTakedown: settled at distToVictim=%.0f (> KnifeAnchorDistance=%.0f) — closing in on victim"),
				DistToVictim, KnifeAnchorDistance);
			AIC->MoveToActor(Victim, KnifeApproachAcceptRadius, /*bStopOnOverlap=*/true,
				/*bUsePathfinding=*/true, /*bCanStrafe=*/false);
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

		// --- Autonomous path: solo takedown without waiting for player commit ---
		if (bAutonomous)
		{
			if (!bAutonomousCommitSent)
			{
				bool bFire = (CachedMethod == ETakedownMethod::Knife);   // knife: execute on arrival
				if (CachedMethod == ETakedownMethod::Shoot)
				{
					AutonomousShootElapsed += DeltaSeconds;
					bFire = (AutonomousShootElapsed >= AutonomousShootDelay);
				}

				if (bFire)
				{
					Companion->CommitTakedownNow();
					bAutonomousCommitSent = true;
				}
			}

			// Instant-complete safety (e.g. shoot with zero settle)
			if (!Companion->IsCommandedTakedownArmed())
			{
				Phase = EPhase::Done;
				CleanupTask(Companion);
				return FinishLatentTask(OwnerComp, CompletionResult());
			}
			break;
		}

		// --- Synced path: wait for player commit (existing behaviour) ---
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
			return FinishLatentTask(OwnerComp, CompletionResult());
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
			return FinishLatentTask(OwnerComp, CompletionResult());
		}
		break;
	}

	case EPhase::Done:
	{
		CleanupTask(Companion);
		return FinishLatentTask(OwnerComp, CompletionResult());
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
	bAutonomous = false;
	bAutonomousCommitSent = false;
	ApproachElapsed = 0.f;
	ArmedHoldElapsed = 0.f;
	AutonomousShootElapsed = 0.f;
	AutonomousShootDelay = 0.f;
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

EBTNodeResult::Type UBTTask_CompanionTakedown::CompletionResult() const
{
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(CachedVictim.Get());
	// Destroyed/invalid victim = killed (Succeeded). Otherwise check the health component.
	if (!IsValid(Enemy)) return EBTNodeResult::Succeeded;
	const UHealthComponent* HC = Enemy->GetHealthComponent();
	const bool bKilled = !IsValid(HC) || HC->IsDead();
	return bKilled ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}

FString UBTTask_CompanionTakedown::GetStaticDescription() const
{
	return TEXT("Companion coordinated takedown (knife sneak / shoot aim, synced to player commit)");
}
