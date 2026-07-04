// BT service — periodically re-evaluates whether the companion's current cover point
// is still the best available, and commits a switch by writing the CoverTarget BB key.
// P3 AICS migration: cover source changed from AAICoverSlot line-segment slots to FCoverHandle/FCoverData points.

#include "BTService_CoverSwitchMonitor.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "CoverSystem.h"
#include "CoverGeometryStatics.h"
#include "CoverReservationSubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionAIController.h"
#include "CompanionTuningDataAsset.h"
#include "CompanionCharacter.h"
#include "EnemyCharacter.h"
#include "EnemyArchetypeData.h"
#include "WeaponBase.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "GameplayTagAssetInterface.h"
#include "HealthComponent.h"
#include "ExtractionTypes.h"

namespace
{
	// --- Scoring weights (ported verbatim from CoverRegistrySubsystem::ScoreSlotFor) ---
	constexpr float Weight_Proximity  = 1.0f;
	constexpr float Weight_Distance   = 0.7f;
	constexpr float Weight_CoverBonus = 0.15f;
	constexpr float IdealDistMin      = 500.f;
	constexpr float IdealDistRange    = 700.f;
	constexpr float DefaultCapsuleRadius = 34.f;

	/** Shared scoring helper. Mirrors CoverRegistrySubsystem::ScoreSlotFor / the enemy's ScoreCoverCandidate. */
	float ScoreCoverCandidate(const FVector& QuerierLoc, const FVector& ThreatLoc, const FVector& CoverLoc, float MaxRadius)
	{
		const float DistToQuerier = FVector::Dist(QuerierLoc, CoverLoc);
		const float ProxScore     = FMath::Clamp(1.f - DistToQuerier / MaxRadius, 0.f, 1.f);
		const float DistToTarget  = FVector::Dist(CoverLoc, ThreatLoc);
		const float DistScore     = FMath::Clamp((DistToTarget - IdealDistMin) / IdealDistRange, 0.f, 1.f);
		return Weight_Proximity * ProxScore + Weight_Distance * DistScore + Weight_CoverBonus;
	}

	// Collects the companion's known EXTRA threats (sight-perceived, enemy-tagged, alive) as actors
	// — everything except the focused CombatTarget — sorted nearest-first and capped to MaxExtra.
	// Returns AActor* so callers can pass each threat into IsThreatCovered's IgnoreThreatActor param
	// (FVector locations would leave IgnoreThreatActor=nullptr, making the trace hit the threat's own
	// body and read as "covered" even when exposed).
	void GatherExtraThreatActors(AAIController* Controller, APawn* Pawn, AActor* FocusTarget,
		int32 MaxExtra, TArray<AActor*, TInlineAllocator<8>>& OutActors)
	{
		OutActors.Reset();
		if (MaxExtra <= 0 || !IsValid(Pawn)) return;

		UAIPerceptionComponent* Perception = Controller ? Controller->GetPerceptionComponent() : nullptr;
		if (!Perception) return;

		TArray<AActor*> Perceived;
		Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Perceived);

		// Perception can hand back null/pending-kill entries; the Sort lambda derefs by const-ref.
		Perceived.RemoveAll([](const AActor* A) { return !IsValid(A); });

		const FVector PawnLoc = Pawn->GetActorLocation();

		Perceived.Sort([PawnLoc](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared(PawnLoc, A.GetActorLocation()) < FVector::DistSquared(PawnLoc, B.GetActorLocation());
		});

		for (AActor* Actor : Perceived)
		{
			if (!IsValid(Actor) || Actor == FocusTarget) continue;

			const IGameplayTagAssetInterface* TagIface = Cast<IGameplayTagAssetInterface>(Actor);
			if (!TagIface) continue;
			FGameplayTagContainer Tags;
			TagIface->GetOwnedGameplayTags(Tags);
			if (!Tags.HasTag(TAG_Character_Enemy)) continue;

			const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
			if (Health && Health->IsDead()) continue;

			OutActors.Add(Actor);
			if (OutActors.Num() >= MaxExtra) break;
		}
	}
}

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

	// Dwell-from-arrival: only treat the cover point as occupied (start accruing dwell) once the pawn
	// is physically at its hunker position. BTTask_MoveToCoverPoint moves the pawn to
	// GetApproachPosition, so test against the equivalent hunker point — not the raw cover Location,
	// which would deadlock dwell for standoff-offset points.
	if (!Mem.bHasArrived)
	{
		const ACharacter* PawnChar = Cast<ACharacter>(Pawn);
		const UCapsuleComponent* Cap = PawnChar ? PawnChar->GetCapsuleComponent() : nullptr;
		const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : DefaultCapsuleRadius;
		const float Standoff = CapRadius + 10.f;
		const FVector PawnLoc = Pawn->GetActorLocation();
		// Enemies arrive edge-aligned (corner-snapped) — the arrival test must use the same position
		// or dwell never starts at endpoint covers. Companions keep the plain hunker (their combat
		// task doesn't edge-align).
		const AEnemyCharacter* MonEnemy = Cast<AEnemyCharacter>(Pawn);
		const UEnemyArchetypeData* MonDA = MonEnemy ? MonEnemy->GetArchetypeData() : nullptr;
		const FVector HunkerLoc = MonDA
			? UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
				Pawn->GetWorld(), CurrentCover.Data, Standoff, CapRadius, MonDA->CoverCornerGap, Pawn)
			: UCoverGeometryStatics::GetHunkerPosition(CurrentCover.Data, Standoff);
		const bool bArrivedNow = FVector::Dist2D(PawnLoc, HunkerLoc) <= ArrivalRadius;
		if (!bArrivedNow)
		{
			Mem.TimeSinceArrival = 0.f;
			return;
		}
		Mem.bHasArrived = true;
	}

	Mem.TimeSinceArrival += DeltaSeconds;
	Mem.TimeSinceReEval  += DeltaSeconds;

	if (Mem.TimeSinceArrival < Tuning->CoverSwitchMinDwell) return;
	if (Mem.TimeSinceReEval  < Tuning->CoverSwitchReEvalInterval) return;

	// Bail early on no combat target before paying for the bounds query.
	AActor* CombatTarget = Cast<AActor>(BB->GetValueAsObject(CombatTargetKey.SelectedKeyName));
	if (!IsValid(CombatTarget)) return;

	Mem.TimeSinceReEval = 0.f;

	// TODO: lift formation-point computation to a shared utility (spec §5.7 open question).
	// FollowPlayer uses a velocity-relative offset; the spec wants a fixed actor-facing offset.
	AActor* Player = Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player)) return;

	const FVector FormationPoint = Player->GetActorLocation()
		+ Player->GetActorRightVector()      * Tuning->FormationOffsetRight
		+ (-Player->GetActorForwardVector()) * Tuning->FormationOffsetBack;

	UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();
	const AController* CoverController = Pawn->GetController();
	const FVector ThreatLoc = CombatTarget->GetActorLocation();

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
	GatherExtraThreatActors(Controller, Pawn, CombatTarget, MaxExtraThreats, ExtraThreatActors);
	const float MultiThreatPenalty = FMath::Clamp(Tuning->MultiThreatExposurePenalty, 0.f, 1.f);
	const bool bScoreMultiThreat = ExtraThreatActors.Num() > 0 && MultiThreatPenalty < 1.f && Tuning->bCoverRequiresBodyProtection;

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

		// Cover must offer a position with LoS to the target.
		if (!UCoverGeometryStatics::CanPeekShoot(World, Candidate.Data,
			UCoverGeometryStatics::GetCoverHeight(Candidate.Data) == ECoverHeight::Crouch,
			ThreatLoc, 150.f, CombatTarget, Pawn))
			continue;

		// Body-protection hard reject: mirrors the old FindBestCoverFor bRequireBodyProtection gate.
		if (Tuning->bCoverRequiresBodyProtection)
		{
			if (!UCoverGeometryStatics::IsThreatCovered(World, Candidate.Data, ThreatLoc,
				ProtectionStandoff, Tuning->CoverProtectionChestHeight, CombatTarget, Pawn))
				continue;
		}

		const float DistSq = FVector::DistSquared(FormationPoint, Candidate.Data.Location);
		float Score = ScoreCoverCandidate(FormationPoint, ThreatLoc, Candidate.Data.Location, SearchRadius);

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
				Score *= FMath::Pow(MultiThreatPenalty, static_cast<float>(UncoveredExtra));
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

	if (!BestCover.IsValid())
	{
		// No better candidate this re-eval — reset the debounce so a future winner must agree fresh. (G2)
		Mem.PendingBestCover = FCoverHandle();
		Mem.ConsecutiveBetterCount = 0;
		return;
	}

	float CurrentScore = ScoreCoverCandidate(FormationPoint, ThreatLoc, CurrentCover.Data.Location, SearchRadius);

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
			CurrentScore *= FMath::Pow(MultiThreatPenalty, static_cast<float>(CurUncoveredExtra));
	}

	// Arc-violation penalty: if the focused target is outside the widened arc for the current cover,
	// penalise the score so a better-positioned candidate can win the margin comparison.
	{
		const FVector CurToTarget2D = (ThreatLoc - CurrentCover.Data.Location).GetSafeNormal2D();
		const FVector CurFireFwd    = UCoverGeometryStatics::GetFireArcForward(CurrentCover.Data);
		const float   CurArcDot     = FVector::DotProduct(CurFireFwd, CurToTarget2D);
		const float   CurWidenedArc = Tuning->CoverFlankArcHalfAngleDeg + Tuning->CoverCompromiseArcSlackDeg;
		if (CurArcDot < FMath::Cos(FMath::DegreesToRadians(CurWidenedArc)))
			CurrentScore *= MultiThreatPenalty;
	}

	if (BestScore < CurrentScore * Tuning->CoverSwitchScoreMargin) // P6
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
		TEXT("CoverSwitch: %s -> new cover (curScore=%.2f, bestScore=%.2f, margin=%.2fx)"),
		*GetNameSafe(Pawn), CurrentScore, BestScore, Tuning->CoverSwitchScoreMargin);
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
