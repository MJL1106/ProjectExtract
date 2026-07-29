// BT task -- companion coordinated takedown. Approaches victim (knife: crouched sneak to stab
// range; shoot: repositions until it has a line), arms the takedown on CompanionCharacter, then
// waits for the player's commit signal.

#include "BTTask_CompanionTakedown.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "CompanionCharacter.h"
#include "CompanionAIController.h"
#include "EnemyCharacter.h"
#include "TakedownVolume.h"
#include "WeaponBase.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "HealthComponent.h"
#include "EngineUtils.h"

namespace
{
	// Half-extents (cm) for projecting a shoot anchor candidate onto the navmesh. Matches the knife
	// anchor's projection box so both methods accept the same amount of off-mesh slack.
	constexpr float ShootAnchorNavProjectExtent = 200.f;

	// Second fan pass multiplier. A lip or low wall that a single sidestep can't see past often
	// clears at double the step, and that is still cheaper than orbiting the victim.
	constexpr float ShootAnchorWideStepFactor = 2.f;

	// Ring fallback yaw offsets (degrees) applied to the victim->companion direction. Ordered
	// nearest-side first purely for readability; the pick itself is by smallest distance.
	constexpr float ShootRingYawOffsets[] = { 0.f, 45.f, -45.f, 90.f, -90.f, 135.f, -135.f, 180.f };

	// Grace (seconds) after issuing a reposition move before its Idle status is believed —
	// GetMoveStatus lags the request by a frame or two and would read as an instant dead settle.
	constexpr float ShootMoveSettleGrace = 0.25f;

	/** The shoot executor's line-of-sight test (ACompanionCharacter::ExecuteCommandedTakedown),
	 *  reproduced exactly: same ECC_Visibility channel, same ignore set (self, victim, held weapon)
	 *  and the same GetAimPointForTarget endpoint. Kept identical on purpose — a task that approved
	 *  a line the executor then rejected is what let a blocked shoot stand still and silently
	 *  abort at trigger time. Every line test in this task goes through here. */
	bool HasShootTakedownLosFromEye(const ACompanionCharacter* Companion, const FVector& EyeLocation,
		const AActor* Victim, AActor** OutBlocker = nullptr)
	{
		if (!IsValid(Companion) || !IsValid(Victim)) return false;

		const UWorld* World = Companion->GetWorld();
		if (!World) return false;

		FHitResult Hit;
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CompanionShootTakedownAnchor), false, Companion);
		TraceParams.AddIgnoredActor(Victim);

		const AWeaponBase* HeldWeapon = Companion->GetCurrentWeapon();
		if (IsValid(HeldWeapon)) TraceParams.AddIgnoredActor(HeldWeapon);

		const FVector TraceEnd = Companion->GetAimPointForTarget(Victim);
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, EyeLocation, TraceEnd, ECC_Visibility, TraceParams);
		if (bBlocked && OutBlocker) *OutBlocker = Hit.GetActor();
		return !bBlocked;
	}

	/** Line test from where the companion is standing right now — byte-for-byte the executor's own
	 *  trace, including its GetActorEyesViewPoint start. */
	bool HasShootTakedownLosInPlace(const ACompanionCharacter* Companion, const AActor* Victim, AActor** OutBlocker = nullptr)
	{
		if (!IsValid(Companion)) return false;

		FVector EyesLoc;
		FRotator EyesRot;
		Companion->GetActorEyesViewPoint(EyesLoc, EyesRot);
		return HasShootTakedownLosFromEye(Companion, EyesLoc, Victim, OutBlocker);
	}

	/** Line test from a candidate navmesh point. A nav point sits at FOOT level, so the eye is a
	 *  capsule half-height above it — offsetting from the actor origin instead would trace from
	 *  knee height and reject anchors the companion would actually see clean from. */
	bool HasShootTakedownLosFromNavPoint(const ACompanionCharacter* Companion, const FVector& NavPoint, const AActor* Victim)
	{
		if (!IsValid(Companion)) return false;

		FVector EyesLoc;
		FRotator EyesRot;
		Companion->GetActorEyesViewPoint(EyesLoc, EyesRot);

		const UCapsuleComponent* Capsule = Companion->GetCapsuleComponent();
		const float HalfHeight = IsValid(Capsule) ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
		const float EyeAboveFeet = EyesLoc.Z - (Companion->GetActorLocation().Z - HalfHeight);

		return HasShootTakedownLosFromEye(Companion, NavPoint + FVector(0.f, 0.f, EyeAboveFeet), Victim);
	}
}

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
	ShootMoveIssuedElapsed = 0.f;
	ShootLosRecheckElapsed = 0.f;
	bShootReanchorUsed = false;
	ArmedReanchorCount = 0;

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
	else if (!BeginShootTakedown(Companion, AIC, Victim))
	{
		CleanupTask(Companion);
		return EBTNodeResult::Failed;
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
		// Instigator-aware: the victim is reserved to us from ArmCommandedTakedown, so a drift to
		// Searching (the alert ratchet waking the level off the player's missed shot) no longer
		// invalidates a takedown already lined up. Only the victim reaching Combat does.
		AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Victim);
		if (!IsValid(Enemy) || !Enemy->IsTakedownEligibleFor(Companion))
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
		// Shoot approaches to open a LINE, not to reach stab range, so it owns its own tick —
		// the knife gating below (distance to victim, re-issue MoveToActor) would walk a shooter
		// straight into the victim's face.
		if (CachedMethod == ETakedownMethod::Shoot)
		{
			if (TickShootApproach(OwnerComp, AIC, Companion, Victim, DeltaSeconds)) return;
			break;
		}

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

		// The victim can walk behind cover during the settle, and the executor's own LoS gate would
		// then abort the takedown outright at trigger time. Re-test on a throttle (a full trace
		// every tick for the whole delay is wasted work) and walk the line back rather than firing
		// into geometry. Never re-tested once committed: from there the kill is already registered
		// and the shot is cosmetic, which is why Executing is excluded above.
		if (CachedMethod == ETakedownMethod::Shoot && !bAutonomousCommitSent)
		{
			ShootLosRecheckElapsed += DeltaSeconds;
			if (ShootLosRecheckElapsed >= ShootLosRecheckInterval)
			{
				ShootLosRecheckElapsed = 0.f;
				if (!HasShootTakedownLosInPlace(Companion, Victim))
				{
					// Cap armed-phase re-anchors: a victim patrolling across a pillar edge
					// flickers the trace indefinitely, each cycle costing a full ComputeShootAnchor
					// (worst-case 18 nav projections + 18 line traces) plus visible stutter-stop.
					++ArmedReanchorCount;
					if (ArmedReanchorCount > MaxArmedReanchors)
					{
						UE_LOG(LogCompanion, Warning, TEXT("CompanionTakedown: armed shoot line lost %d times on %s — falling back to gunfire"), ArmedReanchorCount, *GetNameSafe(Victim));
						CleanupTask(Companion);
						return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
					}

					// The world moved since the anchor was picked, so the approach earns a fresh
					// re-anchor budget instead of inheriting the last approach's spent one.
					bShootReanchorUsed = false;
					if (!BeginShootApproach(Companion, AIC, Victim))
					{
						UE_LOG(LogCompanion, Warning, TEXT("CompanionTakedown: armed shoot line lost on %s, no anchor left — falling back to gunfire"), *GetNameSafe(Victim));
						CleanupTask(Companion);
						return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
					}

					UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: armed shoot line lost on %s — repositioning (%d/%d)"), *GetNameSafe(Victim), ArmedReanchorCount, MaxArmedReanchors);
					break;
				}
			}
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
	// Commit to the victim. Tearing down with it still alive used to simply drop it — the companion
	// fell through to the combat branch, picked whatever was nearest, and the enemy it had been
	// lined up on never died. Latch it instead so the companion finishes it with normal gunfire,
	// then resumes normal targeting. Covers both loss routes: the ratchet waking the victim past
	// the grandfather window, and the shoot path aborting on a blocked line. A victim already held
	// in a finisher is excluded — that kill is landing, it just hasn't registered yet.
	if (IsValid(Companion) && TakedownCommitHoldSeconds > 0.f)
	{
		AEnemyCharacter* Unfinished = Cast<AEnemyCharacter>(CachedVictim.Get());
		if (IsValid(Unfinished) && !Unfinished->IsTakedownPending())
		{
			const UHealthComponent* UnfinishedHealth = Unfinished->GetHealthComponent();
			if (!IsValid(UnfinishedHealth) || !UnfinishedHealth->IsDead())
				Companion->LatchForcedCombatTarget(Unfinished, TakedownCommitHoldSeconds);
		}
	}

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
	ShootMoveIssuedElapsed = 0.f;
	ShootLosRecheckElapsed = 0.f;
	bShootReanchorUsed = false;
	ArmedReanchorCount = 0;
}

bool UBTTask_CompanionTakedown::BeginShootTakedown(ACompanionCharacter* Companion, AAIController* AIC, AActor* Victim)
{
	// Arming blind was the bug: the executor re-traces this exact line at trigger time and aborts on
	// a block, so a companion ordered to shoot through a wall stood still for the whole autonomous
	// delay and then gave up without ever trying to see its victim.
	AActor* Blocker = nullptr;
	if (HasShootTakedownLosInPlace(Companion, Victim, &Blocker))
	{
		ArmShootInPlace(Companion, AIC);
		UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: shoot armed + in position on %s"), *GetNameSafe(Victim));
		return true;
	}

	if (!BeginShootApproach(Companion, AIC, Victim))
	{
		UE_LOG(LogCompanion, Warning, TEXT("CompanionTakedown: no firing anchor with a line to %s (blocked by %s)"),
			*GetNameSafe(Victim), *GetNameSafe(Blocker));
		return false;
	}

	UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: shoot line blocked by %s — repositioning on %s"),
		*GetNameSafe(Blocker), *GetNameSafe(Victim));
	return true;
}

bool UBTTask_CompanionTakedown::BeginShootApproach(ACompanionCharacter* Companion, AAIController* AIC, AActor* Victim)
{
	if (!IsValid(Companion) || !IsValid(AIC)) return false;

	FVector Anchor;
	if (!ComputeShootAnchor(Companion, Victim, Anchor)) return false;

	const EPathFollowingRequestResult::Type MoveResult = AIC->MoveToLocation(
		Anchor, ShootApproachAcceptRadius, /*bStopOnOverlap=*/true,
		/*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true);

	if (MoveResult == EPathFollowingRequestResult::Failed) return false;

	// A blocked line is not "in position". Clearing the flag stops a player commit landing mid-walk
	// and driving the executor's LoS gate into the very geometry we are walking around.
	Companion->SetTakedownInPosition(false);
	bMoveRequestSent = true;
	ShootMoveIssuedElapsed = 0.f;
	Phase = EPhase::Approaching;
	return true;
}

void UBTTask_CompanionTakedown::ArmShootInPlace(ACompanionCharacter* Companion, AAIController* AIC)
{
	if (!IsValid(Companion)) return;

	// Only ever stops OUR reposition move: at execute time no move has been issued yet and the
	// previous task's movement is not ours to cancel.
	if (bMoveRequestSent && IsValid(AIC)) AIC->StopMovement();

	bMoveRequestSent = false;

	// Reset ArmedHoldElapsed only on the FIRST arm of the task run. A re-arm after an armed-phase
	// LoS re-break must NOT reset it, or a victim patrolling across a pillar edge defeats
	// ArmedHoldTimeout indefinitely: each LoS break cycles Armed->Approaching->Armed and zeroes
	// the timeout. The old guard tested Phase != Armed, but Phase is never Armed at this call
	// site (callers are in Done/initial or Approaching), so the guard was dead code and the hold
	// elapsed was zeroed on every re-arm. Gate on the re-anchor counter instead: it is zero only
	// on the very first arm and increments on every LoS re-break that returns to Approaching.
	if (ArmedReanchorCount == 0)
		ArmedHoldElapsed = 0.f;

	Phase = EPhase::Armed;
	ShootLosRecheckElapsed = 0.f;

	// Last: a pending player commit executes the takedown synchronously inside this call, so all
	// phase state must already be settled when it fires.
	Companion->SetTakedownInPosition(true);
}

bool UBTTask_CompanionTakedown::TickShootApproach(UBehaviorTreeComponent& OwnerComp, AAIController* AIC,
	ACompanionCharacter* Companion, AActor* Victim, float DeltaSeconds)
{
	ApproachElapsed += DeltaSeconds;
	ShootMoveIssuedElapsed += DeltaSeconds;

	// Cumulative across every re-approach on purpose: a victim that keeps breaking the line degrades
	// to normal gunfire (CleanupTask latches it) instead of looping approach->armed->approach.
	if (ApproachElapsed > ShootApproachTimeout)
	{
		UE_LOG(LogCompanion, Warning, TEXT("CompanionTakedown: shoot approach timed out (%.1fs) — falling back to gunfire"), ApproachElapsed);
		CleanupTask(Companion);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return true;
	}

	// Arm the INSTANT the line clears, wherever that happens to be. The anchor is only a guess at
	// where the line opens up, and walking the rest of the way burns the unaware window.
	if (HasShootTakedownLosInPlace(Companion, Victim))
	{
		ArmShootInPlace(Companion, AIC);
		UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: shoot line cleared en route — armed on %s"), *GetNameSafe(Victim));
		return false;
	}

	// Move finished (reached or stalled) with the line still blocked — the anchor was stale or
	// unreachable. Re-anchor once; a second dead settle means no reachable firing spot exists.
	const bool bMoveSettled = bMoveRequestSent
		&& AIC->GetMoveStatus() == EPathFollowingStatus::Idle
		&& ShootMoveIssuedElapsed > ShootMoveSettleGrace;
	if (!bMoveSettled) return false;

	if (!bShootReanchorUsed && BeginShootApproach(Companion, AIC, Victim))
	{
		bShootReanchorUsed = true;
		UE_LOG(LogCompanion, Log, TEXT("CompanionTakedown: shoot anchor dead — re-anchoring on %s"), *GetNameSafe(Victim));
		return false;
	}

	UE_LOG(LogCompanion, Warning, TEXT("CompanionTakedown: shoot reposition dead-ended on %s — falling back to gunfire"), *GetNameSafe(Victim));
	CleanupTask(Companion);
	FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	return true;
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

// Cheapest first: a step off our own position, then the same step doubled, and only then a ring
// around the victim. The order matters for stealth as much as for cost — orbiting the victim walks
// the companion across its view cone, which is exactly how a silent takedown turns into a firefight.
bool UBTTask_CompanionTakedown::ComputeShootAnchor(const ACompanionCharacter* Companion, const AActor* Victim, FVector& OutAnchor) const
{
	if (!IsValid(Companion) || !IsValid(Victim)) return false;
	if (TryShootFanAnchor(Companion, Victim, ShootAnchorStepDistance, OutAnchor)) return true;
	if (TryShootFanAnchor(Companion, Victim, ShootAnchorStepDistance * ShootAnchorWideStepFactor, OutAnchor)) return true;
	return TryShootRingAnchor(Companion, Victim, OutAnchor);
}

bool UBTTask_CompanionTakedown::TryShootFanAnchor(const ACompanionCharacter* Companion, const AActor* Victim, float StepDistance, FVector& OutAnchor) const
{
	const FVector MyLoc = Companion->GetActorLocation();
	FVector ToVictim = Victim->GetActorLocation() - MyLoc;
	ToVictim.Z = 0.f;
	// Stacked on the victim — no meaningful fan basis; the ring pass handles that degenerate case.
	if (ToVictim.SizeSquared() <= KINDA_SMALL_NUMBER) return false;

	const FVector Forward = ToVictim.GetSafeNormal();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	const FVector Back = -Forward;   // backing up lowers the look-over angle past a ramp lip or low wall
	const FVector Offsets[] = { Right, -Right, Back, Back + Right, Back - Right };

	// Smallest displacement wins, so a short sidestep beats a wide swing — the same bias the combat
	// task's regain-LoS fan uses, and here it also keeps the companion out of the victim's cone.
	bool bFound = false;
	float BestDistSq = TNumericLimits<float>::Max();
	for (const FVector& Offset : Offsets)
	{
		FVector Candidate;
		if (!TryShootAnchorCandidate(Companion, Victim, MyLoc + Offset.GetSafeNormal() * StepDistance, Candidate)) continue;

		const float DistSq = FVector::DistSquared(Candidate, MyLoc);
		if (DistSq >= BestDistSq) continue;
		BestDistSq = DistSq;
		OutAnchor = Candidate;
		bFound = true;
	}
	return bFound;
}

bool UBTTask_CompanionTakedown::TryShootRingAnchor(const ACompanionCharacter* Companion, const AActor* Victim, FVector& OutAnchor) const
{
	const FVector MyLoc = Companion->GetActorLocation();
	const FVector VictimLoc = Victim->GetActorLocation();
	FVector ToCompanion = MyLoc - VictimLoc;
	ToCompanion.Z = 0.f;
	// Ring is measured from the victim->companion direction so yaw 0 stays on our own side; with the
	// two stacked there is no such direction and nothing to orbit.
	if (ToCompanion.SizeSquared() <= KINDA_SMALL_NUMBER) return false;

	const FVector BaseDir = ToCompanion.GetSafeNormal();

	bool bFound = false;
	float BestDistSq = TNumericLimits<float>::Max();
	for (const float Yaw : ShootRingYawOffsets)
	{
		const FVector Dir = FRotator(0.f, Yaw, 0.f).RotateVector(BaseDir);
		FVector Candidate;
		if (!TryShootAnchorCandidate(Companion, Victim, VictimLoc + Dir * ShootAnchorRingStandoff, Candidate)) continue;

		const float DistSq = FVector::DistSquared(Candidate, MyLoc);
		if (DistSq >= BestDistSq) continue;
		BestDistSq = DistSq;
		OutAnchor = Candidate;
		bFound = true;
	}
	return bFound;
}

bool UBTTask_CompanionTakedown::TryShootAnchorCandidate(const ACompanionCharacter* Companion, const AActor* Victim,
	const FVector& RawCandidate, FVector& OutProjected) const
{
	UWorld* World = Companion->GetWorld();
	UNavigationSystemV1* NavSys = IsValid(World) ? UNavigationSystemV1::GetCurrent(World) : nullptr;
	// No navmesh means no anchor we can promise to reach — better to fail into gunfire than to
	// send a move request at a point path-following will reject anyway.
	if (!IsValid(NavSys)) return false;

	FNavLocation NavLoc;
	if (!NavSys->ProjectPointToNavigation(RawCandidate, NavLoc, FVector(ShootAnchorNavProjectExtent))) return false;

	// Verify from the PROJECTED point, not the raw one: projection can slide a candidate metres onto
	// the nearest mesh, and the line we approve has to be the line from where the companion stands.
	OutProjected = NavLoc.Location;
	return HasShootTakedownLosFromNavPoint(Companion, OutProjected, Victim);
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
