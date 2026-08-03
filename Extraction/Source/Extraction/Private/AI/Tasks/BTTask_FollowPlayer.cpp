// BT task — companion follows its leader in formation or sprints to reach the player.
// Leader == player for the primary companion; leader == primary companion for the armed extractee.

#include "BTTask_FollowPlayer.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "CompanionCharacter.h"
#include "Character/ExtractionPlayerInterface.h" // bleedout remaining drives the rescue-approach pace
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
	FightBiasThreatLocation = FVector::ZeroVector;

	// Reset the leader chain — a re-entry must re-resolve rather than trust a cache from a run that
	// may have ended with the primary down or leashed away.
	CachedLeader.Reset();
	LastFollowLeader.Reset();
	TimeSinceLeaderScan = LeaderRescanInterval; // allow an immediate scan on first tick
	bLeaderLeashBroken = false;
	bLeaderMoving = false;

	// Reset the floor-transit detector — a re-entry mid-descent re-arms from the first sample.
	bHasLeaderZSample = false;
	LeaderZTransitEnvelope = 0.f;
	bWasPursuingLeader = false;

	// Clear any stale sprint flag from a prior abort (e.g. combat or revive re-entry).
	// Pace always clears: a rescue sprint must run at full SprintSpeed, never the catch-up tier.
	//
	// The sprint-to-target exemption that used to live here (keep the flag latched so the revive
	// branch entered at sprint speed) is gone: the approach's pace is now re-decided every tick from
	// bleedout / distance / threat urgency, so entering with a latch inherited from a previous run
	// would show one tick of the wrong pace before the first evaluation corrected it — and the whole
	// point of the change is that a rescue is not always urgent.
	bRescueSprinting = false;
	bFormationSprinting = false;
	Companion->SetFollowCatchupPace(false);
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

	// Leader resolution runs once per tick, before anything reads a formation frame. For a primary
	// companion it returns the player, so every leader-keyed line below is the pre-chain behaviour.
	AActor* Leader = ResolveFollowLeader(*Companion, *Player, *T, DeltaSeconds);
	// Publish so everything that must form up on the SAME actor reads one value: the follow-slot EQS
	// context and the cover-switch monitor's formation point. Deduped on the controller, so on all but
	// a handover tick this is a pointer compare.
	Controller->SetFollowLeader(Leader);
	const bool bLeaderAnchored = Leader != Player; // secondary trailing the primary companion
	const FVector LeaderLocation = Leader->GetActorLocation();
	const float DistToLeader = bLeaderAnchored ? FVector::Dist(CompanionLocation, LeaderLocation) : DistToPlayer;

	// Leader handover clears every latch keyed to the old frame. The floor-transit reset is mandatory,
	// not tidiness, and both halves of it matter: without the sample drop, swapping to a leader
	// standing on another floor reads as one enormous dZ/dt sample and trips seconds of pursuit; and
	// without the envelope drop, a leash break taken mid-stairwell carries the OLD leader's hot
	// envelope onto the new one and pursues it for the envelope's remaining drain time. Same reset
	// ExecuteTask performs on entry.
	if (LastFollowLeader.Get() != Leader)
	{
		LastFollowLeader = Leader;
		LastMoveTarget = FVector::ZeroVector;
		bIsIdling = false;
		bLeaderMoving = false;
		bHasLeaderZSample = false;
		LeaderZTransitEnvelope = 0.f;
	}

	// Mode change drops the idle latch and forces a move re-issue so the new formation applies now.
	if (Companion->GetMode() != LastSeenMode)
	{
		LastSeenMode = Companion->GetMode();
		bIsIdling = false;
		LastMoveTarget = FVector::ZeroVector;
	}

	// Floor-transit detector samples every tick — the idle/back-out branches below early-return,
	// and the envelope must already be live the moment formation logic resumes.
	const ACharacter* LeaderChar = Cast<ACharacter>(Leader);
	UpdateLeaderZTransit(LeaderChar, LeaderLocation.Z, DeltaSeconds);

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

		// Pace is re-decided every tick from the rescue's actual urgency instead of being pinned on.
		// SetFollowCatchupPace stays false throughout (ExecuteTask), so when this DOES sprint it runs
		// at the full SprintSpeed tier rather than the reduced formation catch-up one.
		Companion->SetSprinting(UpdateRescueSprint(*Companion, *T, DistToPlayer, *Player));

		// Approach diagnostic at ~1Hz — a rescue that stalls short of the arrival radius must be
		// visible in the log (dist vs threshold), not inferred.
		SprintLogAccumulator += DeltaSeconds;
		if (SprintLogAccumulator >= 1.f)
		{
			SprintLogAccumulator = 0.f;
			UE_LOG(LogCompanionAI, Log, TEXT("%s: RESCUE APPROACH dist=%.0f arrival=%.0f vel=%.0f sprint=%d threatToBody=%.0f"),
				*Companion->GetName(), DistToPlayer, ArrivalRadius, Companion->GetVelocity().Size2D(),
				(int32)bRescueSprinting, Companion->GetNearestThreatToDownedPlayer());
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

	// --- Wave hold: stand fast instead of pathing back to formation ---
	// A finite Director wave stays active across the gaps BETWEEN squad spawns. Without this the
	// last kill of a squad clears the combat target, the BB observer aborts the combat branch and
	// this task immediately walks the ally home mid-defence. The service owns the flag (including
	// the leash that breaks the hold when the player pushes on), so this is purely "don't move".
	// The rescue-approach branch above has already returned, so a downed player still outranks it.
	if (Companion->IsWaveHoldActive())
	{
		if (!bHoldingForWave)
		{
			bHoldingForWave = true;
			Controller->StopMovement();
			Companion->SetSprinting(false);
			// Clear catch-up pace and its sprint latch -- the wave hold is not a formation gap,
			// and a latched pace would suppress overwatch entry + every non-combat facing tier
			// for the entire hold.
			Companion->SetFollowCatchupPace(false);
			bFormationSprinting = false;
			// Clear both move latches so the first post-hold formation move re-issues instead of
			// deduping against a destination stamped before the hold began.
			LastMoveTarget = FVector::ZeroVector;
			bIsIdling = false;
		}
		return;
	}
	bHoldingForWave = false;

	// --- Commanded cover hold: hold position at the cover the player ordered, same treatment as
	// the wave hold above. The service's leash/DBNO/new-command release clears the flag. ---
	if (Companion->IsCommandedCoverHoldActive())
	{
		if (!bHoldingForCoverCommand)
		{
			bHoldingForCoverCommand = true;
			Controller->StopMovement();
			Companion->SetSprinting(false);
			Companion->SetFollowCatchupPace(false);
			bFormationSprinting = false;
			LastMoveTarget = FVector::ZeroVector;
			bIsIdling = false;
		}
		return;
	}
	bHoldingForCoverCommand = false;

	// --- Formation (non-sprint) mode: stay near the player indefinitely ---

	// Clamp the three follow distances to a sensible ordering at read time so mistuning can't
	// produce unreachable states: EffMinSep < AcceptableRadius, EffStandoff > EffMinSep.
	constexpr float FollowDistMargin = 30.f;
	constexpr float BackoutDeadband  = 20.f;
	const float EffMinSep   = FMath::Min(T->FollowMinSeparation,  T->AcceptableRadius - FollowDistMargin);
	const float EffStandoff = FMath::Max(T->FollowIdleStandoff,   EffMinSep + FollowDistMargin);

	// Level-alignment gate: the follow distance is straight-line and lies through floors — a leader
	// one floor down in a stairwell reads as ~400cm "close". Off-level the companion must never
	// settle. Read-time floor (mirrors EffMinSep/EffStandoff): a max-Z inside the idle radius would
	// make idle unreachable on any slope and sprint the companion in place at the leader's side.
	const float ZDelta = FMath::Abs(CompanionLocation.Z - LeaderLocation.Z);
	const float EffMaxZDelta = T->FollowMaxZDelta <= 0.f
		? 0.f : FMath::Max(T->FollowMaxZDelta, T->AcceptableRadius + FollowDistMargin);
	const bool bZAligned = EffMaxZDelta <= 0.f || ZDelta <= EffMaxZDelta;

	// Formation point is computed up front — Combat mode keys its idle/hysteresis gates on the
	// distance to the LEAD point, not to the leader (a leader squeezing past a leading companion
	// would otherwise latch it idle at their side until the gap exceeded 1.5x AcceptableRadius).
	// Mode shapes the formation: Combat leads AHEAD of the leader (capped at the lead distance by
	// construction), Stealth tucks in tight behind, Normal keeps the standard offsets.
	const ECompanionMode Mode = Companion->GetMode();
	const bool bCombatLead = Mode == ECompanionMode::Combat;

	// Front-to-back stagger for a non-primary companion (the armed extractee) — the lateral mirror
	// alone leaves the pair a symmetric wall across the player's rear. Primary = 0: every use is a
	// no-op. Zeroed on the leader-anchored path: the chain already staggers the pair by a full
	// FormationOffsetBack, and the bias on top only drags the VIP further off the player's track.
	// It still applies on the player-anchored fallback, where the mirror IS the only separation.
	const float BackBias = bLeaderAnchored ? 0.f : Companion->GetFormationBackBias(T->SecondaryFormationBackBias);

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

	// Applied after the mode branch so all three formations mirror with one edit. On the leader-
	// anchored path the mirror is what centres the VIP back on the player's own track (it anchors
	// behind-left of a companion that is itself behind-right of the player), so it stays applied
	// there too. Primary = sign +1 / bias 0: no-op.
	OffsetRight *= Companion->GetFormationSideSign();
	if (!bCombatLead)
		OffsetBack += BackBias; // combat folded the bias into LeadDistance — adding it here double-counts

	// Travel gate. The player-anchored path keeps its original raw speed comparison bit-for-bit; a
	// COMPANION leader gets a latched 2D gate instead, because ally soft-separation shoves it back
	// and forth across a single threshold and the two anchor formulas below would swap every few
	// ticks (travel-offset behind a walking leader vs bearing-hold around a standing one).
	bool bLeaderTravelling;
	if (bLeaderAnchored)
	{
		const float LeaderSpeed2D = LeaderChar ? LeaderChar->GetVelocity().Size2D() : 0.f;
		bLeaderMoving = bLeaderMoving ? LeaderSpeed2D > LeaderMovingSpeedExit : LeaderSpeed2D > LeaderMovingSpeedEnter;
		bLeaderTravelling = bLeaderMoving;
	}
	else
	{
		bLeaderTravelling = LeaderChar && LeaderChar->GetVelocity().SizeSquared() > 100.0f * 100.0f;
	}

	FVector FormationDir;
	if (bLeaderTravelling)
	{
		// Leader is moving — formation relative to their movement direction
		const FVector MoveDir = LeaderChar->GetVelocity().GetSafeNormal2D();
		const FVector MoveRight = FVector::CrossProduct(FVector::UpVector, MoveDir);
		FormationDir = LeaderLocation - MoveDir * OffsetBack + MoveRight * OffsetRight;
	}
	else if (bCombatLead)
	{
		// Leader stationary in Combat mode — take point along their facing at -OffsetBack, which the
		// combat branch set to exactly LeadDistance (bias and floor both already inside it), so the
		// moving and stationary lead points can't drift apart.
		const FVector Facing = Leader->GetActorForwardVector().GetSafeNormal2D();
		const FVector FacingRight = FVector::CrossProduct(FVector::UpVector, Facing);
		FormationDir = LeaderLocation + Facing * -OffsetBack + FacingRight * OffsetRight;
	}
	else
	{
		// Leader stationary — just maintain distance, stay where we are relative to the leader
		const FVector ToCompanion = (CompanionLocation - LeaderLocation).GetSafeNormal2D();
		FormationDir = LeaderLocation + ToCompanion * OffsetBack;
	}

	// Standoff clamp against the PLAYER, leader-anchored only. The leader is a companion that can
	// stand anywhere relative to the player — a Combat-mode primary takes point AHEAD of them, which
	// drops the trailing anchor straight into the player's lap, and the stealth tuck does the same at
	// short range. Mode-agnostic by design: it is the player-relative geometry that is unacceptable,
	// not the mode that produced it. Pushing out to exactly EffStandoff (the same ring the
	// min-separation back-out targets) means the clamp and the back-out can never fight each other.
	if (bLeaderAnchored && FVector::Dist2D(FormationDir, PlayerLocation) < EffStandoff)
	{
		FVector FromPlayer = (FormationDir - PlayerLocation).GetSafeNormal2D();
		if (FromPlayer.IsNearlyZero())
			FromPlayer = (CompanionLocation - PlayerLocation).GetSafeNormal2D();
		if (FromPlayer.IsNearlyZero())
			FromPlayer = (-Player->GetActorForwardVector()).GetSafeNormal2D();
		if (!FromPlayer.IsNearlyZero())
		{
			const float AnchorZ = FormationDir.Z; // the anchor's floor, never the player's
			FormationDir = PlayerLocation + FromPlayer * EffStandoff;
			FormationDir.Z = AnchorZ;
		}
	}

	// Combat lead idles against the lead point; everything else against whatever it forms up on.
	const float IdleGateDist = bCombatLead
		? FVector::Dist(CompanionLocation, FormationDir)
		: (bLeaderAnchored ? DistToLeader : DistToPlayer);

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
		Companion->SetFollowCatchupPace(false);
		bFormationSprinting = false;

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
			Companion->SetFollowCatchupPace(false);
			bFormationSprinting = false;
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
	// Combat mode skips the EQS slot entirely — the query anchors slots around/behind the leader,
	// which would fight the lead point; MoveToLocation's nav projection handles off-mesh leads.
	//
	// BOTH companions dispatch it. The query anchors on UEnvQueryContext_FollowLeader, i.e. the actor
	// resolved above, which makes the primary's behaviour unchanged BY CONSTRUCTION: a primary's
	// resolved leader IS the player (first line of ResolveFollowLeader), so the context hands the query
	// the same actor the old player-anchored context did. Restoring it for the secondary is the point —
	// a primary-only gate took the fight-bias branch below with it and left the VIP strolling through
	// firefights on a raw geometric offset.
	//
	// bPlayerAnchoredSecondary is NOT a primary check in disguise — do not "simplify" it back to one.
	// The hazard is two companions querying the SAME player-centred donut: the winning slot scores
	// identically for both and overrides the mirrored FormationDir that was providing their separation,
	// so they converge on one point (the original author's "it would just steer it back onto the
	// primary's own slot").
	//
	// That needs a second FOLLOW-TICKING querier, which is a narrower thing than "a secondary anchored
	// on the player", and the two hazard states are named directly rather than inferred:
	//   - master switch off: both companions run player-anchored follow side by side, exactly as
	//     bSecondaryFollowsPrimaryCompanion's own comment describes.
	//   - leash broken: the primary is alive and still following, just too far out to trail.
	// Both are readable here for free — T is already dereferenced throughout TickTask and the latch is
	// a member — so no scan and no new state. They separate cleanly only because the latch is now
	// cleared on every other hand-back path in ResolveFollowLeader: it is false on the master-switch
	// and unusable-leader paths and true only on the leash path.
	//
	// A DOWN or DEAD primary is therefore explicitly NOT excluded, and that is the point rather than an
	// oversight. It anchors on the player too, but a DBNO primary is not running its follow task, so
	// there is no second querier to collide with — and primary-down-with-enemies-alive is precisely the
	// state 66f10bc3 added fight-aware follow for. FormationDir would still separate correctly there,
	// but separation was never what the query bought; slot-level fight awareness was.
	//
	// The real fix is an ally-spacing test in the follow-slot query itself, which would also separate
	// the leader-anchored case and would let this guard be deleted. That is designer-side EQS work for
	// a later in-engine pass.
	//
	// bLeaderAnchored is (Leader != Player), so the first half reads "a secondary that resolved onto
	// the player" and the second narrows it to the two states where a rival querier actually exists.
	const bool bPlayerAnchoredSecondary = !Companion->IsPrimaryCompanion() && !bLeaderAnchored
		&& (!T->bSecondaryFollowsPrimaryCompanion || bLeaderLeashBroken);

	TimeSinceLastEqs += DeltaSeconds;
	if (FollowSlotQuery && !bCombatLead && !bPlayerAnchoredSecondary && !bEqsQueryInProgress && TimeSinceLastEqs >= EqsQueryInterval)
	{
		bEqsQueryInProgress = true;
		TimeSinceLastEqs = 0.f;

		// Fight-aware follow: with a fresh live-fight signal, request every passing slot and let
		// the callback prefer one with eye-line toward the threat. SingleResult otherwise.
		bFightBiasQuery = Controller->GetRecentAlertedThreat(T->FightSignalMaxAge, FightBiasThreatLocation);

		FEnvQueryRequest QueryRequest(FollowSlotQuery, Companion);
		QueryRequest.Execute(bFightBiasQuery ? EEnvQueryRunMode::AllMatching : EEnvQueryRunMode::SingleResult,
			FQueryFinishedSignature::CreateUObject(this, &UBTTask_FollowPlayer::OnFollowQueryFinished));
	}

	// Wrong-floor EQS slots are discarded AND forgotten — not just down-scored: the query's donut
	// projects onto whatever navmesh is in vertical range, and its Distance3D scoring can rank a
	// slot directly above/below the anchor as near-perfect. Forgetting matters mid-stairwell: a
	// stale upper-floor slot must not be re-accepted when the anchor's Z drifts back into its band.
	// Keyed on the LEADER, which is what the query anchored on — for a primary that is the player, so
	// the gate is unchanged there, but judging a secondary's primary-anchored slots against the
	// player's floor would reject every one of them whenever the two stand a storey apart.
	if (bHasEqsTarget && EffMaxZDelta > 0.f && FMath::Abs(EqsTarget.Z - LeaderLocation.Z) > EffMaxZDelta)
		bHasEqsTarget = false;

	// Floor transit = pursue the leader's location directly. Mid-flight the leader's Z sits within
	// FollowMaxZDelta of the floor ABOVE, so upper-landing slots legitimately pass the wrong-floor
	// gate and yo-yo the companion on the stairs; the formation anchor (leader Z, offset into the
	// stair slab) is off-mesh and only churns Failed->fallback. Off-level pursues unconditionally
	// for the same reason.
	const bool bLeaderZTransit = T->FollowPursuitZRate > 0.f
		&& LeaderZTransitEnvelope > T->FollowPursuitZRate;
	const bool bPursueLeader = bLeaderZTransit || !bZAligned;
	if (bPursueLeader != bWasPursuingLeader)
	{
		bWasPursuingLeader = bPursueLeader;
		UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] PURSUIT %s (ZRate=%.0f zAligned=%d leaderAnchored=%d)"),
			bPursueLeader ? TEXT("ENTER") : TEXT("EXIT"), LeaderZTransitEnvelope, (int32)bZAligned, (int32)bLeaderAnchored);
	}

	const FVector MoveTarget = bPursueLeader ? LeaderLocation
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

	// Sprint catch-up + idle hysteresis keyed on raw distances (not the slot).
	// SetSprinting MUST stay before any early-return — preserves the c62bdbf sprint-latch fix.
	// Mirror the leader's sprint too. Against the PLAYER that has to be inferred from 2D speed
	// against the tuning threshold (kit BP owns player sprint state; kit walk 410, threshold sits
	// between walk and sprint); a COMPANION leader publishes the flag itself, so read it directly
	// rather than re-deriving it from a velocity that soft-separation and pathing both perturb.
	// The mirror needs a distance floor — without it the companion sprints 2m to a formation
	// point because its leader happened to be sprinting somewhere.
	// Non-null exactly while leader-anchored: ResolveFollowLeader returns the cached primary or the
	// player and nothing else, so this is the same actor Leader points at.
	const ACompanionCharacter* LeaderCompanion = bLeaderAnchored ? CachedLeader.Get() : nullptr;
	const bool bLeaderSprinting = LeaderCompanion
		? LeaderCompanion->IsSprinting()
		: Player->GetVelocity().Size2D() > T->PlayerSprintSpeedThreshold;
	// Off-level counts as far regardless of 3D distance — the real path runs the stairwell.
	// Stealth is exempt: its catch-up ladder owns pace there (sprint break deliberately unreachable).
	// Both distances arm it: the leader gap is what this companion is actually closing, and the
	// player gap bounds total convoy stretch (identical values unless leader-anchored).
	//
	// Hysteretic (latched): enter at SprintDistanceThreshold, release at 0.6x. Without the band
	// the flag flip-flops per-frame at the threshold when any non-combat gameplay focal is live:
	// sprint clears the focal and raises speed -> closes fast -> drops below threshold -> un-sprints
	// -> focal re-asserts, strafe clamp re-engages at 275 -> falls behind again -> re-crosses.
	// The sprint mirror arm uses the same release fraction on its own half-threshold floor.
	const float SprintEnterDist = T->SprintDistanceThreshold;
	const float SprintReleaseDist = SprintEnterDist * FormationSprintReleaseFraction;
	const float SprintMirrorEnterDist = SprintEnterDist * 0.5f;
	const float SprintMirrorReleaseDist = SprintMirrorEnterDist * FormationSprintReleaseFraction;

	const bool bDistEnter = DistToLeader > SprintEnterDist || DistToPlayer > SprintEnterDist;
	const bool bDistRelease = DistToLeader <= SprintReleaseDist && DistToPlayer <= SprintReleaseDist;
	const bool bMirrorEnter = bLeaderSprinting && DistToLeader > SprintMirrorEnterDist;
	const bool bMirrorRelease = !bLeaderSprinting || DistToLeader <= SprintMirrorReleaseDist;
	const bool bOffLevel = !bZAligned && !Companion->IsStealthActive();

	if (!bFormationSprinting)
		bFormationSprinting = bDistEnter || bMirrorEnter || bOffLevel;
	else
		bFormationSprinting = !(bDistRelease && bMirrorRelease && bZAligned);

	const bool bWantSprint = bFormationSprinting;
	UE_LOG(LogCompanion, VeryVerbose, TEXT("[FollowPlayer] FORMATION branch: Dist=%.0f LeaderDist=%.0f Thresh=%.0f Release=%.0f EqsSlot=%d Pursue=%d -> SetSprinting(%d)"),
		DistToPlayer, DistToLeader, SprintEnterDist, SprintReleaseDist, bHasEqsTarget ? 1 : 0, bPursueLeader ? 1 : 0, bWantSprint ? 1 : 0);

	// Catch-up rides the reduced sprint tier (FollowCatchupSprintSpeed 650) — full SprintSpeed read
	// as too fast for closing formation gaps. A leader-anchored secondary drops that tier once either
	// gap is genuinely open: its leader sprints at the full SprintSpeed (850), so a 650 chase never
	// closes and the convoy stretches until the warp net fires. The tier still applies to the small
	// gaps it was tuned for. Pace set before the sprint flag so the speed applies on the same
	// ApplyMovementSpeeds pass.
	const bool bNeedsFullSprint = bLeaderAnchored
		&& (DistToLeader > T->SprintDistanceThreshold || DistToPlayer > T->SprintDistanceThreshold);
	Companion->SetFollowCatchupPace(bWantSprint && !bNeedsFullSprint);
	Companion->SetSprinting(bWantSprint);

	// Only re-issue move if target shifted significantly
	if (FVector::Dist(MoveTarget, LastMoveTarget) < 200.0f)
		return;

	LastMoveTarget = MoveTarget;

	// Pursuit targets the leader's own location — a player or a nav agent, so always nav-adjacent and
	// projection is safe ON. The anchor path keeps it OFF: a projected in-wall anchor lands on the
	// wrong side of the wall. Pursuit accepts at the standoff ring, not AcceptableRadius: reach the
	// leader's AREA — the live radius (30cm) is unreachable through two capsules and would shove the
	// companion into the leader's back until the transit envelope decays.
	const EPathFollowingRequestResult::Type MoveRes = bPursueLeader
		? Controller->MoveToLocation(MoveTarget, EffStandoff, false, true, true, true)
		: Controller->MoveToLocation(MoveTarget, T->AcceptableRadius * 0.5f, false, true, false, true);

	// Off-mesh formation anchor fallback: with the companion on another floor, the stationary-leader
	// anchor (leader + 2D bearing toward the companion) can land inside the stairwell wall/void —
	// and with destination projection off, that move FAILS outright. The Idle-debounce above then
	// re-issued the same doomed move forever: companion parked one floor up while the leader stood
	// still. Path to the leader instead (always nav-adjacent; projection on; the acceptance ring
	// keeps follow distance) so the stairwell route actually runs.
	// LastMoveTarget stays keyed on the ANCHOR (set above): the dedup then suppresses re-issuing
	// the doomed anchor while the fallback move runs, and the Idle-debounce re-opens the gate if
	// the fallback itself ends short.
	// A failed PURSUIT move has no better target to fall back to — the Idle-debounce above retries
	// it once the path-following component settles.
	if (MoveRes == EPathFollowingRequestResult::Failed && !bPursueLeader)
	{
		Controller->MoveToLocation(LeaderLocation, T->AcceptableRadius, false, true, true, true);
		UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] FORMATION anchor off-mesh (zAligned=%d) — falling back to leader-location move"),
			(int32)bZAligned);
	}
}

bool UBTTask_FollowPlayer::UpdateRescueSprint(ACompanionCharacter& Companion,
	const UCompanionTuningDataAsset& Tuning, float DistToPlayer, AActor& Player)
{
	// Gates widen while already sprinting so the decision cannot chatter: the bleedout gate rises,
	// the distance gate falls, the threat gate grows. Same split enter/exit idiom as the stealth
	// catch-up ladder in TickTask — a single threshold parks a companion running the boundary right
	// on it, and every flip is visible (speed tier AND the anim instance's sprint state).
	const float BleedoutGate = bRescueSprinting
		? Tuning.ReviveSprintBleedoutThreshold + RescueSprintBleedoutReleaseMargin
		: Tuning.ReviveSprintBleedoutThreshold;
	const float DistanceGate = bRescueSprinting
		? Tuning.ReviveSprintDistanceThreshold * RescueSprintDistanceReleaseScale
		: Tuning.ReviveSprintDistanceThreshold;
	const float ThreatGate = bRescueSprinting
		? Tuning.ReviveSprintThreatRadius * RescueSprintThreatReleaseScale
		: Tuning.ReviveSprintThreatRadius;

	// Bleedout of exactly 0 means "no timer running" (see ACompanionCharacter/AExtractionPlayer's
	// GetBleedoutTimeRemaining), not "out of time" — a plain <= compare would sprint every approach.
	const IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(&Player);
	const float BleedoutRemaining = PlayerIface ? PlayerIface->GetBleedoutTimeRemaining() : 0.f;
	const bool bClockUrgent = BleedoutRemaining > 0.f && BleedoutRemaining <= BleedoutGate;

	const bool bLongRun = DistToPlayer > DistanceGate;

	// Negative = the state service found nothing (or is not sweeping) — never "distance 0".
	const float ThreatToBody = Companion.GetNearestThreatToDownedPlayer();
	const bool bThreatOnBody = ThreatToBody >= 0.f && ThreatToBody <= ThreatGate;

	bRescueSprinting = bClockUrgent || bLongRun || bThreatOnBody;
	return bRescueSprinting;
}

AActor* UBTTask_FollowPlayer::ResolveFollowLeader(ACompanionCharacter& Companion, AActor& Player,
	const UCompanionTuningDataAsset& Tuning, float DeltaSeconds)
{
	// Zero-change gate for the primary: its leader IS the player, so every leader-keyed line in
	// TickTask resolves exactly as it did before the chain existed. Same for the master switch off.
	if (Companion.IsPrimaryCompanion()) return &Player;
	if (!Tuning.bSecondaryFollowsPrimaryCompanion)
	{
		// Same clear every other hand-back-to-player path performs (unusable leader, leash disabled).
		// Without it, flipping this switch off in the editor while a VIP happened to be latched broken
		// left the latch stale-true for the rest of the run, since no later tick can reach the leash
		// block to re-evaluate it.
		bLeaderLeashBroken = false;
		return &Player;
	}

	// Rescan only on a cache MISS, and then at most every LeaderRescanInterval: GetPrimaryCompanion
	// is a TActorIterator over the whole level and must never run per tick. Pinned at the interval
	// while the cache is hot so the first miss scans on that same tick (TimeSinceLastEqs idiom).
	if (CachedLeader.IsValid())
	{
		TimeSinceLeaderScan = LeaderRescanInterval;
	}
	else
	{
		TimeSinceLeaderScan += DeltaSeconds;
		if (TimeSinceLeaderScan >= LeaderRescanInterval)
		{
			TimeSinceLeaderScan = 0.f;
			CachedLeader = ACompanionCharacter::GetPrimaryCompanion(Companion.GetWorld());
		}
	}

	// Capability is re-tested EVERY tick against the cached pointer, never only at scan time: a
	// leader that goes DBNO, dies or loses its controller mid-follow has to hand back to the player
	// on the next tick, not at the end of the rescan interval. The test itself lives on the controller
	// so its read-time staleness check on the published leader can't drift from this one.
	ACompanionCharacter* Leader = CachedLeader.Get();
	if (!ACompanionAIController::IsUsableFollowLeader(Leader, &Companion))
	{
		// Drop the leash WITH the leader — not redundant, and not safe to delete. The latch means "the
		// leader I am trailing has been commanded too far from the player to anchor on", so carrying it
		// across a leader that is no longer usable at all makes it lie about itself. TickTask's
		// follow-slot query gate reads it, and a stale-set latch would hold the fight-aware query off
		// for the whole time the primary is down — precisely the firefight the query exists for.
		// Re-acquiring later then re-enters on the full leash distance instead of the release band,
		// which is correct: a leader we stopped trailing entirely and later pick up again is a fresh
		// acquisition, and the band is hysteresis for a leader we never lost.
		bLeaderLeashBroken = false;
		return &Player;
	}

	// Leash: a leader commanded away (ping / route / breach) stops being a sane anchor long before
	// the warp net would fire, and trailing it would drag the VIP off the player entirely. Latched
	// with a release band — a leader loitering on the boundary would otherwise swap the whole follow
	// frame (anchor, pursuit target, sprint mirror) every tick. 0 disables the leash.
	if (Tuning.SecondaryLeaderLeashDistance <= 0.f)
	{
		bLeaderLeashBroken = false;
		return Leader;
	}

	// Read-time floor on the release band (mirrors EffMinSep/EffStandoff/EffSprintBreak in TickTask):
	// at any leash inside LeaderLeashReleaseHysteresis the flat band collapses the release distance to
	// zero, and a leash that has already latched broken could then NEVER re-acquire — a silent,
	// permanent fallback to player-follow. Half the leash keeps the band non-zero and strictly inside
	// the break distance at every tuning, and only ever narrows the band: the authored break distance
	// itself is never overridden.
	const float ReleaseDistance = FMath::Max(Tuning.SecondaryLeaderLeashDistance - LeaderLeashReleaseHysteresis,
		Tuning.SecondaryLeaderLeashDistance * 0.5f);
	const float LeaderToPlayer = FVector::Dist2D(Leader->GetActorLocation(), Player.GetActorLocation());
	bLeaderLeashBroken = bLeaderLeashBroken
		? LeaderToPlayer > ReleaseDistance
		: LeaderToPlayer > Tuning.SecondaryLeaderLeashDistance;

	return bLeaderLeashBroken ? &Player : Leader;
}

void UBTTask_FollowPlayer::UpdateLeaderZTransit(const ACharacter* LeaderChar, float LeaderZ, float DeltaSeconds)
{
	// Envelope follower over the leader's grounded vertical rate. Position-delta based: CMC keeps
	// reported velocity horizontal in walking mode, so GetVelocity().Z reads ~0 on stairs. Airborne
	// frames don't sample — a jump apex must not read as floor transit.
	const UCharacterMovementComponent* Move = LeaderChar ? LeaderChar->GetCharacterMovement() : nullptr;
	const bool bOnGround = !Move || Move->IsMovingOnGround();

	float Rate = 0.f;
	if (bOnGround)
	{
		if (bHasLeaderZSample)
			Rate = FMath::Min(FMath::Abs(LeaderZ - LastLeaderZ) / FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER), ZTransitRateClamp);
		LastLeaderZ = LeaderZ;
		bHasLeaderZSample = true;
	}

	// Slew both directions: the attack slope rejects single-tick blips (only a sustained rate can
	// climb to the threshold), the decay sets how long pursuit holds after the leader settles.
	LeaderZTransitEnvelope = FMath::Clamp(Rate,
		FMath::Max(LeaderZTransitEnvelope - ZTransitEnvelopeDecay * DeltaSeconds, 0.f),
		LeaderZTransitEnvelope + ZTransitEnvelopeAttack * DeltaSeconds);
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

	// Best-first window over the result set. Each companion's query is anchored on its OWN leader, so
	// every returned slot already belongs to this companion's formation ring and there is nothing to
	// filter here — the window is the fight-bias TRACE budget and nothing else, and a non-empty result
	// set always fills at least one candidate.
	TArray<FVector, TInlineAllocator<MaxFightBiasSlotTraces>> Candidates;
	Candidates.Reserve(FMath::Min(Result->Items.Num(), MaxFightBiasSlotTraces));
	for (int32 i = 0; i < Result->Items.Num() && Candidates.Num() < MaxFightBiasSlotTraces; ++i)
		Candidates.Add(Result->GetItemAsLocation(i));

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

	// Pace latches — same reason as the EQS resets below: the hysteresis bands are only honest
	// within one approach, and a latched sprint carried into the next one would skip its first
	// evaluation's jog answer entirely.
	bRescueSprinting = false;
	bFormationSprinting = false;

	bEqsQueryInProgress = false;
	bHasEqsTarget = false;
	EqsTarget = FVector::ZeroVector;
	bFightBiasQuery = false;
	FightBiasThreatLocation = FVector::ZeroVector;

	// Leader chain — same reason as the EQS resets: nothing from this run may bleed into the next.
	CachedLeader.Reset();
	LastFollowLeader.Reset();
	TimeSinceLeaderScan = LeaderRescanInterval;
	bLeaderLeashBroken = false;
	bLeaderMoving = false;

	CachedOwnerComp = nullptr;
	CachedController.Reset();
	CachedCompanion.Reset();

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTTask_FollowPlayer::GetStaticDescription() const
{
	return FString::Printf(TEXT("Follow Player%s"), bSprintToTarget ? TEXT(" [SPRINT]") : TEXT(""));
}
