// BTTask_MoveToCoverPoint — latent move to an AICS cover point with advance-fire + stall detection.

#include "BTTask_MoveToCoverPoint.h"
#include "AI/AITargetingStatics.h"
#include "AI/CompanionDiag.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "AI/CompanionCoverStatics.h"
#include "CoverScoringStatics.h"
#include "CoverSystem.h"
#include "CoverGeometryStatics.h"
#include "CoverReservationSubsystem.h"
#include "CoverPoseComponent.h"
#include "CompanionCharacter.h"
#include "CompanionAIController.h"
#include "CompanionTuningDataAsset.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyCharacter.h"
#include "SuppressionComponent.h"
#include "WeaponBase.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "EnemyDebug.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

// Arrival distance thresholds — match BTTask_EnemyMoveToCover constants
// Tight tolerances: a 50cm accept + 55cm drift threshold let the pawn sit half a body proud of
// the wall in the tucked idle. 25/30/45 keeps the hug visually flush without nav-goal dancing.
static constexpr float CoverArrivalAcceptRadius = 25.f;
static constexpr float CoverArrivalTickRadius   = 30.f;
static constexpr float CoverArrivalIdleRadius   = 45.f;
// Prefixed (vs the plain DefaultCapsuleRadius in BTTask_EnemyCombatFire.cpp) — unity builds can
// merge the two TUs into one chunk, where file-scope statics with the same name collide.
static constexpr float MoveToCoverDefaultCapsuleRadius = 34.f;
/** Chest height (cm) for the arrival body-protection trace. Same prefix rule as the capsule radius
 *  above — mirrors BodyProtectChestHeight in BTTask_EnemyCombatFire.cpp without colliding under unity. */
static constexpr float MoveToCoverBodyProtectChestHeight = 60.f;
static constexpr float FireTickInterval         = 0.1f;
/** Seconds between mid-move destination-claim rechecks (occupied/intended by someone else). */
static constexpr float ClaimCheckInterval       = 0.25f;

UBTTask_MoveToCoverPoint::UBTTask_MoveToCoverPoint()
{
	NodeName = TEXT("Move To Cover Point");
	bNotifyTick = true;

	// Key filters will be set in InitializeFromAsset
}

void UBTTask_MoveToCoverPoint::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	if (UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		CoverTargetKey.ResolveSelectedKey(*BBAsset);
		HasCoverKey.ResolveSelectedKey(*BBAsset);
	}
}

uint16 UBTTask_MoveToCoverPoint::GetInstanceMemorySize() const
{
	return sizeof(FMoveToCoverPointMemory);
}

void UBTTask_MoveToCoverPoint::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	Super::InitializeMemory(OwnerComp, NodeMemory, InitType);
	FMoveToCoverPointMemory* Mem = CastInstanceNodeMemory<FMoveToCoverPointMemory>(NodeMemory);
	new (Mem) FMoveToCoverPointMemory();
}

void UBTTask_MoveToCoverPoint::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	FMoveToCoverPointMemory* Mem = CastInstanceNodeMemory<FMoveToCoverPointMemory>(NodeMemory);
	Mem->~FMoveToCoverPointMemory();
	Super::CleanupMemory(OwnerComp, NodeMemory, CleanupType);
}

EBTNodeResult::Type UBTTask_MoveToCoverPoint::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FMoveToCoverPointMemory* Mem = CastInstanceNodeMemory<FMoveToCoverPointMemory>(NodeMemory);
	Mem->Reset();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	// Purposeful commit grant (angle-seek / Combat advance hop): consume UP FRONT — above the
	// EQS-empty return and the claim-guard decline — so no path through this task can leave a
	// stale grant. Time-stamped; expired reads false.
	bool bCommitGrant = false;
	if (ACompanionCharacter* GrantCompanion = Cast<ACompanionCharacter>(Pawn))
		bCommitGrant = GrantCompanion->ConsumeCoverCommitGrant();

	// Set HasCover false at start (mirror old task's ReleaseClaim semantics)
	if (HasCoverKey.SelectedKeyName != NAME_None)
		BB->SetValueAsBool(HasCoverKey.SelectedKeyName, false);

	// Read the cover from the BB via the typed key accessor (avoids GetOuter null for non-instanced
	// nodes). Non-const: the companion's multi-threat re-rank below may override the EQS pick.
	FCover Cover = BB->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
	if (!Cover.IsValid()) return EBTNodeResult::Failed;

	// Companion: a switch-monitor commit writes CoverTarget + stamps intent, then the combat task
	// finishes — but the BT loop's EQS task re-picks and overwrites CoverTarget before this task
	// runs, silently discarding the monitor's (often advancing) choice. Restore our own stamped
	// intent when it still resolves to live, unoccupied cover.
	if (Cast<ACompanionCharacter>(Pawn))
	{
		UWorld* IntentWorld = OwnerComp.GetWorld();
		UCoverReservationSubsystem* IntentSub = IntentWorld ? IntentWorld->GetSubsystem<UCoverReservationSubsystem>() : nullptr;
		ACoverSystem* IntentCoverSys = ACoverSystem::GetCoverSystem(IntentWorld);
		FCoverHandle IntendedHandle;
		if (IntentSub && IntentCoverSys
			&& IntentSub->GetIntendedCover(Controller, IntendedHandle)
			&& IntendedHandle != Cover.Handle)
		{
			FCoverData IntendedData;
			// Reject intents pointing at a cover this controller deliberately vacated — a stale stamp
			// must not walk the companion back onto its own post-vacate cooldown.
			const ACompanionAIController* IntentCompCtrl = Cast<ACompanionAIController>(Controller);
			const UCompanionTuningDataAsset* IntentTuning = IntentCompCtrl ? IntentCompCtrl->GetTuning() : nullptr;
			const bool bIntentOnCooldown = IntentTuning
				&& IntentSub->IsOnPostVacateCooldown(IntendedHandle, Controller, IntentTuning->CoverSwitchPostVacateCooldown);
			if (!bIntentOnCooldown && IntentCoverSys->GetCoverData(IntendedHandle, IntendedData))
			{
				AController* IntentOccupant = IntentCoverSys->GetOccupyingController(IntendedHandle);
				if (!IntentOccupant || IntentOccupant == Controller)
				{
					Cover.Handle = IntendedHandle;
					Cover.Data = IntendedData;
					BB->SetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID(), Cover);
					UE_LOG(LogCompanionAI, Log, TEXT("[COVMOVE] %s restored monitor-intended cover over EQS re-pick loc=%s"),
						*Pawn->GetName(), *Cover.Data.Location.ToCompactString());
				}
			}
		}
	}

	// Claim guard at pick time: the EQS result can be stale by the time this task runs, and the
	// plugin's OccupyCover fails SILENTLY for the loser — never start toward an occupied cover.
	if (ACoverSystem* ClaimCoverSys = ACoverSystem::GetCoverSystem(OwnerComp.GetWorld()))
	{
		AController* Occupant = ClaimCoverSys->GetOccupyingController(Cover.Handle);
		if (Occupant && Occupant != Controller)
		{
			UE_LOG(LogEnemyAI, Log, TEXT("[COVER-AICS] %s picked cover already occupied by %s — failing for re-pick"),
				*Pawn->GetName(), *GetNameSafe(Occupant->GetPawn()));
			// Drop any lingering own intent (e.g. a monitor stamp) — leaving it stamped lets the
			// intent-restore above resurrect a stale point on a later run.
			if (UCoverReservationSubsystem* DeclineResSub = OwnerComp.GetWorld() ? OwnerComp.GetWorld()->GetSubsystem<UCoverReservationSubsystem>() : nullptr)
				DeclineResSub->ClearIntendedCover(Controller);
			BB->ClearValue(CoverTargetKey.GetSelectedKeyID());
			return EBTNodeResult::Failed;
		}
	}

	// Companion cover-commit gate: commit only when a situational trigger demands real cover
	// (under fire / low HP / reloading-low ammo / outnumbered) and the point is within the outer
	// sanity cap. Declining fails the task — the BT's ForceSuccess decorator routes on to
	// open-engage stand-fighting (loose cover bias). Enemies are unaffected; their commit policy
	// lives in the fire task's FSM.
	if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn))
	{
		const ACompanionAIController* CompCtrl = Cast<ACompanionAIController>(Controller);
		if (const UCompanionTuningDataAsset* Tuning = CompCtrl ? CompCtrl->GetTuning() : nullptr)
		{
			const float CommitDist = FVector::Dist(Pawn->GetActorLocation(), Cover.Data.Location);
			const int32 ThreatCount = CompanionCover::CountKnownThreats(Controller, CompanionCover::OutnumberedCountCap(*Tuning));
			const CompanionCover::FCoverTriggers Triggers =
				CompanionCover::EvaluateTriggers(*Companion, *Tuning, ThreatCount, /*bForRelease=*/false);

			// Natural-cycling recommit cooldown: after a committed-time release, don't duck straight
			// back in unless pressure has genuinely spiked (shared bar with the monitor's release).
			bool bRecommitBlocked = false;
			if (!bCommitGrant && Tuning->NaturalReleaseRecommitCooldown > 0.f && OwnerComp.GetWorld())
			{
				const float SinceRelease = OwnerComp.GetWorld()->GetTimeSeconds() - Companion->GetLastNaturalReleaseTime();
				bRecommitBlocked = SinceRelease < Tuning->NaturalReleaseRecommitCooldown
					&& !CompanionCover::HasPressureSpiked(*Companion, *Tuning, ThreatCount);
			}

			// Low-HP dash gate: wounded-and-alone never sprints open ground for a duck spot. The
			// pick is known here, so gate on ITS distance (the reseek pre-gate uses nearest-cover).
			const bool bDashAllowed = bCommitGrant || !Triggers.LowHealthOnly()
				|| Tuning->LowHealthCoverMaxDash <= 0.f
				|| CommitDist <= Tuning->LowHealthCoverMaxDash;

			// Point-blank visible chaser: never run for cover with a threat at knife range — the
			// decline routes to open-engage, which turns and fights (mirrors the combat task's
			// point-blank release; overrides grants too, a hop into a chaser's lap is never right).
			bool bPointBlankThreat = false;
			if (Tuning->PointBlankAbandonDistance > 0.f)
			{
				AActor* PbTarget = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget));
				bPointBlankThreat = IsValid(PbTarget)
					&& FVector::Dist2D(Pawn->GetActorLocation(), PbTarget->GetActorLocation()) <= Tuning->PointBlankAbandonDistance
					&& Controller->LineOfSightTo(PbTarget);
			}

			// Wrong-floor pick: cover only shields on the floor you stand on, and the EQS/scans can
			// hand back a slot one storey up (baked multi-floor cover set). Committing to it sends
			// the companion crouch-walking into the open toward an unreachable hunker. Overrides
			// grants too — no trigger justifies a cross-floor duck spot.
			const bool bOffLevel = Tuning->CoverPickMaxZDelta > 0.f
				&& FMath::Abs(Pawn->GetActorLocation().Z - Cover.Data.Location.Z) > Tuning->CoverPickMaxZDelta;

			// CoverCommitMaxDistance is now an outer sanity cap (never trek across the map for a
			// duck spot), not the old decline-happy 6.5m radius.
			const float OuterCap = FMath::Max(Tuning->CoverCommitMaxDistance, Tuning->CoverSearchRadius);
			if (CommitDist > OuterCap || (!Triggers.Any() && !bCommitGrant) || bRecommitBlocked || !bDashAllowed
				|| bPointBlankThreat || bOffLevel)
			{
				UE_LOG(LogCompanionAI, Log,
					TEXT("%s: cover-commit DECLINED dist=%.0f cap=%.0f triggers[fire=%d hp=%d ammo=%d out#=%d(n=%d)] grant=%d recommitBlock=%d dashOK=%d pointBlank=%d offLevel=%d — open-engage"),
					*Pawn->GetName(), CommitDist, OuterCap, Triggers.bUnderFire ? 1 : 0, Triggers.bLowHealth ? 1 : 0,
					Triggers.bLowAmmoOrReloading ? 1 : 0, Triggers.bOutnumbered ? 1 : 0, ThreatCount,
					bCommitGrant ? 1 : 0, bRecommitBlocked ? 1 : 0, bDashAllowed ? 1 : 0, bPointBlankThreat ? 1 : 0,
					bOffLevel ? 1 : 0);
				// Drop any lingering own intent — same stale-restore guard as the claim decline above.
				if (UCoverReservationSubsystem* DeclineResSub = OwnerComp.GetWorld() ? OwnerComp.GetWorld()->GetSubsystem<UCoverReservationSubsystem>() : nullptr)
					DeclineResSub->ClearIntendedCover(Controller);
				BB->ClearValue(CoverTargetKey.GetSelectedKeyID());
				return EBTNodeResult::Failed;
			}

			// Multi-threat first pick: the shared EQS scores exposure against the FOCUSED target only
			// — when several enemies converge, the EQS winner can be wide open to the others until the
			// switch monitor's next re-eval. Re-rank locally against all known threats and override the
			// BB if a better-shielding candidate exists. Companion-only; the shared scorer is untouched.
			const FCover Reranked = RerankCoverForMultiThreat(OwnerComp, Controller, Pawn, *Tuning, Cover);
			if (Reranked.IsValid() && Reranked.Handle != Cover.Handle)
			{
				Cover = Reranked;
				BB->SetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID(), Cover);
			}

			// Path-threat gate on the FINAL pick (post re-rank): the straight-line approach must not
			// pass through the known threat set. The EQS pick never sees the switch monitor's path
			// reject, so without this gate a compromise relocate or fresh commit can charge the enemy
			// group to reach its scoring winner. Decline routes to open-engage (ForceSuccess decorator),
			// where the companion fights mobile instead of running the gauntlet.
			AActor* PathGateTarget = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget));
			TArray<AActor*, TInlineAllocator<8>> PathThreats;
			CompanionCover::GatherExtraThreatActors(Controller, Pawn, PathGateTarget,
				FMath::Max(0, Tuning->MaxThreatsForCoverScoring - 1), PathThreats);
			if (IsValid(PathGateTarget)) PathThreats.Add(PathGateTarget);
			if (CompanionCover::PathPassesNearThreat(Pawn->GetActorLocation(), Cover.Data.Location,
				PathThreats, Tuning->RelocatePathThreatClearance))
			{
				UE_LOG(LogCompanionAI, Log,
					TEXT("%s: cover-commit DECLINED reason=path-through-threats dist=%.0f clearance=%.0f — open-engage"),
					*Pawn->GetName(), FVector::Dist2D(Pawn->GetActorLocation(), Cover.Data.Location),
					Tuning->RelocatePathThreatClearance);
				if (UCoverReservationSubsystem* DeclineResSub = OwnerComp.GetWorld() ? OwnerComp.GetWorld()->GetSubsystem<UCoverReservationSubsystem>() : nullptr)
					DeclineResSub->ClearIntendedCover(Controller);
				BB->ClearValue(CoverTargetKey.GetSelectedKeyID());
				return EBTNodeResult::Failed;
			}
		}
	}

	// Enemy wrong-floor gate — the enemy counterpart of the companion off-level decline above.
	// Enemies skipped every commit sanity check and took whatever the EQS returned, so a slot one
	// storey up sent them jogging into open ground toward a hunker that shields nothing. Failing
	// here routes the BT back through its own re-pick, same as the occupancy decline.
	if (const AEnemyCharacter* ZEnemy = Cast<AEnemyCharacter>(Pawn))
	{
		const UEnemyArchetypeData* ZDA = ZEnemy->GetArchetypeData();
		if (IsValid(ZDA) && ZDA->CoverPickMaxZDelta > 0.f
			&& FMath::Abs(Pawn->GetActorLocation().Z - Cover.Data.Location.Z) > ZDA->CoverPickMaxZDelta)
		{
			UE_LOG(LogEnemyAI, Log, TEXT("[COVER-AICS] %s declined off-level cover (dZ=%.0f > %.0f) — re-picking"),
				*Pawn->GetName(), FMath::Abs(Pawn->GetActorLocation().Z - Cover.Data.Location.Z),
				ZDA->CoverPickMaxZDelta);
			if (UCoverReservationSubsystem* ZResSub = OwnerComp.GetWorld() ? OwnerComp.GetWorld()->GetSubsystem<UCoverReservationSubsystem>() : nullptr)
			{
				// Stamp post-vacate as well as clearing intent. This decline costs no travel time, so
				// without the stamp the EQS would hand back the same off-level point next run and the
				// task would spin on it every frame. The post-vacate test filters it out for a beat.
				ZResSub->MarkVacated(Cover.Handle, Controller);
				ZResSub->ClearIntendedCover(Controller);
			}
			BB->ClearValue(CoverTargetKey.GetSelectedKeyID());
			return EBTNodeResult::Failed;
		}
	}

	Mem->CoverHandle = Cover.Handle;
	Mem->CachedCoverData = Cover.Data;

	// Cache enemy-specific data
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	const UEnemyArchetypeData* DA = Enemy ? Enemy->GetArchetypeData() : nullptr;
	Mem->CachedEnemy = Enemy;
	Mem->CachedDA = DA;

	// A pose latched at a previous cover would play the full-body cover montage for the whole
	// transit (montage suppresses locomotion → floor-slide). Enemy-only; the companion's combat
	// task owns its pose lifecycle.
	if (IsValid(Enemy))
	{
		if (UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent()) PoseComp->ResetCoverPose();
	}

	// Point-blank override: skip cover entirely so the selector falls through to open-ground fire (enemy only)
	if (IsValid(Enemy) && IsValid(DA) && DA->PointBlankFireRange > 0.f)
	{
		AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
		if (IsValid(Target))
		{
			const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), Target->GetActorLocation());
			if (DistSq <= FMath::Square(DA->PointBlankFireRange))
				return EBTNodeResult::Failed;
		}
	}

	// Compute arrival position
	const FVector PawnLoc = Pawn->GetActorLocation();
	float CapsuleRadius = MoveToCoverDefaultCapsuleRadius;
	if (const ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (const UCapsuleComponent* Cap = Char->GetCapsuleComponent())
			CapsuleRadius = Cap->GetScaledCapsuleRadius();
	}
	const float StandoffPadding = IsValid(DA) ? DA->CoverStandoffPadding : DefaultStandoffPadding;
	const float Standoff = CapsuleRadius + StandoffPadding;

	// Edge-aligned arrival for BOTH species — endpoint covers snap laterally to a fixed gap from
	// the wall corner so peeks clear consistently regardless of bake spacing. Companions used to
	// keep the plain approach position, which parked them mid-wall: endpoint covers resolved Front
	// (over-top only — no corner peeks, coin-flip idle side). Z from the pawn (nav height).
	FVector ArrivalPos;
	if (IsValid(DA))
	{
		ArrivalPos = UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
			OwnerComp.GetWorld(), Cover.Data, Standoff, CapsuleRadius, DA->CoverCornerGap, Pawn);
	}
	else if (const ACompanionCharacter* ArrCompanion = Cast<ACompanionCharacter>(Pawn))
	{
		ArrivalPos = CompanionCover::CompanionHunkerPosition(*ArrCompanion, Cover.Data, Standoff);
		ArrivalPos.Z = PawnLoc.Z;
	}
	else
	{
		ArrivalPos = UCoverGeometryStatics::GetApproachPosition(Cover.Data, PawnLoc, Standoff);
	}
	ArrivalPos.Z = PawnLoc.Z;
	Mem->ArrivalPos = ArrivalPos;

	// Already at destination? (2D — ArrivalPos is on the cover Z plane, see arrival check below)
	if (FVector::Dist2D(PawnLoc, ArrivalPos) <= CoverArrivalAcceptRadius)
	{
		HandleArrival(OwnerComp, Mem, BB, Pawn, Cover.Data);
		return EBTNodeResult::Succeeded;
	}

	// Set move speed to combat for enemy pawns
	if (IsValid(Enemy)) Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	// Set intended cover in reservation subsystem
	UWorld* World = OwnerComp.GetWorld();
	UCoverReservationSubsystem* ResSub = World ? World->GetSubsystem<UCoverReservationSubsystem>() : nullptr;
	if (IsValid(ResSub)) ResSub->SetIntendedCover(Controller, Cover.Handle);

	// Issue move
	if (GetCoverAnimLogLevel() > 0 && IsValid(Enemy))
	{
		const UCoverPoseComponent* PoseDbg = Enemy->GetCoverPoseComponent();
		UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s MOVE(to-cover) posed=%d"),
			*GetNameSafe(Pawn), (IsValid(PoseDbg) && PoseDbg->bInCover) ? 1 : 0);
	}
	const EPathFollowingRequestResult::Type MoveResult = Controller->MoveToLocation(
		ArrivalPos, CoverArrivalAcceptRadius, false, true, true, true);
	Mem->bMoveIssued = true;

	// Combat-move diagnostic (companion only) — mirrors BTTask_CompanionCombat's [CombatMove] lines
	// so every mover that can walk the companion out of a fight is tagged in one log stream.
	if (Cast<ACompanionCharacter>(Pawn) && UE_LOG_ACTIVE(LogCompanionDiag, Log))
	{
		AActor* DiagTarget = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
		float TowardTargetDot = 0.f;
		FVector MoveDir = ArrivalPos - PawnLoc;
		MoveDir.Z = 0.f;
		const float Disp = MoveDir.Size();
		if (Disp > KINDA_SMALL_NUMBER) MoveDir /= Disp;
		if (IsValid(DiagTarget) && Disp > KINDA_SMALL_NUMBER)
		{
			FVector ToTarget = DiagTarget->GetActorLocation() - PawnLoc;
			ToTarget.Z = 0.f;
			if (ToTarget.SizeSquared() > KINDA_SMALL_NUMBER)
				TowardTargetDot = FVector::DotProduct(MoveDir, ToTarget.GetSafeNormal());
		}
		UE_LOG(LogCompanionDiag, Log, TEXT("%s: [CombatMove] site=cover-move dest=%s disp=%.0f towardTargetDot=%+.2f coverLoc=%s"),
			*Pawn->GetName(), *ArrivalPos.ToCompactString(), Disp, TowardTargetDot, *Cover.Data.Location.ToCompactString());
	}

	UE_LOG(LogEnemyAI, Verbose, TEXT("[COVER-AICS] %s MoveTo (%.0f,%.0f,%.0f) dist=%.0f result=%d"),
		*Pawn->GetName(), ArrivalPos.X, ArrivalPos.Y, ArrivalPos.Z,
		FVector::Dist(PawnLoc, ArrivalPos), static_cast<int32>(MoveResult));

	// Advance-fire setup (enemy only)
	if (bFireWhileAdvancing && IsValid(Enemy) && IsValid(DA) && DA->bFireWhileAdvancing)
	{
		AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
		if (IsValid(Target))
		{
			Enemy->SetAimTarget(Target);
			Controller->SetFocus(Target);
			Enemy->SetExtraSpreadDegrees(DA->AdvanceFireExtraSpreadDeg);
			Mem->FirePhase = EMoveShootFirePhase::Acquire;
			Mem->FireTimer = DA->ReactionDelay;
		}
	}

	return EBTNodeResult::InProgress;
}

void UBTTask_MoveToCoverPoint::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FMoveToCoverPointMemory* Mem = CastInstanceNodeMemory<FMoveToCoverPointMemory>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn)
	{
		StopAdvanceFire(OwnerComp, Mem, false);
		// Clear intended cover via OwnerComp world (pawn may be dead)
		if (IsValid(Controller))
		{
			UWorld* World = OwnerComp.GetWorld();
			UCoverReservationSubsystem* ResSub = World ? World->GetSubsystem<UCoverReservationSubsystem>() : nullptr;
			if (IsValid(ResSub)) ResSub->ClearIntendedCover(Controller);
		}
		HandleFailure(OwnerComp, Mem, BB, Controller);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	if (!Mem->bMoveIssued) return;

	UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
	if (!PF)
	{
		StopAdvanceFire(OwnerComp, Mem, false);
		HandleFailure(OwnerComp, Mem, BB, Controller);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	const FVector PawnLoc = Pawn->GetActorLocation();
	// 2D — ArrivalPos sits on the cover's Z plane, not at capsule-centre height; with the
	// tightened radii a 3D check would eat most of the budget in vertical offset.
	const float Dist = FVector::Dist2D(PawnLoc, Mem->ArrivalPos);
	const EPathFollowingStatus::Type Status = PF->GetStatus();
	const bool bArrived = (Dist <= CoverArrivalTickRadius) ||
		(Status == EPathFollowingStatus::Idle && Dist <= CoverArrivalIdleRadius);

	AEnemyCharacter* Enemy = Mem->CachedEnemy.Get();
	const UEnemyArchetypeData* DA = Mem->CachedDA.Get();

	// --- Mid-move claim revalidation (throttled) ---
	// The destination can be taken while en route (the plugin's OccupyCover fails SILENTLY for
	// the loser) — fail early and let the BT re-pick instead of posing into someone else's cover.
	Mem->ClaimCheckAccum += DeltaSeconds;
	if (!bArrived && Mem->ClaimCheckAccum >= ClaimCheckInterval)
	{
		Mem->ClaimCheckAccum = 0.f;
		UWorld* ClaimWorld = OwnerComp.GetWorld();
		ACoverSystem* ClaimCoverSys = ACoverSystem::GetCoverSystem(ClaimWorld);
		if (ClaimCoverSys)
		{
			AController* Occupant = ClaimCoverSys->GetOccupyingController(Mem->CoverHandle);
			bool bStolen = Occupant && Occupant != Controller;
			if (!bStolen)
			{
				UCoverReservationSubsystem* ClaimResSub = ClaimWorld ? ClaimWorld->GetSubsystem<UCoverReservationSubsystem>() : nullptr;
				if (IsValid(ClaimResSub) && ClaimResSub->IsCoverIntendedByOther(Mem->CoverHandle, Controller))
				{
					// Tie-break the symmetric intent race: both racers see "intended by other", so
					// without this BOTH abort and the cover goes unused. Keep moving only when
					// strictly closer than EVERY rival intender; ties and farther agents yield.
					bStolen = false;
					TArray<TPair<APawn*, FCover>> IntentOwners;
					ClaimResSub->GetIntendedCoverOwners(IntentOwners, Controller);
					for (const TPair<APawn*, FCover>& Entry : IntentOwners)
					{
						if (Entry.Value.Handle != Mem->CoverHandle) continue;
						const APawn* RivalPawn = Entry.Key;
						if (!IsValid(RivalPawn)) { bStolen = true; break; } // unknown rival — yield
						const FVector CoverLoc = Entry.Value.Data.Location;
						if (FVector::Dist2D(PawnLoc, CoverLoc) >= FVector::Dist2D(RivalPawn->GetActorLocation(), CoverLoc))
						{
							bStolen = true;
							break;
						}
					}
				}
			}
			if (bStolen)
			{
				UE_LOG(LogEnemyAI, Log, TEXT("[COVER-AICS] %s destination claimed by another agent mid-move — abandoning"),
					*Pawn->GetName());
				Controller->StopMovement();
				StopAdvanceFire(OwnerComp, Mem, false);
				HandleFailure(OwnerComp, Mem, BB, Controller);
				return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			}
		}

		// --- En-route hostile re-validation (same throttle) ---
		// The pick-time anchor reject sampled hostile positions once; a hostile who moves onto the
		// destination during a multi-second run must invalidate it. Same radii as pick time, so a
		// cover legal at pick only fails if a hostile actually closed in.
		float HostileCoverDist = 0.f;
		float HostilePawnDist = 0.f;
		if (IsValid(DA))
		{
			HostileCoverDist = DA->MinHostileCoverDistance;
			HostilePawnDist = DA->MinHostilePawnDistance;
		}
		else if (const ACompanionAIController* HostileCheckCompCtrl = Cast<ACompanionAIController>(Controller))
		{
			if (const UCompanionTuningDataAsset* HostileCheckTuning = HostileCheckCompCtrl->GetTuning())
			{
				HostileCoverDist = HostileCheckTuning->MinHostileCoverDistance;
				HostilePawnDist = HostileCheckTuning->MinHostilePawnDistance;
			}
		}

		if (HostileCoverDist > 0.f || HostilePawnDist > 0.f)
		{
			FHostileAnchors HostileAnchors;
			// Enemy movers: perception-honest anchors — an observable abort must not react to a
			// hostile the enemy can't know about. Companion movers stay omniscient (AI-vs-AI only;
			// never keys off an unseen player).
			if (IsValid(Enemy))
				UCoverScoringStatics::GatherKnownHostileAnchors(ClaimWorld, Enemy, Controller, HostileAnchors);
			else
				UCoverScoringStatics::GatherHostileAnchors(ClaimWorld, Pawn, Controller, HostileAnchors);
			if (UCoverScoringStatics::IsNearHostileAnchor(Mem->CachedCoverData.Location, HostileAnchors,
				HostileCoverDist, HostilePawnDist))
			{
				UE_LOG(LogEnemyAI, Log, TEXT("[COVER-AICS] %s destination compromised by a hostile mid-move — abandoning"),
					*Pawn->GetName());
				Controller->StopMovement();
				StopAdvanceFire(OwnerComp, Mem, false);
				HandleFailure(OwnerComp, Mem, BB, Controller);
				return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			}
		}
	}

	// --- Stall detection ---
	bool bStalled = false;
	if (!bArrived && IsValid(DA))
	{
		if (Dist + DA->CoverMoveStallProgressEpsilon < Mem->StallBestDist)
		{
			Mem->StallBestDist = Dist;
			Mem->StallAccum = 0.f;
		}
		else
		{
			Mem->StallAccum += DeltaSeconds;
			bStalled = (Mem->StallAccum >= DA->CoverMoveStallTimeout);
		}
	}

	// --- Advance-fire tick (throttled to ~10 Hz) ---
	Mem->FireTickAccum += DeltaSeconds;

	if (bFireWhileAdvancing && IsValid(Enemy) && IsValid(DA) && DA->bFireWhileAdvancing &&
		!bArrived && Mem->FireTickAccum >= FireTickInterval)
	{
		const float FireDelta = Mem->FireTickAccum;
		Mem->FireTickAccum = 0.f;

		AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

		if (!IsValid(Target))
		{
			if (Mem->bFiring)
			{
				AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
				if (IsValid(Weapon)) Weapon->StopFiring();
				Mem->bFiring = false;
			}
			Mem->FirePhase = EMoveShootFirePhase::Acquire;
		}
		else
		{
			const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);

			USuppressionComponent* SupprComp = Enemy->GetSuppressionComponent();
			const bool bSuppressed = IsValid(SupprComp) && SupprComp->IsSuppressed();

			if (bSuppressed && Mem->bFiring)
			{
				AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
				if (IsValid(Weapon)) Weapon->StopFiring();
				Mem->bFiring = false;
				Mem->FirePhase = EMoveShootFirePhase::Acquire;
			}
			else if (!bSuppressed)
			{
				switch (Mem->FirePhase)
				{
				case EMoveShootFirePhase::Acquire:
				{
					Mem->FireTimer -= FireDelta;
					if (Mem->FireTimer <= 0.f && bHasLOS)
					{
						AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
						if (IsValid(Weapon) && Weapon->CanFire())
						{
							Weapon->StartFiring();
							Mem->bFiring = true;
						}
						Mem->FirePhase = EMoveShootFirePhase::Firing;
						Mem->FireTimer = FMath::RandRange(DA->BurstDurationMin, DA->BurstDurationMax);
					}
					break;
				}
				case EMoveShootFirePhase::Firing:
				{
					if (!bHasLOS)
					{
						AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
						if (IsValid(Weapon)) Weapon->StopFiring();
						Mem->bFiring = false;
						Mem->FirePhase = EMoveShootFirePhase::Pause;
						Mem->FireTimer = FMath::RandRange(DA->AdvanceFireBurstPauseMin, DA->AdvanceFireBurstPauseMax);
						break;
					}
					Mem->FireTimer -= FireDelta;
					if (Mem->FireTimer <= 0.f)
					{
						AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
						if (IsValid(Weapon)) Weapon->StopFiring();
						Mem->bFiring = false;
						Mem->FirePhase = EMoveShootFirePhase::Pause;
						Mem->FireTimer = FMath::RandRange(DA->AdvanceFireBurstPauseMin, DA->AdvanceFireBurstPauseMax);
					}
					break;
				}
				case EMoveShootFirePhase::Pause:
				{
					Mem->FireTimer -= FireDelta;
					if (Mem->FireTimer <= 0.f)
					{
						Mem->FirePhase = EMoveShootFirePhase::Acquire;
						Mem->FireTimer = 0.f;
					}
					break;
				}
				}
			}
		}
	}

	// Companion approach-fire: muzzle-gated fire at the visible combat target while walking to
	// the committed point — the silent run-to-cover reads as broken when enemies have eyes on it.
	// Shared helper (BTTask_CompanionCombat's final-approach walk runs the same loop); it throttles
	// itself to the old 10 Hz cadence and latches focus per target. StopAdvanceFire tears it down.
	if (!IsValid(Enemy) && !bArrived)
	{
		if (ACompanionCharacter* FireCompanion = Cast<ACompanionCharacter>(Pawn))
			CompanionCover::TickCoverApproachFire(FireCompanion, Controller, BB, Mem->ApproachFire, DeltaSeconds);
	}

	// Wait while still moving and not stalled
	if (!bArrived && Status != EPathFollowingStatus::Idle && !bStalled) return;

	if (bArrived)
	{
		// Read fresh cover data for arrival handling; fall back to cached snapshot if the live read fails
		FCoverData Data;
		ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(Pawn->GetWorld());
		const bool bGotFresh = CoverSys && CoverSys->GetCoverData(Mem->CoverHandle, Data);
		if (!bGotFresh) Data = Mem->CachedCoverData;

		// Claim guard: if someone else holds this cover, we lost the race (our OccupyCover failed
		// silently) — fail so the BT re-picks instead of posing into an occupied cover.
		if (CoverSys)
		{
			AController* Occupant = CoverSys->GetOccupyingController(Mem->CoverHandle);
			if (Occupant && Occupant != Controller)
			{
				UE_LOG(LogEnemyAI, Log, TEXT("[COVER-AICS] %s arrived at cover occupied by %s — re-picking"),
					*Pawn->GetName(), *GetNameSafe(Occupant->GetPawn()));
				StopAdvanceFire(OwnerComp, Mem, false);
				HandleFailure(OwnerComp, Mem, BB, Controller);
				return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			}
		}

		// Arrival re-validation (enemy only — the companion's commit gates all ran at pick time).
		// The EQS pick can be seconds stale by the time the pawn is standing here, and nothing in
		// between re-checks that the wall is still between it and the threat. Posing in-cover anyway
		// IS the "hunkered in the open behind nothing" bug: the fire task's compromise loop only
		// catches it after MinDwell + EvalInterval + two consecutive evals — ~1.8s stood exposed.
		// Re-pick instead. Skipped under enemy.ForceCover, which pins cover for anim inspection.
		if (AEnemyCharacter* ArriveEnemy = Cast<AEnemyCharacter>(Pawn))
		{
			const UEnemyArchetypeData* ArriveDA = ArriveEnemy->GetArchetypeData();
			AActor* ArriveThreat = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
			if (IsValid(ArriveDA) && ArriveDA->bRelocateRequiresBodyProtection
				&& IsValid(ArriveThreat) && GetForceCoverLevel() == 0)
			{
				const AEnemyAIController* ArriveAIC = Cast<AEnemyAIController>(Controller);
				const UEnemyAwarenessComponent* ArriveAwareness = ArriveAIC ? ArriveAIC->GetAwarenessComponent() : nullptr;
				bool bArriveSighted = true;
				const FVector ArriveThreatLoc = UCoverScoringStatics::GetPerceivedThreatLocation(
					ArriveThreat, ArriveAwareness, bArriveSighted);

				const UCapsuleComponent* ArriveCap = ArriveEnemy->GetCapsuleComponent();
				const float ArriveStandoff =
					(ArriveCap ? ArriveCap->GetScaledCapsuleRadius() : MoveToCoverDefaultCapsuleRadius)
					+ ArriveDA->CoverStandoffPadding;

				if (!UCoverGeometryStatics::IsThreatCovered(Pawn->GetWorld(), Data, ArriveThreatLoc,
					ArriveStandoff, MoveToCoverBodyProtectChestHeight, ArriveThreat, Pawn))
				{
					UE_LOG(LogEnemyAI, Log, TEXT("[COVER-AICS] %s arrived at cover that no longer shields vs %s — re-picking"),
						*Pawn->GetName(), *GetNameSafe(ArriveThreat));
					StopAdvanceFire(OwnerComp, Mem, false);
					HandleFailure(OwnerComp, Mem, BB, Controller);
					return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
				}
			}
		}

		StopAdvanceFire(OwnerComp, Mem, true);
		HandleArrival(OwnerComp, Mem, BB, Pawn, Data);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	// Path failed or stalled
	if (bStalled)
	{
		UE_LOG(LogEnemyAI, Log, TEXT("[COVER-AICS] %s stalled advancing to cover (dist=%.0f) — abandoning"),
			*Pawn->GetName(), Dist);
	}

	StopAdvanceFire(OwnerComp, Mem, false);
	HandleFailure(OwnerComp, Mem, BB, Controller);
	return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
}

EBTNodeResult::Type UBTTask_MoveToCoverPoint::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FMoveToCoverPointMemory* Mem = CastInstanceNodeMemory<FMoveToCoverPointMemory>(NodeMemory);

	AAIController* Controller = OwnerComp.GetAIOwner();
	if (Controller) Controller->StopMovement();

	StopAdvanceFire(OwnerComp, Mem, false);

	// Clear any pose this task latched (covers the abort that lands between arrival and the
	// fire task starting — otherwise the pose leaks into whatever branch runs next).
	if (APawn* AbortPawn = Controller ? Controller->GetPawn() : nullptr)
	{
		if (UCoverPoseComponent* PoseComp = AbortPawn->FindComponentByClass<UCoverPoseComponent>())
			if (Cast<AEnemyCharacter>(AbortPawn)) PoseComp->ResetCoverPose();
	}

	// Set HasCover false on abort (mirror old task's ReleaseClaim semantics)
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (BB && HasCoverKey.SelectedKeyName != NAME_None)
		BB->SetValueAsBool(HasCoverKey.SelectedKeyName, false);

	// Clear intended cover via OwnerComp world (pawn may be dead)
	UWorld* World = OwnerComp.GetWorld();
	UCoverReservationSubsystem* ResSub = World ? World->GetSubsystem<UCoverReservationSubsystem>() : nullptr;
	if (IsValid(ResSub) && IsValid(Controller))
		ResSub->ClearIntendedCover(Controller);

	return EBTNodeResult::Aborted;
}

void UBTTask_MoveToCoverPoint::StopAdvanceFire(UBehaviorTreeComponent& OwnerComp,
	FMoveToCoverPointMemory* Mem, bool bKeepFocus) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;

	// Companion approach-fire teardown (the enemy path below early-returns on a companion pawn).
	if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn))
	{
		CompanionCover::StopCoverApproachFire(Companion, Controller, Mem->ApproachFire, bKeepFocus);
		return;
	}

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);

	// If pawn is gone, try the cached weak-ptr to reset spread (accept truly-dead case)
	if (!IsValid(Enemy)) Enemy = Mem->CachedEnemy.Get();
	if (!IsValid(Enemy)) return;

	if (Mem->bFiring)
	{
		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon)) Weapon->StopFiring();
		Mem->bFiring = false;
	}

	Enemy->SetAimTarget(nullptr);
	Enemy->SetExtraSpreadDegrees(0.f);

	if (!bKeepFocus && Controller)
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

void UBTTask_MoveToCoverPoint::HandleArrival(UBehaviorTreeComponent& OwnerComp,
	FMoveToCoverPointMemory* Mem, UBlackboardComponent* BB, APawn* Pawn, const FCoverData& Data) const
{
	// Set HasCover BB key if bound
	if (HasCoverKey.SelectedKeyName != NAME_None)
		BB->SetValueAsBool(HasCoverKey.SelectedKeyName, true);

	// Crouch if this is a crouch-height cover
	const ECoverHeight DerivedHeight = UCoverGeometryStatics::GetCoverHeight(Data);
	if (DerivedHeight == ECoverHeight::Crouch)
	{
		if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
	}

	// Write cover pose component — prefer typed accessor, FindComponentByClass as last resort
	UCoverPoseComponent* PoseComp = nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (Enemy)
		PoseComp = Enemy->GetCoverPoseComponent();
	else if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn))
		PoseComp = Companion->GetCoverPoseComponent();
	else
		PoseComp = Pawn->FindComponentByClass<UCoverPoseComponent>();

	AAIController* Controller = OwnerComp.GetAIOwner();

	if (IsValid(PoseComp))
	{
		// Enemy: lean side first so the anim rise-edge picks the wall-correct peek in one play.
		if (Enemy)
		{
			if (const AActor* Threat = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget)))
				PoseComp->SetLean(UCoverGeometryStatics::ResolveLeanSide(
					Data, DerivedHeight == ECoverHeight::Crouch, Threat->GetActorLocation()));
		}
		PoseComp->SetInCover(true, DerivedHeight);
	}

	if (GetCoverAnimLogLevel() > 0 && Enemy)
		UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s ARRIVE dist=%.0f coverLoc=(%.0f,%.0f,%.0f)"),
			*GetNameSafe(Pawn),
			FVector::Dist2D(Pawn->GetActorLocation(), Mem->ArrivalPos),
			Data.Location.X, Data.Location.Y, Data.Location.Z);

	// Enemy: face back-to-cover (yaw = outward fire direction, companion SlotYawRot convention)
	// and clear focus so nothing fights the peek montages' root-motion rotation. Target focus
	// resumes wherever the pose resets. (The companion manages its own slot yaw in its task.)
	if (Enemy && IsValid(Controller))
	{
		const FRotator WallYaw(0.f, UCoverGeometryStatics::GetFireArcForward(Data).Rotation().Yaw, 0.f);
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
		Pawn->SetActorRotation(WallYaw);
		Controller->SetControlRotation(WallYaw);
	}

	// Clear intended cover
	if (IsValid(Controller))
	{
		UWorld* World = OwnerComp.GetWorld();
		UCoverReservationSubsystem* ResSub = World ? World->GetSubsystem<UCoverReservationSubsystem>() : nullptr;
		if (IsValid(ResSub)) ResSub->ClearIntendedCover(Controller);
	}
}

void UBTTask_MoveToCoverPoint::HandleFailure(UBehaviorTreeComponent& OwnerComp,
	FMoveToCoverPointMemory* Mem, UBlackboardComponent* BB, AAIController* Controller) const
{
	if (Controller) Controller->StopMovement();

	// Set HasCover false on failure (mirror old task's ReleaseClaim semantics)
	if (BB && HasCoverKey.SelectedKeyName != NAME_None)
		BB->SetValueAsBool(HasCoverKey.SelectedKeyName, false);

	// Mark vacated in reservation subsystem (use OwnerComp world so it works even if pawn is dead)
	UWorld* World = OwnerComp.GetWorld();
	UCoverReservationSubsystem* ResSub = World ? World->GetSubsystem<UCoverReservationSubsystem>() : nullptr;
	if (IsValid(ResSub) && IsValid(Controller))
	{
		ResSub->MarkVacated(Mem->CoverHandle, Controller);
		ResSub->ClearIntendedCover(Controller);
	}

	// Clear cover target BB key
	if (BB && CoverTargetKey.SelectedKeyName != NAME_None)
	{
		BB->ClearValue(CoverTargetKey.SelectedKeyName);
	}
}

FCover UBTTask_MoveToCoverPoint::RerankCoverForMultiThreat(UBehaviorTreeComponent& OwnerComp, AAIController* Controller,
	APawn* Pawn, const UCompanionTuningDataAsset& Tuning, const FCover& ChosenCover) const
{
	UWorld* World = OwnerComp.GetWorld();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!World || !BB || !Tuning.bCoverRequiresBodyProtection) return ChosenCover;

	AActor* FocusTarget = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget));
	if (!IsValid(FocusTarget)) return ChosenCover;

	const float Penalty = FMath::Clamp(Tuning.MultiThreatExposurePenalty, 0.f, 1.f);
	const int32 MaxExtra = FMath::Max(0, Tuning.MaxThreatsForCoverScoring - 1);
	if (MaxExtra <= 0 || Penalty >= 1.f) return ChosenCover;

	TArray<AActor*, TInlineAllocator<8>> ExtraThreats;
	CompanionCover::GatherExtraThreatActors(Controller, Pawn, FocusTarget, MaxExtra, ExtraThreats);
	if (ExtraThreats.Num() == 0) return ChosenCover;

	const ACharacter* PawnChar = Cast<ACharacter>(Pawn);
	const UCapsuleComponent* Cap = PawnChar ? PawnChar->GetCapsuleComponent() : nullptr;
	const float Standoff = (Cap ? Cap->GetScaledCapsuleRadius() : MoveToCoverDefaultCapsuleRadius) + 10.f;

	const int32 ChosenUncovered = CompanionCover::CountUncoveredThreats(World, ChosenCover.Data, Standoff,
		Tuning.CoverProtectionChestHeight, Pawn, ExtraThreats);
	if (ChosenUncovered == 0) return ChosenCover; // already shields every known threat

	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	if (!CoverSys) return ChosenCover;
	UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();
	AController* CoverController = Pawn->GetController();
	const FVector MyLoc = Pawn->GetActorLocation();
	const FVector ThreatLoc = FocusTarget->GetActorLocation();
	// Head-height LoS anchor for the peek test (centre-mass reads a standing shooter behind crouch
	// cover as blocked — shared resolver with the state service and aim).
	const FVector ThreatSightLoc = AITargeting::GetSightLocation(FocusTarget);

	// Hostile-adjacency reject (enemy-parity) — never re-rank onto a spot an enemy holds or heads to.
	FHostileAnchors HostileAnchors;
	const bool bRejectHostileAdjacent = Tuning.MinHostileCoverDistance > 0.f || Tuning.MinHostilePawnDistance > 0.f;
	if (bRejectHostileAdjacent)
		UCoverScoringStatics::GatherHostileAnchors(World, Pawn, CoverController, HostileAnchors);

	FCoverScoreParams Params = UCoverScoringStatics::GetParamsForQuerier(Pawn);
	Params.MaxSearchRadius = Tuning.CoverSearchRadius;

	// Penalised score mirrors the switch monitor's multi-threat formula exactly (sign-aware —
	// Combat-mode advance params flip the band term, so raw scores go negative).
	auto PenalizedScore = [&](const FCoverData& Data, int32 Uncovered)
	{
		return UCoverScoringStatics::ApplyScorePenalty(
			UCoverScoringStatics::ScoreCandidate(World, Data, MyLoc, ThreatLoc, /*bBodyProtected=*/false, Params),
			FMath::Pow(Penalty, static_cast<float>(Uncovered)));
	};

	TArray<FCover> Candidates;
	Candidates.Reserve(64);
	const FBoxSphereBounds Bounds(MyLoc, FVector(Tuning.CoverSearchRadius), Tuning.CoverSearchRadius);
	CoverSys->GetCoverDataWithinBounds(Bounds, Candidates);

	const float ArcCos = FMath::Cos(FMath::DegreesToRadians(Tuning.CoverFlankArcHalfAngleDeg));

	FCover Best = ChosenCover;
	float BestScore = PenalizedScore(ChosenCover.Data, ChosenUncovered);
	int32 BestUncovered = ChosenUncovered;

	// Same outer cap the commit gate enforced on the original pick — the box bounds query can hand
	// back corner candidates ~1.41x the radius out.
	const float OuterCapSq = FMath::Square(FMath::Max(Tuning.CoverCommitMaxDistance, Tuning.CoverSearchRadius));

	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid() || Candidate.Handle == ChosenCover.Handle) continue;
		if (FVector::DistSquared(MyLoc, Candidate.Data.Location) > OuterCapSq) continue;
		// Same-floor gate — the bounds query spans storeys (see the commit gate's off-level decline).
		if (Tuning.CoverPickMaxZDelta > 0.f
			&& FMath::Abs(MyLoc.Z - Candidate.Data.Location.Z) > Tuning.CoverPickMaxZDelta) continue;
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != CoverController) continue;
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, CoverController)) continue;
		if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, CoverController, Tuning.CoverSwitchPostVacateCooldown))
			continue;

		// Same validity gates the switch monitor applies: fire arc, hostile adjacency (cheap 2D,
		// before the traces), peekable LoS, focused-threat shield.
		const FVector ToTarget2D = (ThreatLoc - Candidate.Data.Location).GetSafeNormal2D();
		if (FVector::DotProduct(UCoverGeometryStatics::GetFireArcForward(Candidate.Data), ToTarget2D) < ArcCos) continue;
		if (bRejectHostileAdjacent && UCoverScoringStatics::IsNearHostileAnchor(Candidate.Data.Location,
			HostileAnchors, Tuning.MinHostileCoverDistance, Tuning.MinHostilePawnDistance)) continue;
		if (!UCoverGeometryStatics::CanPeekShoot(World, Candidate.Data,
			UCoverGeometryStatics::GetCoverHeight(Candidate.Data) == ECoverHeight::Crouch,
			ThreatSightLoc, 150.f, FocusTarget, Pawn)) continue;
		if (!UCoverGeometryStatics::IsThreatCovered(World, Candidate.Data, ThreatLoc, Standoff,
			Tuning.CoverProtectionChestHeight, FocusTarget, Pawn)) continue;

		const int32 Uncovered = CompanionCover::CountUncoveredThreats(World, Candidate.Data, Standoff,
			Tuning.CoverProtectionChestHeight, Pawn, ExtraThreats);
		if (Uncovered >= BestUncovered) continue; // override only for strictly better shielding

		const float Score = PenalizedScore(Candidate.Data, Uncovered);
		if (Score > BestScore)
		{
			Best = Candidate;
			BestScore = Score;
			BestUncovered = Uncovered;
		}
	}

	if (Best.Handle != ChosenCover.Handle)
		UE_LOG(LogCompanionAI, Log, TEXT("%s: first-pick re-rank — EQS cover exposed to %d extra threat(s), overriding to one exposed to %d"),
			*Pawn->GetName(), ChosenUncovered, BestUncovered);

	return Best;
}

FString UBTTask_MoveToCoverPoint::GetStaticDescription() const
{
	return FString::Printf(TEXT("Move to AICS cover point%s"),
		bFireWhileAdvancing ? TEXT(" (advance-fire)") : TEXT(""));
}
