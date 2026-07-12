// BT task — companion follows player in formation or sprints to reach them.

#include "BTTask_FollowPlayer.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "CompanionCharacter.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"

UBTTask_FollowPlayer::UBTTask_FollowPlayer()
{
	NodeName = TEXT("Follow Player");
	bNotifyTick = true;
	bNotifyTaskFinished = true; // REQUIRED — OnTaskFinished clears sprint latch (c62bdbf regression vector)
	bCreateNodeInstance = true; // bIsIdling / LastMoveTarget are per-instance state — must stay true
}

EBTNodeResult::Type UBTTask_FollowPlayer::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	const UCompanionTuningDataAsset* T = AIC ? AIC->GetTuning() : nullptr;
	if (!T) return EBTNodeResult::Failed;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	AActor* Player = Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player)) return EBTNodeResult::Failed;

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	if (!Companion) return EBTNodeResult::Failed;

	CachedController = AIC;
	CachedCompanion = Companion;
	CachedOwnerComp = &OwnerComp;

	LastMoveTarget = FVector::ZeroVector;
	bIsIdling = false;
	SprintLogAccumulator = 0.f;
	BlockedMoveReissueAccumulator = 0.f;
	LastSeenMode = Companion->GetMode();

	// Reset EQS slot state — stale slots from a previous run must not bleed into this one.
	bEqsQueryInProgress = false;
	bHasEqsTarget = false;
	TimeSinceLastEqs = EqsQueryInterval; // allow an immediate query on first tick
	EqsTarget = FVector::ZeroVector;

	// Clear any stale sprint flag from a prior abort (e.g. combat or revive re-entry).
	// Skip for sprint-to-target so the revive branch keeps sprint speed on entry.
	if (!bSprintToTarget)
		Companion->SetSprinting(false);

	return EBTNodeResult::InProgress;
}

void UBTTask_FollowPlayer::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ACompanionAIController* Controller = CachedController.Get();
	ACompanionCharacter* Companion = CachedCompanion.Get();
	if (!Controller || !Companion) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const UCompanionTuningDataAsset* T = Controller->GetTuning();
	if (!T) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AActor* Player = Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const FVector PlayerLocation = Player->GetActorLocation();
	const FVector CompanionLocation = Companion->GetActorLocation();
	const float DistToPlayer = FVector::Dist(CompanionLocation, PlayerLocation);

	// Mode change drops the idle latch and forces a move re-issue so the new formation applies now.
	if (Companion->GetMode() != LastSeenMode)
	{
		LastSeenMode = Companion->GetMode();
		bIsIdling = false;
		LastMoveTarget = FVector::ZeroVector;
	}

	// Stealth catch-up staging — evaluated before the idle/back-out early-returns so the stage
	// releases as soon as the companion closes the gap. One-way ladder with wide bands: each stage
	// is entered at its threshold but only dropped one band lower — without the hysteresis a player
	// walking away parks the companion exactly on the sprint boundary (uncrouched closes at
	// +sprint-walk, crouched loses at walk-crouch) and it flip-flops crouch/uncrouch every tick.
	if (Companion->IsStealthActive())
	{
		const EStealthCatchup Current = Companion->GetStealthCatchup();
		// Read-time floor (mirrors EffMinSep/EffStandoff below): a break distance at/below the
		// fast-crouch threshold would collapse sprint entry and exit onto one boundary and
		// resurrect the crouch/uncrouch flip-flop the ladder exists to prevent.
		const float EffSprintBreak = FMath::Max(T->StealthSprintBreakDistance, T->SprintDistanceThreshold + 200.f);
		EStealthCatchup Stage;
		if (T->bStealthAllowSprintCatchup && DistToPlayer > EffSprintBreak)
			Stage = EStealthCatchup::Sprint;
		else if (Current == EStealthCatchup::Sprint && DistToPlayer > T->SprintDistanceThreshold)
			Stage = EStealthCatchup::Sprint;   // hold sprint until back inside the fast-crouch band
		else if (DistToPlayer > T->SprintDistanceThreshold)
			Stage = EStealthCatchup::FastCrouch;
		else if (Current != EStealthCatchup::None && DistToPlayer > T->SprintDistanceThreshold * 0.7f)
			Stage = EStealthCatchup::FastCrouch; // hold the hustle until clearly caught up
		else
			Stage = EStealthCatchup::None;
		Companion->SetStealthCatchup(Stage);
	}
	else
		Companion->SetStealthCatchup(EStealthCatchup::None);

	UE_LOG(LogCompanion, VeryVerbose, TEXT("[FollowPlayer] Tick: Dist=%.0f bIsIdling=%d Sprint=%d MaxWalkSpeed=%.0f Vel=%.0f"),
		DistToPlayer, bIsIdling ? 1 : 0,
		Companion->IsSprinting() ? 1 : 0,
		Companion->GetCharacterMovement() ? Companion->GetCharacterMovement()->MaxWalkSpeed : -1.0f,
		Companion->GetVelocity().Size2D());

	// Sprint mode: go directly to the player and Succeed on arrival so parent Sequence
	// (e.g. revive) can advance to the next task.
	if (bSprintToTarget)
	{
		// Arrival derives from the REVIVE range only — never the follow AcceptableRadius. That value
		// is tuned for formation feel (live DA: 30cm — physically unreachable through two colliding
		// capsules), and any mismatch with RevivePlayer's range silently dead-loops the revive
		// sequence with the companion standing pressed against the body, which is exactly what
		// killed the first two rescue playtests. Floored above the two-capsule collision minimum
		// (~68cm) so no ReviveProximityRadius retune can reintroduce the unreachable-arrival loop.
		const float ArrivalRadius = FMath::Max(Companion->ReviveProximityRadius * 0.75f, 90.f);

		if (DistToPlayer <= ArrivalRadius)
		{
			UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] SPRINT-TO-TARGET arrived (Dist=%.0f <= %.0f) — Succeed"), DistToPlayer, ArrivalRadius);
			Controller->StopMovement();
			Companion->SetSprinting(false);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}

		Companion->SetSprinting(true);

		// Approach diagnostic at ~1Hz — a rescue that stalls short of the arrival radius must be
		// visible in the log (dist vs threshold), not inferred.
		SprintLogAccumulator += DeltaSeconds;
		if (SprintLogAccumulator >= 1.f)
		{
			SprintLogAccumulator = 0.f;
			UE_LOG(LogCompanionAI, Log, TEXT("%s: RESCUE APPROACH dist=%.0f arrival=%.0f vel=%.0f"),
				*Companion->GetName(), DistToPlayer, ArrivalRadius, Companion->GetVelocity().Size2D());
		}

		// Rescue destination sits on the COVERED side of the body — offset away from the current
		// combat threat so the companion slides in behind the downed player instead of kneeling
		// in the open fire lane. Offset + move acceptance must stay strictly inside the arrival
		// ring (worst-case stop = offset + acceptance), or a covered-side approach completes its
		// move outside the raw-player-distance arrival check and stalls.
		FVector RescueDest = PlayerLocation;
		if (const AActor* Threat = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget)))
		{
			FVector Away = (PlayerLocation - Threat->GetActorLocation()).GetSafeNormal2D();
			if (!Away.IsNearlyZero())
				RescueDest = PlayerLocation + Away * (ArrivalRadius * 0.4f);
		}

		// Only re-issue move if the destination has shifted significantly — avoids restarting the
		// path every tick which can stutter-step the companion.
		if (FVector::Dist(RescueDest, LastMoveTarget) > 100.0f)
		{
			LastMoveTarget = RescueDest;
			Controller->MoveToLocation(RescueDest, ArrivalRadius * 0.25f, false, true, false, true);
		}
		return;
	}

	// --- Formation (non-sprint) mode: stay near the player indefinitely ---

	// Clamp the three follow distances to a sensible ordering at read time so mistuning can't
	// produce unreachable states: EffMinSep < AcceptableRadius, EffStandoff > EffMinSep.
	constexpr float FollowDistMargin = 30.f;
	constexpr float BackoutDeadband  = 20.f;
	const float EffMinSep   = FMath::Min(T->FollowMinSeparation,  T->AcceptableRadius - FollowDistMargin);
	const float EffStandoff = FMath::Max(T->FollowIdleStandoff,   EffMinSep + FollowDistMargin);

	// Formation point is computed up front — Combat mode keys its idle/hysteresis gates on the
	// distance to the LEAD point, not to the player (a player squeezing past a leading companion
	// would otherwise latch it idle at their side until the gap exceeded 1.5x AcceptableRadius).
	// Mode shapes the formation: Combat leads AHEAD of the player (capped at the lead distance by
	// construction), Stealth tucks in tight behind, Normal keeps the standard offsets.
	const ACharacter* PlayerChar = Cast<ACharacter>(Player);
	const ECompanionMode Mode = Companion->GetMode();
	const bool bCombatLead = Mode == ECompanionMode::Combat;

	// Floor at read time: a lead inside AcceptableRadius would idle the companion at the player's
	// side and never take point (mirrors the EffMinSep/EffStandoff read-time clamps above).
	const float LeadDistance = FMath::Max(T->CombatModeLeadDistance, T->AcceptableRadius + FollowDistMargin);

	float OffsetBack = T->FormationOffsetBack;
	float OffsetRight = T->FormationOffsetRight;
	if (bCombatLead)
	{
		OffsetBack = -LeadDistance; // negative back = in front
		OffsetRight = T->CombatModeLeadOffsetRight;
	}
	else if (Companion->IsStealthActive())
	{
		OffsetBack = T->StealthFormationOffsetBack;
		OffsetRight = T->StealthFormationOffsetRight;
	}

	FVector FormationDir;
	if (PlayerChar && PlayerChar->GetVelocity().SizeSquared() > 100.0f * 100.0f)
	{
		// Player is moving — formation relative to their movement direction
		const FVector MoveDir = PlayerChar->GetVelocity().GetSafeNormal2D();
		const FVector MoveRight = FVector::CrossProduct(FVector::UpVector, MoveDir);
		FormationDir = PlayerLocation - MoveDir * OffsetBack + MoveRight * OffsetRight;
	}
	else if (bCombatLead)
	{
		// Player stationary in Combat mode — take point along their facing.
		const FVector Facing = Player->GetActorForwardVector().GetSafeNormal2D();
		const FVector FacingRight = FVector::CrossProduct(FVector::UpVector, Facing);
		FormationDir = PlayerLocation + Facing * LeadDistance + FacingRight * OffsetRight;
	}
	else
	{
		// Player stationary — just maintain distance, stay where we are relative to player
		const FVector ToCompanion = (CompanionLocation - PlayerLocation).GetSafeNormal2D();
		FormationDir = PlayerLocation + ToCompanion * OffsetBack;
	}

	// Combat lead idles against the lead point; everything else against the player.
	const float IdleGateDist = bCombatLead ? FVector::Dist(CompanionLocation, FormationDir) : DistToPlayer;

	// Min-separation back-out: inside the hard floor → walk back to standoff rather than freezing in place.
	// Deadband on entry prevents the branch from thrashing when the player loiters right at the floor boundary.
	// Always keyed on player distance — this is the personal-space guard, mode-independent.
	if (DistToPlayer < EffMinSep - BackoutDeadband)
	{
		FVector AwayDir = (CompanionLocation - PlayerLocation).GetSafeNormal2D();
		if (AwayDir.IsNearlyZero())
			AwayDir = (-Player->GetActorForwardVector()).GetSafeNormal2D();
		if (AwayDir.IsNearlyZero())
			AwayDir = FVector(1.f, 0.f, 0.f);

		bIsIdling = false;
		Companion->SetSprinting(false);

		const FVector BackoutTarget = PlayerLocation + AwayDir * EffStandoff;
		if (FVector::Dist(BackoutTarget, LastMoveTarget) > 50.f)
		{
			LastMoveTarget = BackoutTarget;
			Controller->MoveToLocation(BackoutTarget, T->FollowBackoutAcceptRadius, false, true, false, true);
		}
		return;
	}

	// Close enough to the formation anchor — stop and idle
	if (IdleGateDist <= T->AcceptableRadius)
	{
		if (!bIsIdling)
		{
			UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] >>> ENTER IDLE branch (GateDist=%.0f)"), IdleGateDist);
			Controller->StopMovement();
			Companion->SetSprinting(false);
			bIsIdling = true;
		}
		return;
	}

	// Hysteresis — don't re-engage movement until outside double the radius
	if (bIsIdling && IdleGateDist < T->AcceptableRadius * 1.5f)
	{
		UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] HYSTERESIS return (GateDist=%.0f, idling, no SetSprinting)"), IdleGateDist);
		return;
	}

	if (bIsIdling)
		UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] >>> EXIT IDLE branch (GateDist=%.0f, resuming formation)"), IdleGateDist);

	bIsIdling = false;

	// Kick an async EQS request periodically when a query asset is set. EqsTarget overrides
	// FormationDir when valid; otherwise we fall through to the formation target unchanged.
	// Combat mode skips the EQS slot entirely — the query anchors slots around/behind the player,
	// which would fight the lead point; MoveToLocation's nav projection handles off-mesh leads.
	TimeSinceLastEqs += DeltaSeconds;
	if (FollowSlotQuery && !bCombatLead && !bEqsQueryInProgress && TimeSinceLastEqs >= EqsQueryInterval)
	{
		bEqsQueryInProgress = true;
		TimeSinceLastEqs = 0.f;
		FEnvQueryRequest QueryRequest(FollowSlotQuery, Companion);
		QueryRequest.Execute(EEnvQueryRunMode::SingleResult,
			FQueryFinishedSignature::CreateUObject(this, &UBTTask_FollowPlayer::OnFollowQueryFinished));
	}

	const FVector MoveTarget = (bHasEqsTarget && !bCombatLead) ? EqsTarget : FormationDir;

	// Blocked-move re-issue: MoveToLocation can silently fail to path (e.g. the player is standing
	// exactly on the formation anchor) and the path-following component settles to Idle without
	// ever crossing the >200cm shift check below — the companion parks short of the target
	// forever. Reset LastMoveTarget so the shift check passes and the move re-issues. This is a
	// true Idle-debounce, not a rate-limit: the accumulator only counts while the path is actually
	// Idle and resets the instant it isn't, so a companion still Moving toward a reachable target
	// never gets its LastMoveTarget yanked out from under it every 0.5s.
	const UPathFollowingComponent* PathFollowing = Controller->GetPathFollowingComponent();
	const bool bPathIdle = IsValid(PathFollowing) && PathFollowing->GetStatus() == EPathFollowingStatus::Idle;
	if (bPathIdle && !LastMoveTarget.IsNearlyZero())
	{
		BlockedMoveReissueAccumulator += DeltaSeconds;
		if (BlockedMoveReissueAccumulator >= BlockedMoveReissueCooldown)
		{
			BlockedMoveReissueAccumulator = 0.f;
			LastMoveTarget = FVector::ZeroVector;
		}
	}
	else
	{
		BlockedMoveReissueAccumulator = 0.f;
	}

	// Sprint catch-up + idle hysteresis still keyed on DistToPlayer (not the slot).
	// SetSprinting MUST stay before any early-return — preserves the c62bdbf sprint-latch fix.
	// Mirror the player's sprint too: kit BP owns player sprint state, so it's inferred from 2D
	// speed against the tuning threshold (kit walk 410; threshold sits between walk and sprint).
	// The mirror needs a distance floor — without it the companion sprints 2m to a formation
	// point because the player happened to be sprinting somewhere.
	const bool bPlayerSprinting = Player->GetVelocity().Size2D() > T->PlayerSprintSpeedThreshold;
	const bool bWantSprint = DistToPlayer > T->SprintDistanceThreshold
		|| (bPlayerSprinting && DistToPlayer > T->SprintDistanceThreshold * 0.5f);
	UE_LOG(LogCompanion, VeryVerbose, TEXT("[FollowPlayer] FORMATION branch: Dist=%.0f Thresh=%.0f EqsSlot=%d -> SetSprinting(%d)"),
		DistToPlayer, T->SprintDistanceThreshold, bHasEqsTarget ? 1 : 0, bWantSprint ? 1 : 0);

	Companion->SetSprinting(bWantSprint);

	// Only re-issue move if target shifted significantly
	if (FVector::Dist(MoveTarget, LastMoveTarget) < 200.0f)
		return;

	LastMoveTarget = MoveTarget;

	Controller->MoveToLocation(MoveTarget, T->AcceptableRadius * 0.5f, false, true, false, true);
}

void UBTTask_FollowPlayer::OnFollowQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	bEqsQueryInProgress = false;

	// Stale-callback guard — task may have been aborted/reset between dispatch and callback.
	// CachedOwnerComp is cleared in OnTaskFinished, so its absence means a stale fire.
	if (!CachedOwnerComp || !CachedCompanion.IsValid()) return;

	if (Result.IsValid() && Result->IsSuccessful() && Result->Items.Num() > 0)
	{
		EqsTarget = Result->GetItemAsLocation(0);
		bHasEqsTarget = true;
	}
	else
	{
		bHasEqsTarget = false;
	}
}

void UBTTask_FollowPlayer::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	if (ACompanionCharacter* Companion = CachedCompanion.Get())
		Companion->SetSprinting(false);

	bEqsQueryInProgress = false;
	bHasEqsTarget = false;
	EqsTarget = FVector::ZeroVector;
	CachedOwnerComp = nullptr;
	CachedController.Reset();
	CachedCompanion.Reset();

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTTask_FollowPlayer::GetStaticDescription() const
{
	return FString::Printf(TEXT("Follow Player%s"), bSprintToTarget ? TEXT(" [SPRINT]") : TEXT(""));
}
