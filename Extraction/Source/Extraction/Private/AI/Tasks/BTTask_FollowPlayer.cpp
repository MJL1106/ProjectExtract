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
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "WeaponBase.h"

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
	bFightBiasQuery = false;
	bAllySpacingQuery = false;
	bAllySpacingRejectedAll = false;
	AllySlotSideSign = 0.f;
	LastMovingSideRight = FVector::ZeroVector;
	FightBiasThreatLocation = FVector::ZeroVector;

	// Reset the floor-transit detector — a re-entry mid-descent re-arms from the first sample.
	bHasPlayerZSample = false;
	PlayerZTransitEnvelope = 0.f;
	bWasPursuingPlayer = false;

	// Clear any stale sprint flag from a prior abort (e.g. combat or revive re-entry).
	// Skip for sprint-to-target so the revive branch keeps sprint speed on entry.
	// Pace always clears: rescue sprint must run at full SprintSpeed, never the catch-up tier.
	Companion->SetFollowCatchupPace(false);
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

	// Floor-transit detector samples every tick — the idle/back-out branches below early-return,
	// and the envelope must already be live the moment formation logic resumes.
	const ACharacter* PlayerChar = Cast<ACharacter>(Player);
	UpdatePlayerZTransit(PlayerChar, PlayerLocation.Z, DeltaSeconds);

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

	// Level-alignment gate: DistToPlayer is straight-line and lies through floors — a stairwell
	// player one floor down reads as ~400cm "close". Off-level the companion must never settle.
	// Read-time floor (mirrors EffMinSep/EffStandoff): a max-Z inside the idle radius would make
	// idle unreachable on any slope and sprint the companion in place at the player's side.
	const float ZDelta = FMath::Abs(CompanionLocation.Z - PlayerLocation.Z);
	const float EffMaxZDelta = T->FollowMaxZDelta <= 0.f
		? 0.f : FMath::Max(T->FollowMaxZDelta, T->AcceptableRadius + FollowDistMargin);
	const bool bZAligned = EffMaxZDelta <= 0.f || ZDelta <= EffMaxZDelta;

	// Formation point is computed up front — Combat mode keys its idle/hysteresis gates on the
	// distance to the LEAD point, not to the player (a player squeezing past a leading companion
	// would otherwise latch it idle at their side until the gap exceeded 1.5x AcceptableRadius).
	// Mode shapes the formation: Combat leads AHEAD of the player (capped at the lead distance by
	// construction), Stealth tucks in tight behind, Normal keeps the standard offsets.
	const ECompanionMode Mode = Companion->GetMode();
	const bool bCombatLead = Mode == ECompanionMode::Combat;

	// Front-to-back stagger for a non-primary companion (the armed extractee) — the lateral mirror
	// alone leaves the pair a symmetric wall across the player's rear. Primary = 0: every use is a no-op.
	const float BackBias = Companion->GetFormationBackBias(T->SecondaryFormationBackBias);

	// Floor at read time: a lead inside AcceptableRadius would idle the companion at the player's
	// side and never take point (mirrors the EffMinSep/EffStandoff read-time clamps above). The bias
	// shortens the lead, so it belongs INSIDE the Max — flooring the raw tunable let any
	// CombatModeLeadDistance under AcceptableRadius + bias + margin (e.g. 300, which still satisfies
	// the tunable's documented "must exceed AcceptableRadius" rule) push a secondary's lead point
	// back inside AcceptableRadius and resurrect the idles-at-the-player's-side bug.
	const float LeadDistance = FMath::Max(T->CombatModeLeadDistance - BackBias, T->AcceptableRadius + FollowDistMargin);

	float OffsetBack = T->FormationOffsetBack;
	float OffsetRight = T->FormationOffsetRight;
	if (bCombatLead)
	{
		OffsetBack = -LeadDistance; // negative back = in front; the bias is already inside LeadDistance
		OffsetRight = T->CombatModeLeadOffsetRight;
	}
	else if (Companion->IsStealthActive())
	{
		OffsetBack = T->StealthFormationOffsetBack;
		OffsetRight = T->StealthFormationOffsetRight;
	}

	// Applied after the mode branch so all three formations mirror with one edit. A second companion
	// (the armed extractee) runs this same task against this same tuning asset, so it would otherwise
	// compute a bit-identical anchor and pile into the primary. Primary = sign +1 / bias 0: no-op.
	OffsetRight *= Companion->GetFormationSideSign();
	if (!bCombatLead)
		OffsetBack += BackBias; // combat folded the bias into LeadDistance — adding it here double-counts

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
		// Player stationary in Combat mode — take point along their facing at -OffsetBack, which the
		// combat branch set to exactly LeadDistance (bias and floor both already inside it), so the
		// moving and stationary lead points can't drift apart.
		const FVector Facing = Player->GetActorForwardVector().GetSafeNormal2D();
		const FVector FacingRight = FVector::CrossProduct(FVector::UpVector, Facing);
		FormationDir = PlayerLocation + Facing * -OffsetBack + FacingRight * OffsetRight;
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

	// Close enough to the formation anchor — stop and idle. Never while off-level: "close"
	// through a floor slab isn't close.
	if (bZAligned && IdleGateDist <= T->AcceptableRadius)
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

	// Hysteresis — don't re-engage movement until outside double the radius. A player dropping
	// a floor breaks the latch immediately (bZAligned) even inside the band.
	if (bZAligned && bIsIdling && IdleGateDist < T->AcceptableRadius * 1.5f)
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

		// Fight-aware follow: with a fresh live-fight signal, request every passing slot and let
		// the callback prefer one with eye-line toward the threat. SingleResult otherwise.
		bFightBiasQuery = Controller->GetRecentAlertedThreat(T->FightSignalMaxAge, FightBiasThreatLocation);

		// A secondary companion needs the full set for the same reason: the single best slot is the
		// one the primary is already heading for, and the callback has to be able to pick another.
		bAllySpacingQuery = !Companion->IsPrimaryCompanion();

		// Side comes from the MIRRORED offset the anchor above actually uses, never a hardcoded "+1 is
		// the primary": FormationOffsetRight is a live serialised override, so a designer flipping it
		// negative swaps which side each companion anchors to, and a fixed assumption would leave the
		// filter rejecting the exact side the anchor is steering toward. Sign(0) = no side, no test.
		AllySlotSideSign = bAllySpacingQuery ? FMath::Sign(OffsetRight) : 0.f;

		FEnvQueryRequest QueryRequest(FollowSlotQuery, Companion);
		QueryRequest.Execute((bFightBiasQuery || bAllySpacingQuery) ? EEnvQueryRunMode::AllMatching : EEnvQueryRunMode::SingleResult,
			FQueryFinishedSignature::CreateUObject(this, &UBTTask_FollowPlayer::OnFollowQueryFinished));
	}

	// Wrong-floor EQS slots are discarded AND forgotten — not just down-scored: the query's donut
	// projects onto whatever navmesh is in vertical range, and its Distance3D scoring can rank a
	// slot directly above/below the player as near-perfect. Forgetting matters mid-stairwell: a
	// stale upper-floor slot must not be re-accepted when the player's Z drifts back into its band.
	if (bHasEqsTarget && EffMaxZDelta > 0.f && FMath::Abs(EqsTarget.Z - PlayerLocation.Z) > EffMaxZDelta)
		bHasEqsTarget = false;

	// Floor transit = pursue the player's location directly. Mid-flight the player's Z sits within
	// FollowMaxZDelta of the floor ABOVE, so upper-landing slots legitimately pass the wrong-floor
	// gate and yo-yo the companion on the stairs; the formation anchor (player Z, offset into the
	// stair slab) is off-mesh and only churns Failed->fallback. Off-level pursues unconditionally
	// for the same reason.
	const bool bPlayerZTransit = T->FollowPursuitZRate > 0.f
		&& PlayerZTransitEnvelope > T->FollowPursuitZRate;
	const bool bPursuePlayer = bPlayerZTransit || !bZAligned;
	if (bPursuePlayer != bWasPursuingPlayer)
	{
		bWasPursuingPlayer = bPursuePlayer;
		UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] PURSUIT %s (ZRate=%.0f zAligned=%d)"),
			bPursuePlayer ? TEXT("ENTER") : TEXT("EXIT"), PlayerZTransitEnvelope, (int32)bZAligned);
	}

	const FVector MoveTarget = bPursuePlayer ? PlayerLocation
		: (bHasEqsTarget && !bCombatLead) ? EqsTarget : FormationDir;

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
	// Off-level counts as far regardless of 3D distance — the real path runs the stairwell.
	// Stealth is exempt: its catch-up ladder owns pace there (sprint break deliberately unreachable).
	const bool bWantSprint = DistToPlayer > T->SprintDistanceThreshold
		|| (bPlayerSprinting && DistToPlayer > T->SprintDistanceThreshold * 0.5f)
		|| (!bZAligned && !Companion->IsStealthActive());
	UE_LOG(LogCompanion, VeryVerbose, TEXT("[FollowPlayer] FORMATION branch: Dist=%.0f Thresh=%.0f EqsSlot=%d Pursue=%d -> SetSprinting(%d)"),
		DistToPlayer, T->SprintDistanceThreshold, bHasEqsTarget ? 1 : 0, bPursuePlayer ? 1 : 0, bWantSprint ? 1 : 0);

	// Catch-up rides the reduced sprint tier (FollowCatchupSprintSpeed) — full SprintSpeed read as
	// too fast for closing formation gaps. Pace set before the sprint flag so the speed applies on
	// the same ApplyMovementSpeeds pass.
	Companion->SetFollowCatchupPace(bWantSprint);
	Companion->SetSprinting(bWantSprint);

	// Only re-issue move if target shifted significantly
	if (FVector::Dist(MoveTarget, LastMoveTarget) < 200.0f)
		return;

	LastMoveTarget = MoveTarget;

	// Pursuit targets the player's own location — always nav-adjacent, so projection is safe ON.
	// The anchor path keeps it OFF: a projected in-wall anchor lands on the wrong side of the wall.
	// Pursuit accepts at the standoff ring, not AcceptableRadius: reach the player's AREA — the
	// live radius (30cm) is unreachable through two capsules and would shove the companion into
	// the player's back until the transit envelope decays.
	const EPathFollowingRequestResult::Type MoveRes = bPursuePlayer
		? Controller->MoveToLocation(MoveTarget, EffStandoff, false, true, true, true)
		: Controller->MoveToLocation(MoveTarget, T->AcceptableRadius * 0.5f, false, true, false, true);

	// Off-mesh formation anchor fallback: with the companion on another floor, the stationary-player
	// anchor (player + 2D bearing toward the companion) can land inside the stairwell wall/void —
	// and with destination projection off, that move FAILS outright. The Idle-debounce above then
	// re-issued the same doomed move forever: companion parked one floor up while the player stood
	// still. Path to the player instead (always nav-adjacent; projection on; the acceptance ring
	// keeps follow distance) so the stairwell route actually runs.
	// LastMoveTarget stays keyed on the ANCHOR (set above): the dedup then suppresses re-issuing
	// the doomed anchor while the fallback move runs, and the Idle-debounce re-opens the gate if
	// the fallback itself ends short.
	// A failed PURSUIT move has no better target to fall back to — the Idle-debounce above retries
	// it once the path-following component settles.
	if (MoveRes == EPathFollowingRequestResult::Failed && !bPursuePlayer)
	{
		Controller->MoveToLocation(PlayerLocation, T->AcceptableRadius, false, true, true, true);
		UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] FORMATION anchor off-mesh (zAligned=%d) — falling back to player-location move"),
			(int32)bZAligned);
	}
}

void UBTTask_FollowPlayer::UpdatePlayerZTransit(const ACharacter* PlayerChar, float PlayerZ, float DeltaSeconds)
{
	// Envelope follower over the player's grounded vertical rate. Position-delta based: CMC keeps
	// reported velocity horizontal in walking mode, so GetVelocity().Z reads ~0 on stairs. Airborne
	// frames don't sample — a jump apex must not read as floor transit.
	const UCharacterMovementComponent* Move = PlayerChar ? PlayerChar->GetCharacterMovement() : nullptr;
	const bool bOnGround = !Move || Move->IsMovingOnGround();

	float Rate = 0.f;
	if (bOnGround)
	{
		if (bHasPlayerZSample)
			Rate = FMath::Min(FMath::Abs(PlayerZ - LastPlayerZ) / FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER), ZTransitRateClamp);
		LastPlayerZ = PlayerZ;
		bHasPlayerZSample = true;
	}

	// Slew both directions: the attack slope rejects single-tick blips (only a sustained rate can
	// climb to the threshold), the decay sets how long pursuit holds after the player settles.
	PlayerZTransitEnvelope = FMath::Clamp(Rate,
		FMath::Max(PlayerZTransitEnvelope - ZTransitEnvelopeDecay * DeltaSeconds, 0.f),
		PlayerZTransitEnvelope + ZTransitEnvelopeAttack * DeltaSeconds);
}

void UBTTask_FollowPlayer::OnFollowQueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
	bEqsQueryInProgress = false;

	// Stale-callback guard — task may have been aborted/reset between dispatch and callback.
	// CachedOwnerComp is cleared in OnTaskFinished, so its absence means a stale fire.
	if (!CachedOwnerComp || !CachedCompanion.IsValid()) return;

	if (!Result.IsValid() || !Result->IsSuccessful() || Result->Items.Num() == 0)
	{
		bHasEqsTarget = false;
		return;
	}

	ACompanionCharacter* Companion = CachedCompanion.Get();
	UWorld* World = Companion->GetWorld();

	// --- Ally filter inputs (non-primary companion only) ---
	// The follow query is player-anchored and identical for both companions, so its top slots are
	// the primary's. Resolve everything the filter needs once; each input gates only the test it
	// feeds, never the whole fill — a missing primary leaves nothing to space against, an
	// unresolvable player leaves no side frame, and either alone must not reject every slot.
	const ACompanionAIController* Controller = CachedController.Get();
	const UCompanionTuningDataAsset* T = Controller ? Controller->GetTuning() : nullptr;

	// TActorIterator, but at EqsQueryInterval and only for a secondary companion — the controller
	// caches no primary, and a registry for this one caller isn't worth the lifetime handling.
	const ACompanionCharacter* Primary = bAllySpacingQuery ? ACompanionCharacter::GetPrimaryCompanion(World) : nullptr;
	const bool bSpacingTest = bAllySpacingQuery && T && IsValid(Primary);
	const FVector PrimaryLocation = bSpacingTest ? Primary->GetActorLocation() : FVector::ZeroVector;
	const float MinSpacingSq = bSpacingTest ? FMath::Square(T->AllyFollowSlotMinSpacing) : 0.f;

	// Side frame for the half-plane test. While the player MOVES it mirrors TickTask's moving-player
	// formation branch exactly — same ACharacter cast, same speed gate, so the two can't diverge on a
	// non-ACharacter pawn — and latches that travel basis. While they STAND STILL the latch is reused
	// rather than the test being dropped: spacing alone only proves a slot is AllyFollowSlotMinSpacing
	// from the primary, never that it is mirrored, so with no side test the secondary took the slot
	// beside the primary and walked across the player's back (at rest the formation branch keeps
	// running — IdleGateDist is player distance, well outside AcceptableRadius — and a cross-body slot
	// clears the 200cm re-issue dedup, so it's a real traversal, not a blip). The latch is travel, not
	// actor-forward: it doesn't rotate when the player turns on the spot, so the accepted half-plane
	// can't swing out from under the stationary bearing-hold anchor and invert the wedge.
	FVector PlayerLocation = FVector::ZeroVector;
	FVector SideRight = FVector::ZeroVector;
	if (bAllySpacingQuery)
	{
		const UBlackboardComponent* BB = CachedOwnerComp->GetBlackboardComponent();
		const AActor* Player = BB ? Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName)) : nullptr;
		const ACharacter* PlayerChar = IsValid(Player) ? Cast<ACharacter>(Player) : nullptr;
		const bool bPlayerMoving = PlayerChar && PlayerChar->GetVelocity().SizeSquared() > 100.f * 100.f;
		if (bPlayerMoving)
		{
			PlayerLocation = PlayerChar->GetActorLocation();
			SideRight = FVector::CrossProduct(FVector::UpVector, PlayerChar->GetVelocity().GetSafeNormal2D());
			LastMovingSideRight = SideRight;
		}
		else if (IsValid(Player) && !LastMovingSideRight.IsNearlyZero())
		{
			PlayerLocation = Player->GetActorLocation();
			SideRight = LastMovingSideRight;
		}
	}
	// Spacing stays unconditional — the pile-up it prevents is just as real standing still. The side
	// test needs both a basis and a signed offset; either missing degrades to spacing-only, which is
	// the pre-latch behaviour rather than a reject-everything stall.
	const bool bSideTest = !SideRight.IsNearlyZero() && !FMath::IsNearlyZero(AllySlotSideSign);

	// Filter during the fill, never after: MaxFightBiasSlotTraces is the fight-bias TRACE budget, and
	// pre-truncating the window to it made "rejected everything" a steady state rather than an
	// anomaly (EQS sorts best-first and the top slots cluster where the primary stands), throwing a
	// whole query away twice a second. Walking until the window is FULL means an empty window really
	// does mean the entire result set was unusable. Both tests are skipped for a primary companion,
	// which therefore still takes the first MaxFightBiasSlotTraces items in score order, unchanged.
	TArray<FVector, TInlineAllocator<MaxFightBiasSlotTraces>> Candidates;
	Candidates.Reserve(FMath::Min(Result->Items.Num(), MaxFightBiasSlotTraces));
	for (int32 i = 0; i < Result->Items.Num() && Candidates.Num() < MaxFightBiasSlotTraces; ++i)
	{
		const FVector Slot = Result->GetItemAsLocation(i);

		// Sitting on the primary's slot — the pile-up this filter exists to prevent.
		if (bSpacingTest && FVector::DistSquared2D(Slot, PrimaryLocation) < MinSpacingSq) continue;

		// On the primary's side of the player. The dead zone passes near-centreline slots so a
		// corridor with no clearly-sided geometry degrades to the anchor instead of to nothing.
		if (bSideTest && FVector::DotProduct(Slot - PlayerLocation, SideRight) * AllySlotSideSign < -AllySlotSideDeadZone) continue;

		Candidates.Add(Slot);
	}

	// Nothing usable — forget the slot and let TickTask fall through to FormationDir, which already
	// IS the correct mirrored anchor. Unreachable for a primary: with both tests off, a non-empty
	// result set always fills at least one candidate.
	if (Candidates.Num() == 0)
	{
		bHasEqsTarget = false;
		// Transition-only, at Log: sustained rejection means AllyFollowSlotMinSpacing is tuned wider
		// than the query's donut and the secondary is permanently pinned to the anchor — worth one
		// line, not one every EqsQueryInterval for the rest of the level.
		if (!bAllySpacingRejectedAll)
		{
			bAllySpacingRejectedAll = true;
			UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] Ally filter rejected all %d slots — falling back to the mirrored formation anchor"),
				Result->Items.Num());
		}
		return;
	}
	bAllySpacingRejectedAll = false;

	EqsTarget = Candidates[0];
	bHasEqsTarget = true;

	// Fight-aware follow: prefer the best-scored surviving slot with eye-line toward the live-fight
	// bearing. A trace that reaches the threat — or stops on any pawn short of it (an enemy body IS
	// the fight) — counts as eye-line. Falls back to the top-scored slot when every candidate is
	// walled off.
	if (bFightBiasQuery && IsValid(World))
	{
		const UCapsuleComponent* Capsule = Companion->GetCapsuleComponent();
		const float SlotEyeOffset = (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f)
			+ Companion->BaseEyeHeight;

		for (const FVector& Slot : Candidates)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionFollowFightBias), true);
			Params.AddIgnoredActor(Companion);
			Params.AddIgnoredActor(Companion->GetCurrentWeapon());
			FHitResult Hit;
			const bool bBlocked = World->LineTraceSingleByChannel(Hit,
				Slot + FVector(0.f, 0.f, SlotEyeOffset), FightBiasThreatLocation,
				ECC_Visibility, Params);
			if (!bBlocked || Cast<APawn>(Hit.GetActor()))
			{
				EqsTarget = Slot;
				break;
			}
		}
	}
}

void UBTTask_FollowPlayer::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	if (ACompanionCharacter* Companion = CachedCompanion.Get())
	{
		Companion->SetSprinting(false);
		Companion->SetFollowCatchupPace(false);
	}

	bEqsQueryInProgress = false;
	bHasEqsTarget = false;
	EqsTarget = FVector::ZeroVector;
	bFightBiasQuery = false;
	bAllySpacingQuery = false;
	bAllySpacingRejectedAll = false;
	AllySlotSideSign = 0.f;
	LastMovingSideRight = FVector::ZeroVector;
	FightBiasThreatLocation = FVector::ZeroVector;
	CachedOwnerComp = nullptr;
	CachedController.Reset();
	CachedCompanion.Reset();

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTTask_FollowPlayer::GetStaticDescription() const
{
	return FString::Printf(TEXT("Follow Player%s"), bSprintToTarget ? TEXT(" [SPRINT]") : TEXT(""));
}
