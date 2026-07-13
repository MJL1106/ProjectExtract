// BT service — periodically re-evaluates whether the companion's current cover point
// is still the best available, and commits a switch by writing the CoverTarget BB key.
// P3 AICS migration: cover source changed from AAICoverSlot line-segment slots to FCoverHandle/FCoverData points.

#include "BTService_CoverSwitchMonitor.h"
#include "AI/AITargetingStatics.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "AI/CompanionCoverStatics.h"
#include "CoverSystem.h"
#include "CoverGeometryStatics.h"
#include "CoverScoringStatics.h"
#include "CoverReservationSubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h"
#include "CompanionTuningDataAsset.h"
#include "CompanionCharacter.h"
#include "Character/ExtractionPlayerInterface.h"
#include "EnemyCharacter.h"
#include "EnemyArchetypeData.h"
#include "WeaponBase.h"
#include "HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"

namespace
{
	constexpr float DefaultCapsuleRadius = 34.f;
	// Multiplier on ArrivalRadius for the per-tick decision gate — looser than the arrival latch so
	// peek-step offsets (lean-fire positions reach ~125cm from the hunker) don't suppress decisions.
	constexpr float AtCoverDecisionSlack = 2.f;

	/** Per-re-eval decay on the player-advance accumulator (re-evals ~1 s apart) — a player who
	 *  stops pushing bleeds back below the gate within a few evals. */
	constexpr float PlayerAdvanceDecayPerReEval = 0.75f;

	/** Consecutive positive compromise evals before a geometric flank confirms (~0.8 s at the
	 *  default 0.4 s cadence) — same anti-flicker bar as the enemy flank-break. */
	constexpr int32 CompromiseDebounceRequired = 2;

	/** Shared with the combat task's companion.CoverDebug cvar (defined in BTTask_CompanionCombat.cpp). */
	bool MonitorCovDbg()
	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("companion.CoverDebug"));
		return CVar && CVar->GetInt() > 0;
	}
}
// Threat gathering lives in CompanionCover:: (AI/CompanionCoverStatics.h) — shared with the
// first-pick multi-threat re-rank in BTTask_MoveToCoverPoint so both test the same threat set.

UBTService_CoverSwitchMonitor::UBTService_CoverSwitchMonitor()
{
	NodeName         = TEXT("Cover Switch Monitor");
	Interval         = 0.1f;
	RandomDeviation  = 0.02f;
	bCreateNodeInstance = false; // state lives in NodeMemory
	INIT_SERVICE_NODE_NOTIFY_FLAGS();

	CoverTargetKey.SelectedKeyName = TEXT("CoverTarget");

	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		CoverTargetKey.AllowedTypes.Add(NewObject<UBlackboardKeyType_Cover>(this, TEXT("CoverTargetKey_Cover")));
	}
}

void UBTService_CoverSwitchMonitor::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	Super::InitializeMemory(OwnerComp, NodeMemory, InitType);
	FCoverSwitchMonitorMemory* Mem = CastInstanceNodeMemory<FCoverSwitchMonitorMemory>(NodeMemory);
	new (Mem) FCoverSwitchMonitorMemory();
}

void UBTService_CoverSwitchMonitor::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	FCoverSwitchMonitorMemory* Mem = CastInstanceNodeMemory<FCoverSwitchMonitorMemory>(NodeMemory);
	Mem->~FCoverSwitchMonitorMemory();
	Super::CleanupMemory(OwnerComp, NodeMemory, CleanupType);
}

void UBTService_CoverSwitchMonitor::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	if (UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		HasCoverPositionKey.ResolveSelectedKey(*BBAsset);
		CoverTargetKey.ResolveSelectedKey(*BBAsset);
		CoverLocationKey.ResolveSelectedKey(*BBAsset);
		CombatTargetKey.ResolveSelectedKey(*BBAsset);
		PlayerActorKey.ResolveSelectedKey(*BBAsset);

		ensureMsgf(CoverTargetKey.SelectedKeyType != nullptr,
			TEXT("BTService_CoverSwitchMonitor: CoverTargetKey '%s' failed to resolve against BB asset '%s' — monitor will never switch cover"),
			*CoverTargetKey.SelectedKeyName.ToString(), *GetNameSafe(BBAsset));
	}
}

void UBTService_CoverSwitchMonitor::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	FCoverSwitchMonitorMemory& Mem = *reinterpret_cast<FCoverSwitchMonitorMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;

	ACompanionAIController* Controller = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (!Controller) return;

	APawn* Pawn = Controller->GetPawn();
	if (!Pawn) return;

	const bool bHasCover = BB->GetValueAsBool(HasCoverPositionKey.SelectedKeyName);

	if (!bHasCover)
	{
		Mem = {};
		return;
	}

	const FCover CurrentCover = BB->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
	if (!CurrentCover.IsValid())
	{
		BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
		return;
	}

	const UCompanionTuningDataAsset* Tuning = Controller->GetTuning();
	if (!Tuning) return;

	UWorld* World = Pawn->GetWorld();
	if (!World) return;

	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	if (!CoverSys) return;

	// Fresh arrival: previous tick had no cover, now we do. Dwell does not start until physical arrival.
	if (!Mem.bWasInCoverLastTick)
	{
		Mem.TimeSinceArrival    = 0.f;
		Mem.TimeSinceReEval     = 0.f;
		Mem.bWasInCoverLastTick = true;
		Mem.bHasArrived         = false;
		return;
	}

	// Identity guard on the arrival latch: a task-internal shuffle or a monitor commit swaps the BB
	// cover without a HasCover false/true edge — the latch (and dwell) must re-earn at the new point,
	// or the monitor keeps "re-scoring from cover" while the pawn is still walking to it.
	if (Mem.bHasArrived && Mem.LastArrivalCover != CurrentCover.Handle)
	{
		Mem.bHasArrived = false;
		Mem.TimeSinceArrival = 0.f;
	}

	// Physical at-cover distance: starts dwell (arrival latch) and gates every switch/compromise
	// decision below. BTTask_MoveToCoverPoint moves the pawn to the edge-aligned hunker, so test
	// against the equivalent hunker point — not the raw cover Location, which would deadlock dwell
	// for standoff-offset points.
	const ACharacter* PawnChar = Cast<ACharacter>(Pawn);
	const UCapsuleComponent* Cap = PawnChar ? PawnChar->GetCapsuleComponent() : nullptr;
	const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : DefaultCapsuleRadius;
	const float Standoff = CapRadius + 10.f;
	const AEnemyCharacter* MonEnemy = Cast<AEnemyCharacter>(Pawn);
	const UEnemyArchetypeData* MonDA = MonEnemy ? MonEnemy->GetArchetypeData() : nullptr;
	const ACompanionCharacter* MonCompanion = Cast<ACompanionCharacter>(Pawn);
	FVector HunkerLoc;
	if (MonDA)
		HunkerLoc = UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
			Pawn->GetWorld(), CurrentCover.Data, Standoff, CapRadius, MonDA->CoverCornerGap, Pawn);
	else if (MonCompanion)
		HunkerLoc = CompanionCover::CompanionHunkerPosition(*MonCompanion, CurrentCover.Data, Standoff);
	else
		HunkerLoc = UCoverGeometryStatics::GetHunkerPosition(CurrentCover.Data, Standoff);
	const float DistToHunker2D = FVector::Dist2D(Pawn->GetActorLocation(), HunkerLoc);

	if (!Mem.bHasArrived)
	{
		if (DistToHunker2D > ArrivalRadius)
		{
			Mem.TimeSinceArrival = 0.f;
			return;
		}
		Mem.bHasArrived = true;
		Mem.LastArrivalCover = CurrentCover.Handle;
	}

	Mem.TimeSinceArrival += DeltaSeconds;
	Mem.TimeSinceReEval  += DeltaSeconds;

	// Decision gate: while the pawn isn't physically at the current point (final-approach walk,
	// snap-glide, wandered peek), hold ALL switch/compromise decisions — judging or vacating a point
	// the pawn isn't at is what produced mid-trek CoverSwitch/CompromiseBreak churn. Slack over
	// ArrivalRadius tolerates peek-step offsets. Dwell keeps accruing; the latch stays intact.
	if (DistToHunker2D > ArrivalRadius * AtCoverDecisionSlack)
		return;

	AActor* CombatTarget = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));

	// --- Compromise-break detection (enemy flank-break parity) ---
	// Own fast cadence, evaluated BEFORE the dwell/re-eval gates: a point that has stopped
	// protecting must not sit out the ~1 s score cadence while an enemy shoots down the side of it.
	// Geometric flank (arc/body-shield vs the focused target, or body exposed to a known extra
	// threat) needs two consecutive positive evals; a HIT from an enemy the point doesn't shield
	// against confirms instantly — being hit is the proof the angle is real (the enemy's
	// instant-break fast path uses LOS for the same purpose).
	bool bCompromiseBreak = false;
	if (Tuning->bCoverCompromiseBreakEnabled && IsValid(CombatTarget))
	{
		Mem.CompromiseEvalTimer += DeltaSeconds;
		if (Mem.TimeSinceArrival >= Tuning->CoverCompromiseMinDwell
			&& Mem.CompromiseEvalTimer >= Tuning->CoverCompromiseEvalInterval)
		{
			Mem.CompromiseEvalTimer = 0.f;

			// Identity guard: a task-internal shuffle swaps the BB cover with no Mem reset, and a
			// target switch changes what "compromised" means — a stale count must not carry across.
			if (Mem.LastCompromiseEvalCover != CurrentCover.Handle || Mem.LastCompromiseEvalTarget.Get() != CombatTarget)
			{
				Mem.CompromiseConsecutiveCount = 0;
				Mem.LastCompromiseEvalCover = CurrentCover.Handle;
				Mem.LastCompromiseEvalTarget = CombatTarget;
			}

			const ACharacter* CompChar = Cast<ACharacter>(Pawn);
			const UCapsuleComponent* CompCap = CompChar ? CompChar->GetCapsuleComponent() : nullptr;
			const float CompStandoff = (CompCap ? CompCap->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + 10.f;

			const bool bCompromised = CompanionCover::IsCoverCompromised(World, CurrentCover.Data,
				CombatTarget, CombatTarget->GetActorLocation(),
				Tuning->CoverFlankArcHalfAngleDeg, Tuning->CoverCompromiseArcSlackDeg,
				CompStandoff, Tuning->CoverProtectionChestHeight, Pawn);

			// Multi-threat enforcement on the HELD point: body exposed to a second known hostile
			// also counts — else the companion fights one enemy while sitting wide open to another.
			// Feeds the debounced path only, matching the enemy.
			bool bExtraExposed = false;
			if (!bCompromised)
			{
				const int32 MaxExtraEval = FMath::Max(0, Tuning->MaxThreatsForCoverScoring - 1);
				TArray<AActor*, TInlineAllocator<8>> EvalExtraThreats;
				CompanionCover::GatherExtraThreatActors(Controller, Pawn, CombatTarget, MaxExtraEval, EvalExtraThreats);
				bExtraExposed = CompanionCover::CountUncoveredThreats(World, CurrentCover.Data,
					CompStandoff, Tuning->CoverProtectionChestHeight, Pawn, EvalExtraThreats) > 0;
			}

			// Exposed-side hit: instant confirm, no debounce.
			bool bFlankerHit = false;
			const ACompanionCharacter* HitCompanion = Cast<ACompanionCharacter>(Pawn);
			if (HitCompanion && Tuning->FlankerResponseWindow > 0.f)
			{
				const AEnemyCharacter* AttackerEnemy =
					Cast<AEnemyCharacter>(HitCompanion->GetRecentAttacker(Tuning->FlankerResponseWindow));
				if (IsValid(AttackerEnemy))
				{
					// Freshness gate: the hit must postdate the last break — one stale stamp must
					// not break two points in a row while the window is still open.
					const bool bFreshHit = HitCompanion->GetLastAttackerStampTime() > HitCompanion->GetLastCompromiseBreakTime();
					const UHealthComponent* AttackerHealth = AttackerEnemy->GetHealthComponent();
					const bool bAttackerAlive = !AttackerHealth || !AttackerHealth->IsDead();
					if (bFreshHit && bAttackerAlive && !UCoverGeometryStatics::IsThreatCovered(World, CurrentCover.Data,
						AttackerEnemy->GetActorLocation(), CompStandoff, Tuning->CoverProtectionChestHeight,
						AttackerEnemy, Pawn))
						bFlankerHit = true;
				}
			}

			if (bCompromised || bExtraExposed)
				Mem.CompromiseConsecutiveCount = FMath::Min(Mem.CompromiseConsecutiveCount + 1, CompromiseDebounceRequired);
			else
				Mem.CompromiseConsecutiveCount = FMath::Max(0, Mem.CompromiseConsecutiveCount - 1);

			// Debounced (geometric) confirms respect the break cooldown — a crossfire with no
			// protecting point anywhere churns at worst once per cooldown (enemy CoverRelocateCooldown
			// parity). The hit path bypasses it: fresh damage is fresh evidence.
			const bool bGeoCooledDown = !HitCompanion
				|| (World->GetTimeSeconds() - HitCompanion->GetLastCompromiseBreakTime()) >= Tuning->CoverCompromiseBreakCooldown;
			bCompromiseBreak = bFlankerHit
				|| (Mem.CompromiseConsecutiveCount >= CompromiseDebounceRequired && bGeoCooledDown);

			if (bCompromiseBreak && MonitorCovDbg())
				UE_LOG(LogCompanionAI, Log,
					TEXT("[COVDBG] %s COMPROMISE-BREAK hit=%d geo=%d extra=%d consec=%d"),
					*GetNameSafe(Pawn), bFlankerHit ? 1 : 0, bCompromised ? 1 : 0, bExtraExposed ? 1 : 0,
					Mem.CompromiseConsecutiveCount);
		}
	}
	else
	{
		Mem.CompromiseEvalTimer = 0.f;
		Mem.CompromiseConsecutiveCount = 0;
	}

	if (!bCompromiseBreak)
	{
		if (Mem.TimeSinceArrival < Tuning->CoverSwitchMinDwell) return;
		if (Mem.TimeSinceReEval  < Tuning->CoverSwitchReEvalInterval) return;
	}

	// Bail early on no combat target before paying for the bounds query.
	if (!IsValid(CombatTarget)) return;

	Mem.TimeSinceReEval = 0.f;

	// Exit-on-trigger-clear: the commit gate (BTTask_MoveToCoverPoint) has a release side — once
	// nothing demands real cover any more (health recovered, mag topped up, threats thinned, fire
	// lifted) and the exit dwell has passed, vacate and fall back to loose-cover open-engage.
	// Release thresholds are widened (hysteresis) and the post-vacate cooldown blocks an immediate
	// re-commit to the same point, so this can't pop-out thrash.
	// (Compromise break outranks the trigger-clear exit — the break decides relocate-vs-vacate below.)
	if (ACompanionCharacter* ExitCompanion = bCompromiseBreak ? nullptr : Cast<ACompanionCharacter>(Pawn))
	{
		if (Mem.TimeSinceArrival >= FMath::Max(Tuning->CoverSwitchMinDwell, Tuning->CoverExitMinDwell))
		{
			const AWeaponBase* ExitWeapon = ExitCompanion->GetCurrentWeapon();
			const bool bFiringNow = IsValid(ExitWeapon) && ExitWeapon->IsFiring();
			const int32 ExitThreatCount = CompanionCover::CountKnownThreats(Controller, CompanionCover::OutnumberedCountCap(*Tuning));
			const CompanionCover::FCoverTriggers Release =
				CompanionCover::EvaluateTriggers(*ExitCompanion, *Tuning, ExitThreatCount, /*bForRelease=*/true);

			// Natural cycling: after the mode's max commit time at this point with only low-grade
			// pressure, release back to mobile fighting even though a widened release trigger still
			// holds. Combat mode uses the short override — cover is a touch point, not a campsite.
			// The recommit cooldown (both commit sites, bypassed only by HasPressureSpiked — no
			// static low-health bypass) stops it ducking straight back in.
			const float MaxCommitTime = ExitCompanion->GetMode() == ECompanionMode::Combat
				? Tuning->CombatCoverMaxCommitTime
				: Tuning->CoverMaxCommitTime;
			const bool bNaturalRelease = MaxCommitTime > 0.f
				&& Mem.TimeSinceArrival >= MaxCommitTime
				&& !CompanionCover::IsStrongPressure(*ExitCompanion, *Tuning, ExitThreatCount);

			if (!bFiringNow && (!Release.Any() || bNaturalRelease))
			{
				if (UCoverReservationSubsystem* ExitResSub = World->GetSubsystem<UCoverReservationSubsystem>())
					ExitResSub->MarkVacated(CurrentCover.Handle, Controller);
				BB->ClearValue(CoverTargetKey.GetSelectedKeyID());
				if (CoverLocationKey.SelectedKeyName != NAME_None)
					BB->ClearValue(CoverLocationKey.SelectedKeyName);
				BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
				const float TimeAtCover = Mem.TimeSinceArrival;
				Mem = {};
				if (bNaturalRelease && Release.Any())
				{
					ExitCompanion->StampNaturalCoverRelease();
					UE_LOG(LogCompanionAI, Log, TEXT("CoverExit: %s NATURAL release after %.1fs (low-grade pressure) — vacating to open-engage"),
						*GetNameSafe(Pawn), TimeAtCover);
				}
				else
				{
					UE_LOG(LogCompanionAI, Log, TEXT("CoverExit: %s triggers cleared — vacating to open-engage"), *GetNameSafe(Pawn));
				}
				return;
			}
		}
	}

	// Compromise break shared vacate: leave the point and fall back to open-engage (the combat
	// task fights mobile). Used when no candidate protects, or when the loop can't even run.
	auto VacateCompromised = [&]()
	{
		if (ACompanionCharacter* VacCompanion = Cast<ACompanionCharacter>(Pawn))
			VacCompanion->StampCompromiseBreak();
		if (UCoverReservationSubsystem* VacResSub = World->GetSubsystem<UCoverReservationSubsystem>())
			VacResSub->MarkVacated(CurrentCover.Handle, Controller);
		BB->ClearValue(CoverTargetKey.GetSelectedKeyID());
		if (CoverLocationKey.SelectedKeyName != NAME_None)
			BB->ClearValue(CoverLocationKey.SelectedKeyName);
		BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
		Mem = {};
		UE_LOG(LogCompanionAI, Log, TEXT("[COVMOVE] CompromiseBreak: %s vacating to open-engage — no protecting candidate"),
			*GetNameSafe(Pawn));
	};

	// TODO: lift formation-point computation to a shared utility (spec §5.7 open question).
	// FollowPlayer uses a velocity-relative offset; the spec wants a fixed actor-facing offset.
	AActor* Player = Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player))
	{
		if (bCompromiseBreak) VacateCompromised();
		return;
	}

	// Rescue doctrine: while the player is DBNO, score-improvement and advance switches are churn —
	// the companion holds its point and fights. Only the safety paths (compromise break above,
	// trigger-clear exit earlier) may still move it; skip the candidate search entirely otherwise.
	if (!bCompromiseBreak)
	{
		if (const IExtractionPlayerInterface* DBNOPlayer = Cast<IExtractionPlayerInterface>(Player))
		{
			if (DBNOPlayer->GetIsDBNO())
			{
				Mem.PendingBestCover = FCoverHandle();
				Mem.ConsecutiveBetterCount = 0;
				return;
			}
		}
	}

	// Combat mode anchors the candidate search AHEAD of the player (mirrors the FollowPlayer lead)
	// so cover-to-cover switches gain ground instead of hanging back at the follow formation.
	const ACompanionCharacter* CompanionPawn = Cast<ACompanionCharacter>(Pawn);
	const bool bCombatLead = CompanionPawn && CompanionPawn->GetMode() == ECompanionMode::Combat;
	const FVector FormationPoint = bCombatLead
		? Player->GetActorLocation()
			+ Player->GetActorForwardVector() * Tuning->CombatModeLeadDistance
			+ Player->GetActorRightVector()   * Tuning->CombatModeLeadOffsetRight
		: Player->GetActorLocation()
			+ Player->GetActorRightVector()      * Tuning->FormationOffsetRight
			+ (-Player->GetActorForwardVector()) * Tuning->FormationOffsetBack;

	// Shared scorer params: neutral defaults reproduce the old local formula exactly for enemies
	// and for Normal/Stealth companions; a Combat-mode companion gets the advance shift (band floor
	// pulled toward the threat, band term flipped) from GetParamsForQuerier. MaxSearchRadius must
	// track this service's own SearchRadius so proximity normalisation is unchanged.
	FCoverScoreParams ScoreParams;
	if (CompanionPawn)
		ScoreParams = UCoverScoringStatics::GetParamsForQuerier(Pawn);
	ScoreParams.MaxSearchRadius = SearchRadius;

	UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();
	const AController* CoverController = Pawn->GetController();
	const FVector ThreatLoc = CombatTarget->GetActorLocation();
	// Head-height LoS anchor for the peek tests — centre-mass traces read a standing shooter behind
	// crouch cover as blocked (shared resolver with the state service and aim).
	const FVector ThreatSightLoc = AITargeting::GetSightLocation(CombatTarget);

	// Player-advance tracking (Normal/Stealth advance gate): accrue the PLAYER'S OWN displacement
	// projected toward the focused threat, decaying so it needs sustained pushing. Raw range-to-threat
	// deltas would open the gate when an enemy rushes a stationary player.
	{
		const FVector PlayerLoc = Player->GetActorLocation();
		Mem.PlayerAdvanceProgress *= PlayerAdvanceDecayPerReEval;
		if (Mem.bHasPlayerAdvanceSample)
		{
			FVector PlayerDelta = PlayerLoc - Mem.LastPlayerAdvanceLoc;
			PlayerDelta.Z = 0.f;
			const FVector ToThreat2D = (ThreatLoc - Mem.LastPlayerAdvanceLoc).GetSafeNormal2D();
			const float Gain = FVector::DotProduct(PlayerDelta, ToThreat2D);
			if (Gain > 0.f) Mem.PlayerAdvanceProgress += Gain;
		}
		Mem.LastPlayerAdvanceLoc = PlayerLoc;
		Mem.bHasPlayerAdvanceSample = true;
	}

	const ACharacter* ProtectionChar = Cast<ACharacter>(Pawn);
	const UCapsuleComponent* PawnCap = ProtectionChar ? ProtectionChar->GetCapsuleComponent() : nullptr;
	const float ProtectionStandoff = (PawnCap ? PawnCap->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + 10.f;

	TArray<FCover> Candidates;
	Candidates.Reserve(64);
	const FBoxSphereBounds SearchBounds(FormationPoint, FVector(SearchRadius), SearchRadius);
	CoverSys->GetCoverDataWithinBounds(SearchBounds, Candidates);

	// Multi-threat: gather the closest OTHER known threats (beyond the focused CombatTarget) once per
	// re-eval. Candidates that fail to shield the body from these get their score penalised so, when
	// enemies surround the companion, it prefers cover that protects against the most attackers.
	// MaxThreatsForCoverScoring counts the focused target, so extra = that minus one.
	// Returns AActor* (not FVector) so IsThreatCovered can ignore the threat's own body in the trace.
	const int32 MaxExtraThreats = FMath::Max(0, Tuning->MaxThreatsForCoverScoring - 1);
	TArray<AActor*, TInlineAllocator<8>> ExtraThreatActors;
	CompanionCover::GatherExtraThreatActors(Controller, Pawn, CombatTarget, MaxExtraThreats, ExtraThreatActors);
	const float MultiThreatPenalty = FMath::Clamp(Tuning->MultiThreatExposurePenalty, 0.f, 1.f);
	const bool bScoreMultiThreat = ExtraThreatActors.Num() > 0 && MultiThreatPenalty < 1.f && Tuning->bCoverRequiresBodyProtection;

	// Hostile anchors gathered once per re-eval — enemy pawns + covers enemies have declared intent on.
	FHostileAnchors HostileAnchors;
	const bool bRejectHostileAdjacent = Tuning->MinHostileCoverDistance > 0.f || Tuning->MinHostilePawnDistance > 0.f;
	if (bRejectHostileAdjacent)
		UCoverScoringStatics::GatherHostileAnchors(World, Pawn, CoverController, HostileAnchors);

	FCover BestCover;
	float BestScore = -1.f;
	float BestDistSq = FLT_MAX;

	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid()) continue;
		if (Candidate.Handle == CurrentCover.Handle) continue;

		// Skip occupied covers (single lookup — treat occupied-by-self as available).
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != CoverController) continue;

		// Skip covers intended by another agent (claim race guard).
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, CoverController)) continue;

		// P4 — exclude a cover the pawn just deliberately vacated, until the cooldown elapses (anti snap-back).
		if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, CoverController, Tuning->CoverSwitchPostVacateCooldown))
			continue;

		// Target must be within the cover's fire arc.
		const FVector ToTarget2D = (ThreatLoc - Candidate.Data.Location).GetSafeNormal2D();
		const FVector FireFwd    = UCoverGeometryStatics::GetFireArcForward(Candidate.Data);
		const float   ArcDot     = FVector::DotProduct(FireFwd, ToTarget2D);
		if (ArcDot < FMath::Cos(FMath::DegreesToRadians(Tuning->CoverFlankArcHalfAngleDeg))) continue;

		// Hostile-adjacency reject (enemy-parity): never relocate onto a spot an enemy is holding
		// or heading to. Pure 2D distance — runs before the trace-heavy peek gate.
		if (bRejectHostileAdjacent && UCoverScoringStatics::IsNearHostileAnchor(Candidate.Data.Location,
			HostileAnchors, Tuning->MinHostileCoverDistance, Tuning->MinHostilePawnDistance))
			continue;

		// Cover must offer a position with LoS to the target.
		if (!UCoverGeometryStatics::CanPeekShoot(World, Candidate.Data,
			UCoverGeometryStatics::GetCoverHeight(Candidate.Data) == ECoverHeight::Crouch,
			ThreatSightLoc, 150.f, CombatTarget, Pawn))
			continue;

		// Body-protection hard reject: mirrors the old FindBestCoverFor bRequireBodyProtection gate.
		if (Tuning->bCoverRequiresBodyProtection)
		{
			if (!UCoverGeometryStatics::IsThreatCovered(World, Candidate.Data, ThreatLoc,
				ProtectionStandoff, Tuning->CoverProtectionChestHeight, CombatTarget, Pawn))
				continue;
		}

		const float DistSq = FVector::DistSquared(FormationPoint, Candidate.Data.Location);
		// bBodyProtected=false: protection is a hard reject above, not a scored bonus — matches the
		// old local formula which had no protective term.
		float Score = UCoverScoringStatics::ScoreCandidate(World, Candidate.Data, FormationPoint, ThreatLoc,
			/*bBodyProtected=*/ false, ScoreParams);

		// Multi-threat exposure penalty: for each of the closest extra threats this candidate fails to
		// shield the body from, multiply the score down. Prefers cover that protects against the most
		// attackers when surrounded, without hard-rejecting (a partially-exposed cover still beats none).
		if (bScoreMultiThreat)
		{
			int32 UncoveredExtra = 0;
			for (AActor* ThreatActor : ExtraThreatActors)
			{
				if (!IsValid(ThreatActor)) continue;
				if (!UCoverGeometryStatics::IsThreatCovered(World, Candidate.Data, ThreatActor->GetActorLocation(),
					ProtectionStandoff, Tuning->CoverProtectionChestHeight, ThreatActor, Pawn))
					++UncoveredExtra;
			}
			if (UncoveredExtra > 0)
				Score = UCoverScoringStatics::ApplyScorePenalty(Score,
					FMath::Pow(MultiThreatPenalty, static_cast<float>(UncoveredExtra)));
		}

		const bool bBetter = Score > BestScore;
		const bool bTie    = FMath::IsNearlyEqual(Score, BestScore) && DistSq < BestDistSq;
		if (bBetter || bTie)
		{
			BestScore  = Score;
			BestDistSq = DistSq;
			BestCover  = Candidate;
		}
	}

	// --- Compromise-break commit: relocate to the best qualifying candidate, or vacate to
	// open-engage when nothing protects. Bypasses margin/G4/G5/debounce — those guard against
	// churn between two WORKING points; they don't apply to a point that gets the companion shot.
	if (bCompromiseBreak)
	{
		ACompanionCharacter* BreakCompanion = Cast<ACompanionCharacter>(Pawn);

		// Stop any active burst so the transit starts clean (enemy instant-break parity).
		if (BreakCompanion)
			if (AWeaponBase* BreakWeapon = BreakCompanion->GetCurrentWeapon())
				if (BreakWeapon->IsFiring()) BreakWeapon->StopFiring();

		if (BestCover.IsValid())
		{
			if (BreakCompanion)
			{
				// Purposeful-pick grant: without it MoveToCoverPoint's commit gate re-adjudicates
				// against the (stricter) commit triggers and declines inside the hysteresis band —
				// the relocate half of the break would only ever run under heavy pressure.
				BreakCompanion->SetCoverCommitGrant(true);
				BreakCompanion->StampCompromiseBreak();
			}
			if (IsValid(ResSub))
			{
				ResSub->MarkVacated(CurrentCover.Handle, Controller);
				ResSub->SetIntendedCover(Controller, BestCover.Handle);
			}
			BB->SetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID(), BestCover);
			if (CoverLocationKey.SelectedKeyName != NAME_None)
				BB->SetValueAsVector(CoverLocationKey.SelectedKeyName, BestCover.Data.Location);
			BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
			Mem = {};
			UE_LOG(LogCompanionAI, Log,
				TEXT("[COVMOVE] CompromiseBreak: %s relocating — held point no longer protects (bestScore=%.2f)"),
				*GetNameSafe(Pawn), BestScore);
		}
		else
		{
			VacateCompromised();
		}
		return;
	}

	if (!BestCover.IsValid())
	{
		// No better candidate this re-eval — reset the debounce so a future winner must agree fresh. (G2)
		Mem.PendingBestCover = FCoverHandle();
		Mem.ConsecutiveBetterCount = 0;
		return;
	}

	float CurrentScore = UCoverScoringStatics::ScoreCandidate(World, CurrentCover.Data, FormationPoint, ThreatLoc,
		/*bBodyProtected=*/ false, ScoreParams);

	// Apply the same penalties to CurrentScore that candidates receive — without this the current
	// cover gets a free pass on arc violations and multi-threat exposure, making the 1.2x beat margin
	// nearly impossible to overcome even when the current cover is genuinely bad.
	if (bScoreMultiThreat)
	{
		int32 CurUncoveredExtra = 0;
		for (AActor* ThreatActor : ExtraThreatActors)
		{
			if (!IsValid(ThreatActor)) continue;
			if (!UCoverGeometryStatics::IsThreatCovered(World, CurrentCover.Data, ThreatActor->GetActorLocation(),
				ProtectionStandoff, Tuning->CoverProtectionChestHeight, ThreatActor, Pawn))
				++CurUncoveredExtra;
		}
		if (CurUncoveredExtra > 0)
			CurrentScore = UCoverScoringStatics::ApplyScorePenalty(CurrentScore,
				FMath::Pow(MultiThreatPenalty, static_cast<float>(CurUncoveredExtra)));
	}

	// Arc-violation penalty: if the focused target is outside the widened arc for the current cover,
	// penalise the score so a better-positioned candidate can win the margin comparison.
	{
		const FVector CurToTarget2D = (ThreatLoc - CurrentCover.Data.Location).GetSafeNormal2D();
		const FVector CurFireFwd    = UCoverGeometryStatics::GetFireArcForward(CurrentCover.Data);
		const float   CurArcDot     = FVector::DotProduct(CurFireFwd, CurToTarget2D);
		const float   CurWidenedArc = Tuning->CoverFlankArcHalfAngleDeg + Tuning->CoverCompromiseArcSlackDeg;
		if (CurArcDot < FMath::Cos(FMath::DegreesToRadians(CurWidenedArc)))
			CurrentScore = UCoverScoringStatics::ApplyScorePenalty(CurrentScore, MultiThreatPenalty);
	}

	// Blind-current: no peek position at the current point has LoS to the focused target. Such a
	// point can never earn peek cycles (the G5 gate would deadlock it — same rationale as the combat
	// task's blind-shuffle exemption) and must not defend its score, or the companion camps a point
	// it cannot shoot from. Every candidate above already passed the CanPeekShoot filter, so a commit
	// here always lands on a point with a verified shot. Persistence: an enemy's own peek cycle
	// samples blind/visible alternately at re-eval cadence — require 2 consecutive blind re-evals
	// before the penalty + G5 bypass, or the monitor flip-flops covers chasing moments.
	const bool bBlindNow = !UCoverGeometryStatics::CanPeekShoot(World, CurrentCover.Data,
		UCoverGeometryStatics::GetCoverHeight(CurrentCover.Data) == ECoverHeight::Crouch,
		ThreatSightLoc, 150.f, CombatTarget, Pawn);
	// Identity guard: a task-internal shuffle swaps the BB cover with no Mem reset, and a target
	// switch changes what "blind" means — a stale count must not carry across either.
	if (Mem.LastBlindEvalCover != CurrentCover.Handle || Mem.LastBlindEvalTarget.Get() != CombatTarget)
	{
		Mem.ConsecutiveBlindReEvals = 0;
		Mem.LastBlindEvalCover = CurrentCover.Handle;
		Mem.LastBlindEvalTarget = CombatTarget;
	}
	Mem.ConsecutiveBlindReEvals = bBlindNow ? Mem.ConsecutiveBlindReEvals + 1 : 0;
	const bool bCurrentBlind = Mem.ConsecutiveBlindReEvals >= 2;
	if (bCurrentBlind)
		CurrentScore = UCoverScoringStatics::ApplyScorePenalty(CurrentScore, MultiThreatPenalty);

	// Advance ("move up, take space"): a candidate that meaningfully gains ground toward the threat
	// — and already passed the body-protection gate in the loop above — only needs AdvanceScoreMargin,
	// so a protected sidegrade can take ground instead of waiting for a 1.2x upgrade that never comes.
	// Combat mode advances freely; Normal/Stealth advance only while the PLAYER is gaining ground on
	// the threat (companion takes space with you, not ahead of you). Ordinary switches keep
	// CoverSwitchScoreMargin.
	float RequiredMargin = Tuning->CoverSwitchScoreMargin;
	if (Tuning->bCombatAllowAdvance
		&& (bCombatLead || Mem.PlayerAdvanceProgress >= Tuning->PlayerAdvanceGateDistance))
	{
		constexpr float MinAdvanceGain = 150.f;
		const float CandDistToThreat = FVector::Dist2D(BestCover.Data.Location, ThreatLoc);
		const float CurDistToThreat  = FVector::Dist2D(CurrentCover.Data.Location, ThreatLoc);
		// Combat mode uses its own (stiffer) advance margin — the free-advance 1.0 sidegrade bar
		// read as cover-to-cover churn with no firing once cover was force-taken.
		if (CandDistToThreat < CurDistToThreat - MinAdvanceGain)
			RequiredMargin = bCombatLead ? Tuning->CombatAdvanceScoreMargin : Tuning->AdvanceScoreMargin;
	}

	// Multiplicative margins invert for non-positive scores (cur -0.55 x 1.2 = -0.66 LOWERS the
	// bar → any candidate wins every re-eval → constant churn). Non-positive current scores use an
	// absolute improvement bar instead.
	const bool bBeatsBar = CurrentScore > 0.f
		? BestScore >= CurrentScore * RequiredMargin
		: BestScore >= CurrentScore + Tuning->CoverSwitchMinScoreGain;
	if (!bBeatsBar) // P6
	{
		Mem.PendingBestCover = FCoverHandle();
		Mem.ConsecutiveBetterCount = 0;
		return;
	}

	// G4 — hold the switch while firing. Return WITHOUT advancing the debounce so a burst doesn't count
	// as an agreeing re-eval; the switch resumes evaluating once the weapon stops.
	const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn);
	const AWeaponBase* Weapon = Companion ? Companion->GetCurrentWeapon() : nullptr;
	if (IsValid(Weapon) && Weapon->IsFiring()) return;

	// G5 — commit gate (enemy MinPeekCyclesBeforeRelocate parity): a score-based switch may not
	// move a companion off a point it never completed a peek cycle at. Trigger-driven exits above
	// stay ungated, and a blind current point is exempt — it can never earn a cycle, so the gate
	// would deadlock it. Reset the pending debounce so a stale candidate can't insta-commit later.
	if (!bCurrentBlind && Companion && Companion->GetPeekCyclesAtCurrentCover() < Tuning->MinPeekCyclesBeforeRelocate)
	{
		if (MonitorCovDbg())
			UE_LOG(LogCompanionAI, Log, TEXT("[COVDBG] %s MONITOR G5-skip cycles=%d < %d (best=%.2f cur=%.2f)"),
				*GetNameSafe(Pawn), Companion->GetPeekCyclesAtCurrentCover(),
				Tuning->MinPeekCyclesBeforeRelocate, BestScore, CurrentScore);
		Mem.PendingBestCover = FCoverHandle();
		Mem.ConsecutiveBetterCount = 0;
		return;
	}

	// G2 — debounce: require two consecutive re-evals agreeing on the same candidate before committing.
	if (Mem.PendingBestCover == BestCover.Handle)
	{
		++Mem.ConsecutiveBetterCount;
	}
	else
	{
		Mem.PendingBestCover       = BestCover.Handle;
		Mem.ConsecutiveBetterCount = 1;
	}

	if (Mem.ConsecutiveBetterCount < Tuning->CoverSwitchRequiredAgreeingReEvals) return;

	// Commit = BB write (plugin's Keep Cover Occupied service auto-occupies) + post-vacate stamp on the
	// old point. No manual claim handshake — that's the old registry's job, dropped entirely.
	if (IsValid(ResSub))
	{
		ResSub->MarkVacated(CurrentCover.Handle, Controller);
		ResSub->SetIntendedCover(Controller, BestCover.Handle);
	}

	BB->SetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID(), BestCover);
	if (CoverLocationKey.SelectedKeyName != NAME_None)
		BB->SetValueAsVector(CoverLocationKey.SelectedKeyName, BestCover.Data.Location);
	BB->SetValueAsBool(HasCoverPositionKey.SelectedKeyName, false);
	Mem = {};

	UE_LOG(LogCompanionAI, Log,
		TEXT("[COVMOVE] CoverSwitch: %s -> new cover (curScore=%.2f, bestScore=%.2f, margin=%.2fx, cyclesAtOld=%d)"),
		*GetNameSafe(Pawn), CurrentScore, BestScore, Tuning->CoverSwitchScoreMargin,
		Companion ? Companion->GetPeekCyclesAtCurrentCover() : -1);
}

void UBTService_CoverSwitchMonitor::OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnCeaseRelevant(OwnerComp, NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (BB)
	{
		AAIController* Controller = OwnerComp.GetAIOwner();
		UWorld* World = OwnerComp.GetWorld();
		UCoverReservationSubsystem* ResSub = World ? World->GetSubsystem<UCoverReservationSubsystem>() : nullptr;
		if (IsValid(ResSub) && IsValid(Controller))
			ResSub->ClearIntendedCover(Controller);
	}

	*reinterpret_cast<FCoverSwitchMonitorMemory*>(NodeMemory) = {};
}

FString UBTService_CoverSwitchMonitor::GetStaticDescription() const
{
	return FString::Printf(TEXT("Cover Switch Monitor (radius: %.0f)"), SearchRadius);
}
