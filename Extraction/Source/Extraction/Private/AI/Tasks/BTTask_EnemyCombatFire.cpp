// BTTask_EnemyCombatFire — latent peek-fire loop driven by a NodeMemory state machine.
// Bugs 1+2 fix: while AwarenessState==Combat, never return Failed to the Selector.
// Instead: pursue (no LOS/range) or re-seek cover (suppressed in the open).
// P2 AICS migration: cover source changed from AAICoverSlot to FCoverHandle/FCoverData.

#include "BTTask_EnemyCombatFire.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyCharacter.h"
#include "EnemyGrenadierComponent.h"
#include "SuppressionComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "CoverSystem.h"
#include "CoverGeometryStatics.h"
#include "CoverScoringStatics.h"
#include "CoverReservationSubsystem.h"
#include "CoverPoseComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EnemyMoraleComponent.h"
#include "EnemyPostureComponent.h"
#include "Squad/EnemySquad.h"
#include "EnemyDebug.h"
#include "EnemyAnimInstance.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"

// ExposePhaseDuration / RecoverPhaseDuration promoted to DA fields (ExposePhaseMin/Max, RecoverPhaseMin/Max).
static constexpr float DefaultCapsuleRadius = 34.f;
static constexpr float SeekCoverArrivalTickRadius = 30.f;   // mirrors BTTask_MoveToCoverPoint
static constexpr float SeekCoverArrivalIdleRadius = 45.f;   // small margin over accept radius
/** Arrival radius used when checking whether the enemy has reached their cover point (Part B dwell). */
static constexpr float FlankSlotArrivalRadius = 120.f;
/** Consecutive compromise-positive evaluations required before triggering a relocate (debounce). */
static constexpr int32 CompromiseDebounceRequired = 2;
/** Chest height (cm) for the body-protection trace from a candidate's behind-cover position. */
static constexpr float BodyProtectChestHeight = 60.f;
/** Perpendicular-offset cap (cm) for same-wall candidate gates: IsSameWall is direction-only
 *  (dot >= 0.94), so a nearby parallel wall sharing the same facing would otherwise pass.
 *  Shared by FindShuffleCover and FindSidePeekCover. */
static constexpr float SameWallMaxPerpOffset = 150.f;

// --- Hunkered timing scalars (Broken morale) ---
/** Multiplier on ExposePhaseDuration when hunkered — shorter peeks. */
static constexpr float HunkerExposeScale = 0.7f;
/** Multiplier on burst duration (BurstDurationMin/Max) when hunkered — shorter bursts. */
static constexpr float HunkerBurstScale = 0.7f;
/** Multiplier on pause duration (BurstPauseMin/Max) when hunkered — longer pauses between peeks. */
static constexpr float HunkerPauseScale = 1.5f;

// --- Debug: enemy.ForceCoverHeight ---
/** Returns the forced cover height (0=auto, 1=crouch-only, 2=stand-only). Cached cvar pointer. */
static int32 GetForceCoverHeightLocal()
{
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("enemy.ForceCoverHeight"));
	return CVar ? CVar->GetInt() : 0;
}
/** True when the candidate should be skipped due to forced height.
 *  Uses the derived height (GetCoverHeight), not the raw bCrouchedCover flag. */
static bool ShouldSkipForForcedHeight(const FCoverData& Data, int32 ForcedHeight)
{
	if (ForcedHeight == 0) return false;
	const ECoverHeight H = UCoverGeometryStatics::GetCoverHeight(Data);
	if (ForcedHeight == 1 && H != ECoverHeight::Crouch) return true;
	if (ForcedHeight == 2 && H != ECoverHeight::Stand) return true;
	return false;
}

// --- Cover facing / peek-gap selection ---
// ChooseGapPeekSide / TryOppositeEndpointSide moved to UCoverGeometryStatics (shared with companion).
/** Reseek cadence when enemy.ForceCover is on. */
static constexpr float ForceCoverReseekCooldown = 0.5f;
// MinPeekCyclesBeforeRelocate promoted to DA field.
/** 2D drift from the hunker spot that triggers a corrective step-back before the next peek
 *  (mops up root-motion residue from peeks whose Return doesn't land exactly home). */
static constexpr float CoverDriftCorrectDist = 30.f;

/** 2D distance from the BB cover beyond which BB_HasCover is a lie and gets cleared. Must exceed
 *  every legitimate in-cover offset: the edge-align corner march (300) + corner gap (≤100) +
 *  capsule (~34) + peek step-out margin — endpoint covers park the pawn that far from the baked
 *  point while genuinely in cover. */
static constexpr float StaleCoverAbandonDist = 500.f;

/** Roll a pause duration with the canonical composition: base RandRange -> Hunker scalar -> feint multiplier. */
static float RollPauseDuration(const UEnemyArchetypeData* DA, bool bHunkered)
{
	const float PauseScale = bHunkered ? HunkerPauseScale : 1.f;
	float Duration = FMath::RandRange(DA->BurstPauseMin * PauseScale, DA->BurstPauseMax * PauseScale);
	if (DA->LongHideChance > 0.f && FMath::FRand() < DA->LongHideChance)
		Duration *= DA->LongHideMultiplier;
	return Duration;
}

/** [COVERSTATE] tattletale: logs every AI-issued move with the current pose flag — a move
 *  logged with posed=1 is a montage-slide caught red-handed. */
static void LogCoverMove(const TCHAR* Reason, const APawn* Pawn, const AEnemyCharacter* Enemy)
{
	if (GetCoverAnimLogLevel() == 0 || !IsValid(Enemy)) return;
	const UCoverPoseComponent* Pose = Enemy->GetCoverPoseComponent();
	UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s MOVE(%s) posed=%d"),
		*GetNameSafe(Pawn), Reason, (IsValid(Pose) && Pose->bInCover) ? 1 : 0);
}

/** Faces the enemy back-to-cover: yaw = the cover's outward fire direction (away from the wall,
 *  same convention as the companion's SlotYawRot). Focus is cleared so nothing fights the peek
 *  montages' root-motion rotation; target focus resumes wherever the pose resets. */
static void ApplyCoverFacing(AAIController* Controller, APawn* Pawn, const FCoverData& Data)
{
	if (!Controller || !IsValid(Pawn)) return;
	const FRotator WallYaw(0.f, UCoverGeometryStatics::GetFireArcForward(Data).Rotation().Yaw, 0.f);
	// Clear BOTH Gameplay and Move priorities — PathFollowingComponent::UpdateMoveFocus sets a
	// Move-priority focal point along the path every tick; leaving it active lets
	// UpdateControlRotation fight the wall yaw after the Gameplay slot is empty.
	Controller->ClearFocus(EAIFocusPriority::Gameplay);
	Controller->ClearFocus(EAIFocusPriority::Move);
	Pawn->SetActorRotation(WallYaw);
	Controller->SetControlRotation(WallYaw);
}

/** Focus invariant: while posed in cover the back-to-cover yaw owns the body — actively CLEAR
 *  any focus left behind by higher-priority branches (suppress/flank set focus and abort without
 *  clearing; with controller-yaw pawns it spins the pose toward the player). Otherwise face the
 *  target (or clear when none). */
static void UpdateCombatFocus(AAIController* Controller, const AEnemyCharacter* Enemy, AActor* Target)
{
	if (!Controller || !IsValid(Enemy)) return;
	const UCoverPoseComponent* Pose = Enemy->GetCoverPoseComponent();
	if (IsValid(Pose) && Pose->bInCover)
	{
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
		Controller->ClearFocus(EAIFocusPriority::Move);
		return;
	}
	if (IsValid(Target)) Controller->SetFocus(Target);
	else Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

/** Honest knowledge: the threat position this enemy is entitled to act on for cover decisions —
 *  live while sighted, frozen at LastKnownLocation once LOS is lost. Cover picks, compromise
 *  checks, and peek-side choices all route through this so a hidden player can genuinely flank. */
static FVector GetPerceivedThreatLoc(const AController* Controller, const AActor* Target)
{
	const AEnemyAIController* EnemyController = Cast<AEnemyAIController>(Controller);
	const UEnemyAwarenessComponent* Awareness = EnemyController ? EnemyController->GetAwarenessComponent() : nullptr;
	bool bSighted = true;
	return UCoverScoringStatics::GetPerceivedThreatLocation(Target, Awareness, bSighted);
}

/** True while the grenadier component is winding up a throw (non-grenadiers return false). */
static bool IsGrenadeTelegraphing(const AEnemyCharacter* Enemy)
{
	const UEnemyGrenadierComponent* GrenComp = IsValid(Enemy) ? Enemy->GetGrenadierComponent() : nullptr;
	return IsValid(GrenComp) && GrenComp->IsTelegraphing();
}

/** Returns true when the current cover point no longer protects against Target.
 *  Arc test uses GetFireArcForward (= DirectionToWall, toward the covered side) and the DA's CoverFlankArcHalfAngleDeg,
 *  widened by ArcSlackDeg so a recently-selected point doesn't immediately re-trigger.
 *  Body-shield test uses IsThreatCovered from the stable hunkered-position (same as the
 *  candidate picker), so the signal doesn't oscillate as the peek loop ducks and pops up. */
static bool IsCoverCompromised(UWorld* World, const FCoverData& CoverData,
	const FVector& PawnLoc, const AActor* Target, const FVector& PerceivedThreatLoc,
	float ArcHalfAngleDeg, float ArcSlackDeg,
	float Standoff, APawn* Pawn,
	bool* OutOutsideArc = nullptr, bool* OutBodyExposed = nullptr, float* OutAngleDeg = nullptr)
{
	if (!IsValid(Target) || !World) return false;

	const FVector TargetLoc = PerceivedThreatLoc;

	// Arc test: GetFireArcForward is DirectionToWall (toward the covered side). Dot against to-target direction.
	const FVector ToTarget = (TargetLoc - CoverData.Location).GetSafeNormal2D();
	const FVector FireFwd = UCoverGeometryStatics::GetFireArcForward(CoverData);
	const float Dot = FVector::DotProduct(FireFwd, ToTarget);
	const float WidenedHalfArcDeg = ArcHalfAngleDeg + ArcSlackDeg;
	const bool bOutsideArc = Dot < FMath::Cos(FMath::DegreesToRadians(WidenedHalfArcDeg));
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));

	// Body-shield test: probe from the STABLE hunkered position rather than the pawn's live
	// chest, which oscillates during the peek loop and causes flickering verdicts.
	const bool bBodyProtected = UCoverGeometryStatics::IsThreatCovered(
		World, CoverData, TargetLoc, Standoff, BodyProtectChestHeight, Target, Pawn);
	const bool bBodyExposed = !bBodyProtected;

	if (OutOutsideArc)  *OutOutsideArc  = bOutsideArc;
	if (OutBodyExposed) *OutBodyExposed  = bBodyExposed;
	if (OutAngleDeg)    *OutAngleDeg     = AngleDeg;

	return bOutsideArc || bBodyExposed;
}

// Candidate scoring lives in UCoverScoringStatics::ScoreCandidate — shared with the EQS test.

struct FScoredCover
{
	float Score = 0.f;
	float DistSq = 0.f;
	FCover Cover;
};

/** Final pick over scored candidates. Best score wins (nearly-equal ties break nearer). When the
 *  DA enables path exposure, the top-N candidates additionally pay a penalty for how much of the
 *  approach route the threat can see — an enemy stops sprinting through the player's line of fire
 *  to reach "better" cover. Trace cost is hard-capped by PathExposureMaxTracesPerSelection. */
static FCover PickBestScoredCover(UWorld* World, TArray<FScoredCover>& Scored,
	const FVector& PawnLoc, const FVector& ThreatLoc, const AActor* Target, const APawn* Pawn,
	const UEnemyArchetypeData* DA)
{
	if (Scored.IsEmpty()) return FCover();

	Scored.Sort([](const FScoredCover& A, const FScoredCover& B)
	{
		if (!FMath::IsNearlyEqual(A.Score, B.Score)) return A.Score > B.Score;
		return A.DistSq < B.DistSq;
	});

	if (DA->PathExposureWeight <= 0.f) return Scored[0].Cover;

	int32 TraceBudget = DA->PathExposureMaxTracesPerSelection;
	const int32 TopN = FMath::Max(1, TraceBudget / FMath::Max(1, DA->PathExposureSampleCount));
	const int32 EvalCount = FMath::Min(TopN, Scored.Num());

	int32 BestIdx = 0;
	float BestAdjusted = -FLT_MAX;
	for (int32 i = 0; i < EvalCount; ++i)
	{
		const float Exposure = UCoverScoringStatics::ScorePathExposure(World, PawnLoc,
			Scored[i].Cover.Data.Location, ThreatLoc, DA->PathExposureSampleCount,
			Target, Pawn, TraceBudget);
		const float Adjusted = Scored[i].Score - DA->PathExposureWeight * Exposure;
		if (Adjusted > BestAdjusted)
		{
			BestAdjusted = Adjusted;
			BestIdx = i;
		}
	}
	return Scored[BestIdx].Cover;
}

/** Enemy-only protective relocate pick. Iterates AICS covers in radius; keeps those in-arc +
 *  able to peek-shoot (CanPeekShoot) and — when DA->bRelocateRequiresBodyProtection — whose
 *  hunkered body is geometry-shielded; scores via distance/alignment + protective bonus, nearer-
 *  is-better tiebreak. Returns an invalid FCover when none qualify. */
static FCover FindProtectiveCover(UWorld* World, const APawn* Pawn, AActor* Target,
	const UEnemyArchetypeData* DA, float Standoff, const FCoverHandle& CurrentHandle,
	AController* Controller)
{
	if (!World || !IsValid(Pawn) || !IsValid(Target) || !IsValid(DA))
		return FCover();

	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	if (!CoverSys) return FCover();

	UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();

	const FVector PawnLoc   = Pawn->GetActorLocation();
	const FVector ThreatLoc = GetPerceivedThreatLoc(Controller, Target);

	TArray<FCover> Candidates;
	Candidates.Reserve(64);
	const FBoxSphereBounds SearchBounds(PawnLoc, FVector(DA->CoverSearchRadius), DA->CoverSearchRadius);
	CoverSys->GetCoverDataWithinBounds(SearchBounds, Candidates);

	const int32 ForcedHeight = GetForceCoverHeightLocal();
	const float FlankArcCos = FMath::Cos(FMath::DegreesToRadians(DA->CoverFlankArcHalfAngleDeg));
	const AEnemyCharacter* ScoringEnemy = Cast<const AEnemyCharacter>(Pawn);
	const FCoverScoreParams ScoreParams = UCoverScoringStatics::MakeParamsForEnemy(ScoringEnemy);

	// Multi-threat: extra sighted hostiles beyond Target (empty when the archetype disables it).
	TArray<FEnemyKnownThreat> ExtraThreats;
	UCoverScoringStatics::GatherEnemyExtraThreats(ScoringEnemy, Target, ExtraThreats);

	// Hostile anchors for the claim-collision reject (same rule as the EQS CoverIntent filter).
	FHostileAnchors HostileAnchors;
	if (DA->MinHostileCoverDistance > 0.f || DA->MinHostilePawnDistance > 0.f)
		UCoverScoringStatics::GatherHostileAnchors(World, Pawn, Controller, HostileAnchors);

	TArray<FScoredCover> Scored;
	Scored.Reserve(Candidates.Num());

	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid()) continue;
		// Skip current cover
		if (Candidate.Handle == CurrentHandle) continue;
		// Skip occupied covers (single lookup — treat occupied-by-self as available)
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Controller) continue;
		// Skip covers intended by another agent (claim race guard)
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, Controller))
			continue;
		// Skip covers on post-vacate cooldown
		if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, Controller, DA->CoverRelocateCooldown))
			continue;
		// Skip covers next to a hostile or a hostile's declared destination (claim collision)
		if (UCoverScoringStatics::IsNearHostileAnchor(Candidate.Data.Location, HostileAnchors,
			DA->MinHostileCoverDistance, DA->MinHostilePawnDistance))
			continue;

		const FCoverData& Data = Candidate.Data;

		// Debug: forced cover height filter
		if (ShouldSkipForForcedHeight(Data, ForcedHeight)) continue;

		// Arc test: same as compromise check — must be in fire arc
		const FVector ToThreat = (ThreatLoc - Data.Location).GetSafeNormal2D();
		const FVector FireFwd = UCoverGeometryStatics::GetFireArcForward(Data);
		const float ArcDot = FVector::DotProduct(FireFwd, ToThreat);
		if (ArcDot < FlankArcCos)
			continue;

		// Must be able to peek-shoot
		const bool bCrouched = UCoverGeometryStatics::GetCoverHeight(Data) == ECoverHeight::Crouch;
		if (!UCoverGeometryStatics::CanPeekShoot(World, Data, bCrouched, ThreatLoc,
			BodyProtectChestHeight, Target, Pawn))
			continue;

		// Body protection check
		const bool bBodyProtected = UCoverGeometryStatics::IsThreatCovered(
			World, Data, ThreatLoc, Standoff, BodyProtectChestHeight, Target, Pawn);
		if (DA->bRelocateRequiresBodyProtection && !bBodyProtected) continue;

		// Score: shared formula (proximity + target-distance + flat bonus + protective bonus)
		const float DistSq = FVector::DistSquared(PawnLoc, Data.Location);
		float Score = UCoverScoringStatics::ScoreCandidate(
			World, Data, PawnLoc, ThreatLoc, bBodyProtected, ScoreParams);
		Score = UCoverScoringStatics::ApplyMultiThreatPenalty(Score, World, Data,
			Standoff, BodyProtectChestHeight, Pawn, ExtraThreats, DA->MultiThreatExposurePenalty);

		Scored.Add({ Score, DistSq, Candidate });
	}
	return PickBestScoredCover(World, Scored, PawnLoc, ThreatLoc, Target, Pawn, DA);
}

/** Picks a nav-projected lateral point near PawnLoc with LOS to Target. When RetreatBias > 0,
 *  prefers points that increase 2D distance to the threat (best-retreat sample wins, even if it
 *  doesn't fully clear the bias). RetreatBias <= 0 keeps legacy first-valid-sample behaviour.
 *  Returns true and sets OutPoint on success. */
static bool TryOpenGroundStrafe(APawn* Pawn, const AActor* Target, float Radius, FVector& OutPoint,
	float RetreatBias = 0.f)
{
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Pawn->GetWorld());
	UWorld* World = Pawn->GetWorld();
	if (!NavSys || !World) return false;

	const FVector TargetLoc = Target->GetActorLocation();
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector NavExtent(Radius * 0.5f, Radius * 0.5f, 100.f);

	FCollisionQueryParams LOSParams(SCENE_QUERY_STAT(EnemyOpenGroundStrafeLOS), false);
	LOSParams.AddIgnoredActor(Pawn);
	LOSParams.AddIgnoredActor(Target);

	constexpr int32 MaxAttempts = 5;
	constexpr float MinMoveDistSq = 60.f * 60.f;
	constexpr float EyeOffset = 150.f;

	const float CurDistToThreat = FVector::Dist2D(PawnLoc, TargetLoc);
	bool bHaveAny = false;
	FVector BestPoint = FVector::ZeroVector;
	float BestRetreatGain = -FLT_MAX;

	for (int32 i = 0; i < MaxAttempts; ++i)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * UE_PI);
		const float Dist  = FMath::FRandRange(Radius * 0.3f, Radius);
		const FVector Candidate = PawnLoc + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);

		FNavLocation NavLoc;
		if (!NavSys->ProjectPointToNavigation(Candidate, NavLoc, NavExtent)) continue;

		const FVector Point = NavLoc.Location;
		if (FVector::DistSquared2D(Point, PawnLoc) < MinMoveDistSq) continue;
		if (World->LineTraceTestByChannel(Point + FVector(0.f, 0.f, EyeOffset), TargetLoc, ECC_Visibility, LOSParams)) continue;

		if (RetreatBias <= 0.f)
		{
			OutPoint = Point;
			return true;
		}

		const float RetreatGain = FVector::Dist2D(Point, TargetLoc) - CurDistToThreat;
		if (!bHaveAny || RetreatGain > BestRetreatGain)
		{
			bHaveAny = true;
			BestRetreatGain = RetreatGain;
			BestPoint = Point;
		}
	}

	if (RetreatBias > 0.f && bHaveAny)
	{
		OutPoint = BestPoint;
		return true;
	}
	return false;
}

// --- Cover-move speed helpers (feature 6) ---

void UBTTask_EnemyCombatFire::ApplyCoverMoveSpeed(AEnemyCharacter* Enemy, FFireMemory* Mem,
	const UEnemyArchetypeData* DA)
{
	if (!IsValid(Enemy) || !Mem || !IsValid(DA)) return;
	if (Mem->bCoverMoveSpeedCapped) return;
	if (DA->CoverMoveSpeed <= 0.f) return;

	UCharacterMovementComponent* Move = Enemy->GetCharacterMovement();
	if (!IsValid(Move)) return;

	Mem->OriginalMaxWalkSpeed = Move->MaxWalkSpeed;
	Mem->OriginalMaxWalkSpeedCrouched = Move->MaxWalkSpeedCrouched;
	Move->MaxWalkSpeed = FMath::Min(Move->MaxWalkSpeed, DA->CoverMoveSpeed);
	Move->MaxWalkSpeedCrouched = FMath::Min(Move->MaxWalkSpeedCrouched, DA->CoverMoveSpeed);
	Mem->bCoverMoveSpeedCapped = true;
}

void UBTTask_EnemyCombatFire::RestoreCoverMoveSpeed(AEnemyCharacter* Enemy, FFireMemory* Mem)
{
	if (!IsValid(Enemy) || !Mem) return;
	if (!Mem->bCoverMoveSpeedCapped) return;

	UCharacterMovementComponent* Move = Enemy->GetCharacterMovement();
	if (IsValid(Move))
	{
		Move->MaxWalkSpeed = Mem->OriginalMaxWalkSpeed;
		Move->MaxWalkSpeedCrouched = Mem->OriginalMaxWalkSpeedCrouched;
	}
	Mem->bCoverMoveSpeedCapped = false;
	Mem->OriginalMaxWalkSpeed = 0.f;
	Mem->OriginalMaxWalkSpeedCrouched = 0.f;
}

// --- ClearPendingShuffle ---

void UBTTask_EnemyCombatFire::ClearPendingShuffle(AEnemyCharacter* Enemy, FFireMemory* Mem) const
{
	if (!Mem) return;
	if (Mem->bShufflePending)
	{
		UCoverPoseComponent* PoseComp = IsValid(Enemy) ? Enemy->GetCoverPoseComponent() : nullptr;
		if (IsValid(PoseComp)) PoseComp->SetLean(ECoverLean::None);
	}
	Mem->bShufflePending = false;
	Mem->ShuffleHoldTimer = 0.f;
}

// --- TryLadderSwapMove: wall-walk to the opposite-side end point ---
// Extracted from the forced side-peek-hop pattern. Sets bLadderSwapMovePending so the
// arrival reset preserves LadderStage = 2 instead of resetting to 0.

bool UBTTask_EnemyCombatFire::TryLadderSwapMove(UBehaviorTreeComponent& OwnerComp,
	FFireMemory* Mem, AAIController* Controller, APawn* Pawn, AEnemyCharacter* Enemy,
	const FCover& CurrentCover, ECoverLean OppositeSide, AActor* Target,
	const UEnemyArchetypeData* DA) const
{
	UWorld* World = OwnerComp.GetWorld();
	if (!World || !IsValid(Pawn) || !IsValid(Enemy) || !IsValid(DA)) return false;
	if (!CurrentCover.IsValid() || !IsValid(Target)) return false;

	FCover HopDest = FindSidePeekCover(World, Pawn, CurrentCover, OppositeSide,
		GetPerceivedThreatLoc(Controller, Target), DA, Controller, Target);
	if (!HopDest.IsValid()) return false;

	const UCapsuleComponent* Cap = Enemy->GetCapsuleComponent();
	const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : DefaultCapsuleRadius;
	const float Standoff = CapRadius + DA->CoverStandoffPadding;
	const FVector HopArrival = UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
		World, HopDest.Data, Standoff, CapRadius, DA->CoverCornerGap, Pawn);

	LogCoverMove(TEXT("ladder-swap"), Pawn, Enemy);
	const EPathFollowingRequestResult::Type MoveResult =
		Controller->MoveToLocation(HopArrival, 25.f, false, true, true, true);
	if (MoveResult != EPathFollowingRequestResult::RequestSuccessful
		&& MoveResult != EPathFollowingRequestResult::AlreadyAtGoal)
	{
		UE_LOG(LogEnemyAI, Log, TEXT("[COVER] %s ladder-swap move refused (result=%d)"),
			*Pawn->GetName(), static_cast<int32>(MoveResult));
		return false;
	}

	// Move accepted — commit the cover swap (mirrors side-peek hop pattern).
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	UCoverReservationSubsystem* ResSub = Mem->CachedResSub.Get();
	if (!ResSub) ResSub = World->GetSubsystem<UCoverReservationSubsystem>();
	if (IsValid(ResSub))
	{
		ResSub->MarkVacated(CurrentCover.Handle, Controller);
		ResSub->SetIntendedCover(Controller, HopDest.Handle);
	}
	if (BB) WriteCoverToBB(BB, HopDest);

	Mem->ReseekCover = HopDest.Handle;
	Mem->ReseekCoverData = HopDest.Data;
	Mem->ReseekArrivalPos = HopArrival;
	Mem->bArrivedAtSlot = false;
	Mem->SlotDwellTime = 0.f;
	Mem->CompromiseConsecutiveCount = 0;
	Mem->CompromiseEvalTimer = 0.f;
	Mem->bRelocatePending = false;
	Mem->ExposeLosTimeoutCount = 0;
	Mem->bLadderForceOppositeSide = false;
	Mem->bLadderForceOverTop = false;
	Mem->LadderOppositeSide = ECoverLean::None;
	// Stage 2 will be set on arrival via bLadderSwapMovePending.
	Mem->bLadderSwapMovePending = true;
	Mem->bShufflePending = false;
	Mem->ShuffleHoldTimer = 0.f;
	Mem->SeekStallBestDist = TNumericLimits<float>::Max();
	Mem->SeekStallAccum = 0.f;

	// Pose for transit — mirror ExecuteShuffleMove's same-height treatment so the anim gate
	// stays open and the walk montage plays (FindSidePeekCover gates same-height).
	const FVector SwapLateral = CurrentCover.Data.Rotation.RotateVector(FVector::RightVector);
	const FVector SwapMoveDir2D = (HopArrival - Pawn->GetActorLocation()).GetSafeNormal2D();
	const float SwapMoveDot = FVector::DotProduct(SwapMoveDir2D, SwapLateral);
	const ECoverLean SwapMoveDirection = (SwapMoveDot >= 0.f) ? ECoverLean::Right : ECoverLean::Left;
	const bool bSwapCrouched = UCoverGeometryStatics::GetCoverHeight(CurrentCover.Data) == ECoverHeight::Crouch;

	UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
	if (IsValid(PoseComp))
	{
		PoseComp->SetPeeking(false);
		PoseComp->SetLean(ECoverLean::None);
		if (bSwapCrouched)
			PoseComp->SetInCover(true, ECoverHeight::Crouch);
		else
			PoseComp->SetInCover(true, ECoverHeight::Stand);
		PoseComp->SetCoverMoving(true, SwapMoveDirection);
	}
	// Stay crouched for crouch→crouch; no UnCrouch needed for stand→stand.
	if (bSwapCrouched)
		if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();

	// Per-tick wall-facing lock during the move (no SetFocus — the facing lock clears it).
	Mem->bCoverMoveFacingActive = true;
	Mem->CoverMoveFacingData = CurrentCover.Data;
	Mem->CoverMoveArrivalPos = HopArrival;
	ApplyCoverMoveSpeed(Enemy, Mem, DA);

	Mem->Phase = EFireTaskPhase::SeekingCover;
	UE_LOG(LogEnemyAI, Log, TEXT("[COVER] %s ladder-swap (%s) -> (%.0f,%.0f,%.0f)"),
		*Pawn->GetName(), OppositeSide == ECoverLean::Left ? TEXT("L") : TEXT("R"),
		HopDest.Data.Location.X, HopDest.Data.Location.Y, HopDest.Data.Location.Z);
	return true;
}

// --- ExecuteShuffleMove (the "full commit" helper) ---
// Issues MoveToLocation, vacate/claim/BB/pose, phase=SeekingCover.
// Shared between the immediate same-side path and the hold-expiry path.

bool UBTTask_EnemyCombatFire::ExecuteShuffleMove(UBehaviorTreeComponent& OwnerComp,
	FFireMemory* Mem, AAIController* Controller, APawn* Pawn, AEnemyCharacter* Enemy,
	const FCover& FromCover, const FCover& ToDest, AActor* Target,
	const UEnemyArchetypeData* DA) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	UWorld* World = OwnerComp.GetWorld();
	if (!BB || !World) return false;

	const UCapsuleComponent* Cap = Enemy->GetCapsuleComponent();
	const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : DefaultCapsuleRadius;
	const float Standoff = CapRadius + DA->CoverStandoffPadding;
	const FVector ShuffleArrival = UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
		World, ToDest.Data, Standoff, CapRadius, DA->CoverCornerGap, Pawn);

	// Move-first: issue the request BEFORE committing the cover swap so a refusal
	// does not strand the pawn with both covers on post-vacate cooldown.
	LogCoverMove(TEXT("shuffle"), Pawn, Enemy);
	const EPathFollowingRequestResult::Type MoveResult =
		Controller->MoveToLocation(ShuffleArrival, 25.f, false, true, true, true);
	if (MoveResult != EPathFollowingRequestResult::RequestSuccessful
		&& MoveResult != EPathFollowingRequestResult::AlreadyAtGoal)
	{
		UE_LOG(LogEnemyAI, Log, TEXT("[COVER] %s shuffle move refused (result=%d) — staying put"),
			*Pawn->GetName(), static_cast<int32>(MoveResult));
		return false;
	}

	// Move accepted — commit the cover swap.
	UCoverReservationSubsystem* ResSub = Mem->CachedResSub.Get();
	if (!ResSub) ResSub = World->GetSubsystem<UCoverReservationSubsystem>();

	if (IsValid(ResSub))
	{
		ResSub->MarkVacated(FromCover.Handle, Controller);
		ResSub->SetIntendedCover(Controller, ToDest.Handle);
	}

	WriteCoverToBB(BB, ToDest);

	Mem->ReseekCover = ToDest.Handle;
	Mem->ReseekCoverData = ToDest.Data;
	Mem->ReseekArrivalPos = ShuffleArrival;

	Mem->bArrivedAtSlot = false;
	Mem->SlotDwellTime = 0.f;
	Mem->CompromiseConsecutiveCount = 0;
	Mem->CompromiseEvalTimer = 0.f;
	Mem->bRelocatePending = false;
	Mem->RelocatePendingSetTime = 0.f;
	Mem->ExposeLosTimeoutCount = 0;
	Mem->bLadderForceOppositeSide = false;
	Mem->bLadderForceOverTop = false;
	Mem->LadderOppositeSide = ECoverLean::None;
	Mem->LadderStage = 0;
	Mem->bLadderSwapMovePending = false;
	Mem->bShufflePending = false;
	Mem->ShuffleHoldTimer = 0.f;
	Mem->SeekStallBestDist = TNumericLimits<float>::Max();
	Mem->SeekStallAccum = 0.f;

	// Direction for the cover-move pose (lateral projection on wall lateral axis).
	const FVector ShuffleLateral = FromCover.Data.Rotation.RotateVector(FVector::RightVector);
	const FVector MoveDir2D = (ShuffleArrival - Pawn->GetActorLocation()).GetSafeNormal2D();
	const float MoveDot = FVector::DotProduct(MoveDir2D, ShuffleLateral);
	const ECoverLean MoveDirection = (MoveDot >= 0.f) ? ECoverLean::Right : ECoverLean::Left;

	// Fix 6: crouch→crouch same-wall shuffles stay crouched (the cover-move montage
	// selection reads CoverHeight; resetting to Stand prevents crouched montages from playing).
	const bool bFromCrouch = UCoverGeometryStatics::GetCoverHeight(FromCover.Data) == ECoverHeight::Crouch;
	const bool bToCrouch = UCoverGeometryStatics::GetCoverHeight(ToDest.Data) == ECoverHeight::Crouch;

	UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
	if (IsValid(PoseComp))
	{
		if (bFromCrouch && bToCrouch)
		{
			// Keep crouched pose alive — re-assert height (over-top peek may have set Stand).
			PoseComp->SetPeeking(false);
			PoseComp->SetLean(ECoverLean::None);
			PoseComp->SetInCover(true, ECoverHeight::Crouch);
			PoseComp->SetCoverMoving(true, MoveDirection);
		}
		else if (!bFromCrouch && !bToCrouch)
		{
			// Stand→stand: keep the standing cover pose alive so the anim gate stays open
			// for the walk montage (resetting drops bInCover, closing the gate mid-move).
			PoseComp->SetPeeking(false);
			PoseComp->SetLean(ECoverLean::None);
			PoseComp->SetInCover(true, ECoverHeight::Stand);
			PoseComp->SetCoverMoving(true, MoveDirection);
		}
		else
		{
			// Mixed-height transition (crouch→stand / stand→crouch): full reset + UnCrouch
			// handled below.
			PoseComp->ResetCoverPose();
			PoseComp->SetCoverMoving(true, MoveDirection);
		}
	}

	// Per-tick wall-facing lock (feature 6 task).
	Mem->bCoverMoveFacingActive = true;
	Mem->CoverMoveFacingData = FromCover.Data;
	Mem->CoverMoveArrivalPos = ShuffleArrival;

	// Cap walk speed to DA->CoverMoveSpeed while the strafe move is in flight (feature 6 task).
	ApplyCoverMoveSpeed(Enemy, Mem, DA);

	// Do NOT set focus on the target during the move window — ApplyCoverFacing clears
	// focus, and the per-tick wall-facing re-assert (bCoverMoveFacingActive) keeps the body
	// aligned to the wall. Target focus resumes at the next Expose via existing code.
	if (bFromCrouch && bToCrouch)
	{
		// Re-crouch: an over-top peek may have un-crouched the pawn before this commit.
		if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
	}
	else if (!bFromCrouch && !bToCrouch)
	{
		// Stand→stand: no crouch state change needed.
	}
	else
	{
		if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
	}
	Mem->Phase = EFireTaskPhase::SeekingCover;

	UE_LOG(LogEnemyAI, Log, TEXT("[COVER] %s shuffle -> (%.0f,%.0f,%.0f)"),
		*Pawn->GetName(), ToDest.Data.Location.X, ToDest.Data.Location.Y, ToDest.Data.Location.Z);
	return true;
}

// --- CommitSameWallShuffle (decision stage) ---
// Computes move direction. Same-side or unavailable anim instance: immediate full commit.
// Opposite-side: defer move behind a hold timer for the idle-side swap animation.

bool UBTTask_EnemyCombatFire::CommitSameWallShuffle(UBehaviorTreeComponent& OwnerComp,
	FFireMemory* Mem, AAIController* Controller, APawn* Pawn, AEnemyCharacter* Enemy,
	const FCover& FromCover, const FCover& ToDest, AActor* Target,
	const UEnemyArchetypeData* DA) const
{
	if (!Mem || !IsValid(Enemy) || !IsValid(DA)) return false;

	// Compute move direction (lateral projection on wall axis).
	const UCapsuleComponent* Cap = Enemy->GetCapsuleComponent();
	const float CapRadius = Cap ? Cap->GetScaledCapsuleRadius() : DefaultCapsuleRadius;
	const float Standoff = CapRadius + DA->CoverStandoffPadding;
	const FVector ShuffleArrival = UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
		Pawn->GetWorld(), ToDest.Data, Standoff, CapRadius, DA->CoverCornerGap, Pawn);
	const FVector ShuffleLateral = FromCover.Data.Rotation.RotateVector(FVector::RightVector);
	const FVector MoveDir2D = (ShuffleArrival - Pawn->GetActorLocation()).GetSafeNormal2D();
	const float MoveDot = FVector::DotProduct(MoveDir2D, ShuffleLateral);
	const ECoverLean MoveDirection = (MoveDot >= 0.f) ? ECoverLean::Right : ECoverLean::Left;

	// Read the anim instance's last cover side to decide whether a pre-move hold is needed.
	ECoverLean AnimLastSide = ECoverLean::None;
	if (USkeletalMeshComponent* MeshComp = Enemy->GetMesh())
	{
		if (UEnemyAnimInstance* AnimInst = Cast<UEnemyAnimInstance>(MeshComp->GetAnimInstance()))
			AnimLastSide = AnimInst->GetLastCoverSide();
	}

	// Same side (or anim instance unavailable): proceed with the full commit immediately.
	const bool bNeedsSwap = (AnimLastSide == ECoverLean::Left || AnimLastSide == ECoverLean::Right)
		&& (MoveDirection != AnimLastSide)
		&& DA->CoverMoveSideSwapDelay > 0.f;

	if (!bNeedsSwap)
		return ExecuteShuffleMove(OwnerComp, Mem, Controller, Pawn, Enemy, FromCover, ToDest, Target, DA);

	// Opposite side: defer the move behind a hold timer so the idle-side swap animates first.
	Mem->bShufflePending = true;
	Mem->PendingShuffleDest = ToDest;
	Mem->PendingShuffleFrom = FromCover;
	Mem->ShuffleHoldTimer = DA->CoverMoveSideSwapDelay;

	// Re-crouch if both covers are crouch height — an over-top peek may have un-crouched the pawn.
	const bool bPendFromCrouch = UCoverGeometryStatics::GetCoverHeight(FromCover.Data) == ECoverHeight::Crouch;
	const bool bPendToCrouch = UCoverGeometryStatics::GetCoverHeight(ToDest.Data) == ECoverHeight::Crouch;
	if (bPendFromCrouch && bPendToCrouch)
		if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();

	// Flip the idle side via SetLean while NOT peeking — existing idle-selection logic swaps the idle montage.
	UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
	if (IsValid(PoseComp))
	{
		PoseComp->SetPeeking(false);
		if (bPendFromCrouch && bPendToCrouch)
			PoseComp->SetInCover(true, ECoverHeight::Crouch);
		PoseComp->SetLean(MoveDirection);
	}

	// Park in Pause with enough timer to cover the hold (+ small epsilon so nothing else rolls).
	Mem->Phase = EFireTaskPhase::Pause;
	Mem->PhaseTimer = DA->CoverMoveSideSwapDelay + 0.05f;

	if (GetCoverAnimLogLevel() > 0)
		UE_LOG(LogEnemyAI, Log, TEXT("[COVERSTATE] %s shuffle pending side-swap hold=%.2f dir=%d"),
			*Pawn->GetName(), DA->CoverMoveSideSwapDelay, static_cast<int32>(MoveDirection));
	return true;
}

UBTTask_EnemyCombatFire::UBTTask_EnemyCombatFire()
{
	NodeName = TEXT("Enemy Combat Fire");
	bNotifyTick = true;

	CoverTargetKey.SelectedKeyName = TEXT("CoverTarget");

	// Add Cover type filter for the CoverTargetKey selector (same pattern as plugin's BTTask_EQS_Query_Cover)
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		CoverTargetKey.AllowedTypes.Add(NewObject<UBlackboardKeyType_Cover>(this, TEXT("CoverTargetKey_Cover")));
	}
}

void UBTTask_EnemyCombatFire::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	if (UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		CoverTargetKey.ResolveSelectedKey(*BBAsset);
	}
}

uint16 UBTTask_EnemyCombatFire::GetInstanceMemorySize() const
{
	return sizeof(FFireMemory);
}

void UBTTask_EnemyCombatFire::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	Super::InitializeMemory(OwnerComp, NodeMemory, InitType);
	FFireMemory* Mem = CastInstanceNodeMemory<FFireMemory>(NodeMemory);
	new (Mem) FFireMemory();
}

void UBTTask_EnemyCombatFire::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	FFireMemory* Mem = CastInstanceNodeMemory<FFireMemory>(NodeMemory);

	// Fix 6: on BT teardown paths that skip AbortTask (subtree swap / asset reassignment),
	// blind-fire state (ExtraSpreadDegrees, CoverPose bBlindFiring) leaks on the character.
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);

	if (Mem->bBlindFiringNow && IsValid(Enemy))
		ClearBlindFireState(Enemy, Mem);

	// Fix 7: RestoreCoverMoveSpeed on BT teardown -- paths that skip AbortTask leave
	// MaxWalkSpeed capped at CoverMoveSpeed indefinitely.
	if (Mem->bCoverMoveSpeedCapped && IsValid(Enemy))
		RestoreCoverMoveSpeed(Enemy, Mem);

	if (Mem->bShufflePending && IsValid(Enemy))
		ClearPendingShuffle(Enemy, Mem);

	// Same skip-AbortTask teardown (combat subtree swapped out, e.g. Combat -> Searching): a latched
	// cover pose + crouch + BB_HasCover otherwise ride into the next state — the enemy then wanders
	// the searching branch crouched in a cover pose, "taking cover" in the open. Destroy only:
	// a StoreSubtree pause must keep valid in-cover state for the resume.
	if (CleanupType == EBTMemoryClear::Destroy)
	{
		if (IsValid(Enemy))
		{
			if (UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent())
			{
				PoseComp->SetCoverMoving(false, ECoverLean::None);
				PoseComp->ResetCoverPose();
			}
			if (ACharacter* Char = Cast<ACharacter>(Enemy)) Char->UnCrouch();
		}
		if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
		{
			ClearCoverBB(BB);
			BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
		}
		// An in-flight reseek intent would otherwise block that cover for everyone until this
		// controller's next SetIntendedCover (the map self-prunes only on controller destruction).
		if (Mem->ReseekCover.IsValid() && IsValid(Controller))
		{
			if (UCoverReservationSubsystem* ResSub = Mem->CachedResSub.Get())
				ResSub->ClearIntendedCover(Controller);
		}
	}

	Mem->~FFireMemory();
	Super::CleanupMemory(OwnerComp, NodeMemory, CleanupType);
}

// --- BB key helpers ---

FCover UBTTask_EnemyCombatFire::ReadCoverFromBB(const UBlackboardComponent* BB) const
{
	if (!BB || CoverTargetKey.SelectedKeyName == NAME_None) return FCover();
	return BB->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
}

void UBTTask_EnemyCombatFire::WriteCoverToBB(UBlackboardComponent* BB, const FCover& Cover) const
{
	if (!BB || CoverTargetKey.SelectedKeyName == NAME_None) return;
	BB->SetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID(), Cover);
}

void UBTTask_EnemyCombatFire::ClearCoverBB(UBlackboardComponent* BB) const
{
	if (!BB || CoverTargetKey.SelectedKeyName == NAME_None) return;
	BB->ClearValue(CoverTargetKey.GetSelectedKeyID());
}

bool UBTTask_EnemyCombatFire::ValidateCoverStillHeld(UBlackboardComponent* BB, APawn* Pawn, AEnemyCharacter* Enemy) const
{
	if (!BB || !IsValid(Pawn)) return false;
	if (!BB->GetValueAsBool(AEnemyAIController::BB_HasCover)) return false;

	const FCover Cover = ReadCoverFromBB(BB);
	bool bStale = !Cover.IsValid();
	if (!bStale)
		bStale = FVector::Dist2D(Pawn->GetActorLocation(), Cover.Data.Location) > StaleCoverAbandonDist;
	if (!bStale) return true;

	// The pawn left this cover without anything clearing the flag (pursue / bounding advance /
	// searching wander / branch switch that skipped cleanup) — stop it acting "in cover" in the open.
	BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
	ClearCoverBB(BB);

	if (IsValid(Enemy))
	{
		if (UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent())
		{
			PoseComp->SetCoverMoving(false, ECoverLean::None);
			PoseComp->ResetCoverPose();
		}
	}
	if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();

	if (GetCoverAnimLogLevel() > 0)
		UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s STALE cover cleared (dist=%.0f) — was acting in-cover in the open"),
			*GetNameSafe(Pawn), Cover.IsValid() ? FVector::Dist2D(Pawn->GetActorLocation(), Cover.Data.Location) : -1.f);
	return false;
}

EBTNodeResult::Type UBTTask_EnemyCombatFire::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FFireMemory* Mem = CastInstanceNodeMemory<FFireMemory>(NodeMemory);
	*Mem = FFireMemory(); // reset to defaults (InitializeMemory already constructed)

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	// Cache subsystem pointers for tick-path use (PERF #8)
	UWorld* W = OwnerComp.GetWorld();
	if (W)
	{
		Mem->CachedCoverSys = ACoverSystem::GetCoverSystem(W);
		Mem->CachedResSub = W->GetSubsystem<UCoverReservationSubsystem>();
	}

	// Stale-cover correction on entry: the pawn may arrive here from a searching wander, pursue,
	// or maneuver task with BB_HasCover still true from a cover it left long ago — every branch
	// below trusts that flag, so validate before anything reads it.
	ValidateCoverStillHeld(BB, Pawn, Enemy);

	// Only allow Failed from ExecuteTask when NOT in combat — Bug 2 fix.
	const EEnemyAwarenessState Awareness = static_cast<EEnemyAwarenessState>(
		BB->GetValueAsEnum(AEnemyAIController::BB_AwarenessState));

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

	// Morale read — gates pursue and timing adjustments.
	const UEnemyMoraleComponent* MoraleComp = Enemy->GetMoraleComponent();
	const EMoraleState Morale = IsValid(MoraleComp) ? MoraleComp->GetMoraleState() : EMoraleState::Confident;
	const bool bAggressive = (Morale == EMoraleState::Confident);
	const bool bHunkered = (Morale == EMoraleState::Broken);

	if (!IsValid(Target))
	{
		if (Awareness == EEnemyAwarenessState::Combat)
		{
			if (bAggressive)
			{
				// No target yet but combat aware — pursue last known location.
				// A pose latched by MoveToCoverPoint's arrival would montage-slide the whole pursue
				// (and leave its wall-facing focal point steering the body sideways) — clear both.
				if (UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent()) PoseComp->ResetCoverPose();
				Controller->ClearFocus(EAIFocusPriority::Gameplay);
				Mem->Phase = EFireTaskPhase::Pursuing;
				Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
				const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
				if (!LastKnown.IsNearlyZero())
					Controller->MoveToLocation(LastKnown, 100.f, false, true, false, true);
			}
			else
			{
				// Shaken/Broken: hold position, seek cover if exposed (cooldown-gated).
				Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
				const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
				if (!bHasCover)
				{
					const float Now = W ? W->GetTimeSeconds() : 0.f;
					if ((Now - Mem->LastReseekCoverTime) >= SuppressedReseekCooldown)
					{
						Mem->LastReseekCoverTime = Now;
						TryReseekCover(OwnerComp, Mem, Controller, Pawn, Enemy, nullptr, DA, false);
					}
				}

				if (Mem->Phase != EFireTaskPhase::SeekingCover)
				{
					if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
					Mem->Phase = EFireTaskPhase::Pause;
					Mem->PauseDuration = RollPauseDuration(DA, bHunkered);
					Mem->PhaseTimer = Mem->PauseDuration;
				}
			}
			return EBTNodeResult::InProgress;
		}
		if (GetCoverAnimLogLevel() > 0)
			UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s CombatFire FAIL (awareness!=Combat) — selector falls to siblings"), *GetNameSafe(Pawn));
		return EBTNodeResult::Failed;
	}

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	const bool bInRange = BB->GetValueAsBool(AEnemyAIController::BB_TargetInRange);
	if (!bInRange && !bHasLOS)
	{
		if (Awareness == EEnemyAwarenessState::Combat)
		{
			if (bAggressive)
			{
				// Out of range/LOS but combat — pursue.
				// Clear any pose latched by MoveToCoverPoint's arrival (montage-slide + wall-facing yaw).
				if (UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent()) PoseComp->ResetCoverPose();
				Controller->SetFocus(Target);
				Mem->Phase = EFireTaskPhase::Pursuing;
				Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
				const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
				const FVector PursueTarget = LastKnown.IsNearlyZero() ? Target->GetActorLocation() : LastKnown;
				Controller->MoveToLocation(PursueTarget, 100.f, false, true, false, true);
			}
			else
			{
				// Shaken/Broken: hold position, seek cover if exposed (cooldown-gated).
				Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
				const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
				if (!bHasCover)
				{
					const float Now = W ? W->GetTimeSeconds() : 0.f;
					if ((Now - Mem->LastReseekCoverTime) >= SuppressedReseekCooldown)
					{
						Mem->LastReseekCoverTime = Now;
						TryReseekCover(OwnerComp, Mem, Controller, Pawn, Enemy, Target, DA, false);
					}
				}

				if (Mem->Phase != EFireTaskPhase::SeekingCover)
				{
					if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
					Mem->Phase = EFireTaskPhase::Pause;
					Mem->PauseDuration = RollPauseDuration(DA, bHunkered);
					Mem->PhaseTimer = Mem->PauseDuration;
				}
			}
			return EBTNodeResult::InProgress;
		}
		if (GetCoverAnimLogLevel() > 0)
			UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s CombatFire FAIL (awareness!=Combat) — selector falls to siblings"), *GetNameSafe(Pawn));
		return EBTNodeResult::Failed;
	}

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	// Start in Acquire phase. UpdateCombatFocus preserves the wall-aligned focal point when the
	// task starts already posed in cover (arrived via MoveToCoverPoint).
	Enemy->SetAimTarget(Target);
	UpdateCombatFocus(Controller, Enemy, Target);
	Mem->Phase = EFireTaskPhase::Acquire;
	Mem->PhaseTimer = DA->ReactionDelay;

	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyCombatFire::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FFireMemory* Mem = CastInstanceNodeMemory<FFireMemory>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Acquire world pointer once per tick; re-acquire cached subsystem pointers if stale (PERF #8)
	UWorld* TickWorld = OwnerComp.GetWorld();
	if (TickWorld)
	{
		if (!Mem->CachedCoverSys.IsValid())
			Mem->CachedCoverSys = ACoverSystem::GetCoverSystem(TickWorld);
		if (!Mem->CachedResSub.IsValid())
			Mem->CachedResSub = TickWorld->GetSubsystem<UCoverReservationSubsystem>();
	}
	const float TickNow = TickWorld ? TickWorld->GetTimeSeconds() : 0.f;

	// Stale-cover correction: BB_HasCover must mean "AT my cover" every tick, not "arrived once".
	// Skipped during SeekingCover — a reseek/ladder transit is legitimately far from the old point.
	if (Mem->Phase != EFireTaskPhase::SeekingCover)
		ValidateCoverStillHeld(BB, Pawn, Enemy);

	// Drift-correct facing lock: the corrective MoveToLocation steers the pawn toward the wall
	// (bUseControllerDesiredRotation follows path-following during the move). Re-assert the
	// back-to-cover yaw every tick so the body stays tucked facing out — the move still translates
	// the pawn to the hunker; only its facing is overridden. Clear once home, on path idle, or when
	// a non-tucked phase takes over (pursue/relocate re-set their own facing and must not be fought).
	if (Mem->bDriftCorrecting)
	{
		const bool bStillTucked = BB->GetValueAsBool(AEnemyAIController::BB_HasCover)
			&& (Mem->Phase == EFireTaskPhase::Acquire
				|| Mem->Phase == EFireTaskPhase::Pause
				|| Mem->Phase == EFireTaskPhase::Recover);
		const float DriftDist = FVector::Dist2D(Pawn->GetActorLocation(), Mem->DriftArrivalPos);
		const bool bDriftDone = DriftDist <= CoverDriftCorrectDist
			|| Controller->GetMoveStatus() == EPathFollowingStatus::Idle;
		if (!bStillTucked || bDriftDone)
			Mem->bDriftCorrecting = false;
		else
		{
			ApplyCoverFacing(Controller, Pawn, Mem->DriftFacingCover);
			Mem->bFacingReassertedThisTick = true;
		}
	}

	// Cover-move facing lock (feature 6 task): while a same-wall shuffle/ladder move is in flight,
	// re-assert back-to-cover wall yaw every tick so the strafe reads as a lateral slide rather
	// than a 180° body swing. Cleared on arrival (SeekingCover arrive block) or phase change.
	if (Mem->bCoverMoveFacingActive)
	{
		const float MoveDist = FVector::Dist2D(Pawn->GetActorLocation(), Mem->CoverMoveArrivalPos);
		const bool bWithinArrivalProximity = MoveDist <= SeekCoverArrivalIdleRadius;
		const bool bPathIdle = Controller->GetMoveStatus() == EPathFollowingStatus::Idle;
		if (bWithinArrivalProximity || bPathIdle)
		{
			// Releasing on proximity while the path is still Moving leaves the engine's Move-priority
			// focal point alive (ApplyCoverFacing only clears it while re-asserted each tick); the next
			// tick's UpdateControlRotation then swings yaw to the walk direction during deceleration.
			// Stop the move so OnPathFinished clears that focal point — the pawn is already within
			// the acceptance envelope, so ending the move early doesn't cost distance.
			if (bWithinArrivalProximity && !bPathIdle)
				Controller->StopMovement();
			Mem->bCoverMoveFacingActive = false;
		}
		else
		{
			ApplyCoverFacing(Controller, Pawn, Mem->CoverMoveFacingData);
			Mem->bFacingReassertedThisTick = true;
		}
	}

	// --- Cover-move diagnostic (enemy.CoverMoveDebug) ---
	if (GetCoverMoveDebugLevel() > 0)
	{
		const float PostArrivalSoak = 2.f;
		const bool bInSoakWindow = (TickNow - Mem->CoverMoveArrivalTime) < PostArrivalSoak;
		const bool bShouldLog = Mem->bShufflePending
			|| Mem->bCoverMoveFacingActive
			|| Mem->Phase == EFireTaskPhase::SeekingCover
			|| bInSoakWindow;

		if (bShouldLog)
		{
			const float ActorYaw = Pawn->GetActorRotation().Yaw;
			const float CtrlYaw = Controller->GetControlRotation().Yaw;

			float WallYaw = -999.f;
			if (Mem->bCoverMoveFacingActive)
				WallYaw = UCoverGeometryStatics::GetFireArcForward(Mem->CoverMoveFacingData).Rotation().Yaw;
			else if (Mem->bDriftCorrecting)
				WallYaw = UCoverGeometryStatics::GetFireArcForward(Mem->DriftFacingCover).Rotation().Yaw;

			const FVector FocG = Controller->GetFocalPointForPriority(EAIFocusPriority::Gameplay);
			const FVector FocM = Controller->GetFocalPointForPriority(EAIFocusPriority::Move);
			const FVector FocD = Controller->GetFocalPointForPriority(EAIFocusPriority::Default);
			const FVector FocResolved = Controller->GetFocalPoint();
			const bool bFocGValid = FAISystem::IsValidLocation(FocG);
			const bool bFocMValid = FAISystem::IsValidLocation(FocM);
			const bool bFocDValid = FAISystem::IsValidLocation(FocD);

			const bool bReasserted = Mem->bFacingReassertedThisTick;

			const EPathFollowingStatus::Type PathStatus = Controller->GetMoveStatus();
			const FVector Vel2D = FVector(Pawn->GetVelocity().X, Pawn->GetVelocity().Y, 0.f);
			const float Speed2D = Vel2D.Size();
			const float VelYaw = (Speed2D > 5.f) ? Vel2D.Rotation().Yaw : -999.f;

			const UCoverPoseComponent* PoseDbg = Enemy->GetCoverPoseComponent();
			const bool bInCov = IsValid(PoseDbg) && PoseDbg->bInCover;
			const int32 Height = IsValid(PoseDbg) ? static_cast<int32>(PoseDbg->CoverHeight) : -1;
			const int32 Lean = IsValid(PoseDbg) ? static_cast<int32>(PoseDbg->LeanDirection) : -1;
			const bool bMoving = IsValid(PoseDbg) && PoseDbg->bCoverMoving;

			FString CoverMontName = TEXT("none");
			FString MoveMontName = TEXT("none");
			if (USkeletalMeshComponent* MeshComp = Enemy->GetMesh())
			{
				if (UEnemyAnimInstance* AnimInst = Cast<UEnemyAnimInstance>(MeshComp->GetAnimInstance()))
				{
					if (IsValid(AnimInst->GetActiveCoverMontage()))
						CoverMontName = AnimInst->GetActiveCoverMontage()->GetName();
					if (IsValid(AnimInst->GetActiveCoverMoveMontage()))
						MoveMontName = AnimInst->GetActiveCoverMoveMontage()->GetName();
				}
			}

			UE_LOG(LogEnemyAI, Log,
				TEXT("[COVERMOVEDBG] %s yaw=%.1f ctrl=%.1f wall=%.1f focG=%d focM=%d focD=%d focLoc=(%.0f,%.0f) reassert=%d path=%d velYaw=%.1f speed=%.0f inCov=%d h=%d lean=%d moving=%d pend=%d phase=%d covMont=%s moveMont=%s"),
				*Pawn->GetName(),
				ActorYaw, CtrlYaw, WallYaw,
				bFocGValid ? 1 : 0, bFocMValid ? 1 : 0, bFocDValid ? 1 : 0,
				FocResolved.X, FocResolved.Y,
				bReasserted ? 1 : 0,
				static_cast<int32>(PathStatus),
				VelYaw, Speed2D,
				bInCov ? 1 : 0, Height, Lean, bMoving ? 1 : 0,
				Mem->bShufflePending ? 1 : 0,
				static_cast<int32>(Mem->Phase),
				*CoverMontName, *MoveMontName);

			// Directional arrows: green=forward, blue=controlRot, red=focalPoint, yellow=wallTarget
			if (TickWorld)
			{
				const FVector ChestLoc = Pawn->GetActorLocation() + FVector(0.f, 0.f, 80.f);
				const float ArrowLen = 80.f;
				const float Life = -1.f; // single frame

				// Green: actor forward
				DrawDebugDirectionalArrow(TickWorld, ChestLoc,
					ChestLoc + Pawn->GetActorForwardVector() * ArrowLen,
					8.f, FColor::Green, false, Life);

				// Blue: control rotation
				const FVector CtrlFwd = Controller->GetControlRotation().Vector();
				DrawDebugDirectionalArrow(TickWorld, ChestLoc,
					ChestLoc + CtrlFwd * ArrowLen,
					8.f, FColor::Blue, false, Life);

				// Red: line to focal point (when valid)
				if (FAISystem::IsValidLocation(FocResolved))
				{
					const FVector ToFocal = (FocResolved - ChestLoc).GetSafeNormal() * ArrowLen;
					DrawDebugDirectionalArrow(TickWorld, ChestLoc,
						ChestLoc + ToFocal,
						8.f, FColor::Red, false, Life);
				}

				// Yellow: wall-facing target
				if (WallYaw > -998.f)
				{
					const FVector WallFwd = FRotator(0.f, WallYaw, 0.f).Vector();
					DrawDebugDirectionalArrow(TickWorld, ChestLoc,
						ChestLoc + WallFwd * ArrowLen,
						8.f, FColor::Yellow, false, Life);
				}
			}
		}
	}
	Mem->bFacingReassertedThisTick = false;

	// --- Pending shuffle hold-expiry: pre-move idle-side swap wait ---
	if (Mem->bShufflePending)
	{
		Mem->ShuffleHoldTimer -= DeltaSeconds;
		if (Mem->ShuffleHoldTimer <= 0.f)
		{
			// Hold expired — clear pending state and attempt the full commit.
			const FCover PendDest = Mem->PendingShuffleDest;
			const FCover PendFrom = Mem->PendingShuffleFrom;
			Mem->bShufflePending = false;
			Mem->ShuffleHoldTimer = 0.f;

			// Abort helper: reset lean, re-roll pause so the inflated hold timer doesn't hang.
			auto AbortPending = [&](const TCHAR* Reason)
			{
				UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
				if (IsValid(PoseComp)) PoseComp->SetLean(ECoverLean::None);
				const UEnemyMoraleComponent* MC = Enemy->GetMoraleComponent();
				const bool bHunk = IsValid(MC) && MC->GetMoraleState() == EMoraleState::Broken;
				Mem->PhaseTimer = RollPauseDuration(DA, bHunk);
				if (GetCoverAnimLogLevel() > 0)
					UE_LOG(LogEnemyAI, Log, TEXT("[COVERSTATE] %s pending shuffle aborted (%s)"), *Pawn->GetName(), Reason);
			};

			// Gate: phase must still be Pause and current cover must match the origin.
			// A reseek/relocate mid-hold reassigns the cover; committing would shuffle
			// from the wrong point.
			if (Mem->Phase != EFireTaskPhase::Pause || ReadCoverFromBB(BB).Handle != PendFrom.Handle)
			{
				AbortPending(TEXT("phase/cover changed"));
			}
			// Gate: dest must still be valid.
			else if (!PendDest.IsValid())
			{
				AbortPending(TEXT("invalid dest"));
			}
			else
			{
				// Gate: suppression check — natural shuffle is gated !bSuppressed; honour that here.
				USuppressionComponent* PendSuppr = Enemy->GetSuppressionComponent();
				const bool bPendSuppressed = IsValid(PendSuppr) && PendSuppr->IsSuppressed();
				if (bPendSuppressed)
				{
					AbortPending(TEXT("suppressed"));
				}
				else
				{
					// Gate: target must be valid.
					AActor* PendTarget = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
					if (!IsValid(PendTarget))
					{
						AbortPending(TEXT("no target"));
					}
					else
					{
						// Re-validate occupancy.
						bool bOccupied = false;
						ACoverSystem* CoverSys = Mem->CachedCoverSys.Get();
						if (IsValid(CoverSys))
						{
							AController* Occupant = CoverSys->GetOccupyingController(PendDest.Handle);
							if (Occupant && Occupant != Controller) bOccupied = true;
						}
						UCoverReservationSubsystem* ResSub = Mem->CachedResSub.Get();
						if (!bOccupied && IsValid(ResSub) && ResSub->IsCoverIntendedByOther(PendDest.Handle, Controller))
							bOccupied = true;

						if (bOccupied)
						{
							AbortPending(TEXT("occupancy"));
						}
						else
						{
							UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
							if (IsValid(PoseComp)) PoseComp->SetLean(ECoverLean::None);

							if (!ExecuteShuffleMove(OwnerComp, Mem, Controller, Pawn, Enemy,
								PendFrom, PendDest, PendTarget, DA))
							{
								// Move refused — re-roll pause.
								const UEnemyMoraleComponent* MC = Enemy->GetMoraleComponent();
								const bool bHunk = IsValid(MC) && MC->GetMoraleState() == EMoraleState::Broken;
								Mem->PhaseTimer = RollPauseDuration(DA, bHunk);
								if (GetCoverAnimLogLevel() > 0)
									UE_LOG(LogEnemyAI, Log, TEXT("[COVERSTATE] %s pending shuffle aborted (move refused)"), *Pawn->GetName());
							}
						}
					}
				}
			}
		}
	}

	// In-world cover debug (enemy.CoverAnimLog): line pawn→its BB cover point, sphere at the
	// hunker position, phase/side/cycles text overhead. Makes enemy↔diamond mapping visible.
	if (GetCoverAnimLogLevel() > 0 && TickWorld && BB->GetValueAsBool(AEnemyAIController::BB_HasCover))
	{
		const FCover DbgCover = ReadCoverFromBB(BB);
		if (DbgCover.IsValid())
		{
			const FVector PawnLoc = Pawn->GetActorLocation();
			const FVector HunkerDbg = UCoverGeometryStatics::GetHunkerPosition(DbgCover.Data, 50.f);
			DrawDebugLine(TickWorld, PawnLoc, DbgCover.Data.Location + FVector(0, 0, 30), FColor::Green, false, 0.1f, 0, 2.f);
			DrawDebugSphere(TickWorld, HunkerDbg + FVector(0, 0, 30), 18.f, 8, FColor::Cyan, false, 0.1f);
			const UCoverPoseComponent* DbgPose = Enemy->GetCoverPoseComponent();
			DrawDebugString(TickWorld, PawnLoc + FVector(0, 0, 120),
				FString::Printf(TEXT("ph=%d lean=%d cyc=%d posed=%d"),
					static_cast<int32>(Mem->Phase),
					DbgPose ? static_cast<int32>(DbgPose->LeanDirection) : -1,
					Mem->PeekCyclesAtCover,
					(DbgPose && DbgPose->bInCover) ? 1 : 0),
				nullptr, FColor::Yellow, 0.1f, true);
		}
	}

	const EEnemyAwarenessState Awareness = static_cast<EEnemyAwarenessState>(
		BB->GetValueAsEnum(AEnemyAIController::BB_AwarenessState));

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

	// Morale read — gates pursue and timing adjustments.
	const UEnemyMoraleComponent* MoraleComp = Enemy->GetMoraleComponent();
	const EMoraleState Morale = IsValid(MoraleComp) ? MoraleComp->GetMoraleState() : EMoraleState::Confident;
	const bool bAggressive = (Morale == EMoraleState::Confident);
	const bool bHunkered = (Morale == EMoraleState::Broken);

	// Bug 2 fix: only allow Failed when NOT in Combat.
	if (!IsValid(Target))
	{
		StopFireAndCleanUp(OwnerComp, Mem);
		if (Awareness != EEnemyAwarenessState::Combat)
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

		if (bAggressive)
		{
			// Still in Combat with no target — pursue last known.
			Mem->Phase = EFireTaskPhase::Pursuing;
			const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
			if (!LastKnown.IsNearlyZero() && Controller->GetMoveStatus() != EPathFollowingStatus::Moving)
				Controller->MoveToLocation(LastKnown, 100.f, false, true, false, true);
		}
		else
		{
			// Shaken/Broken: hold position, seek cover if exposed (cooldown-gated).
			const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (!bHasCover && Mem->Phase != EFireTaskPhase::SeekingCover)
			{
				const float Now = TickNow;
				if ((Now - Mem->LastReseekCoverTime) >= SuppressedReseekCooldown)
				{
					Mem->LastReseekCoverTime = Now;
					TryReseekCover(OwnerComp, Mem, Controller, Pawn, Enemy, nullptr, DA, false);
				}
			}

			if (Mem->Phase != EFireTaskPhase::SeekingCover)
			{
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
				Mem->Phase = EFireTaskPhase::Pause;
				Mem->PauseDuration = RollPauseDuration(DA, bHunkered);
				Mem->PhaseTimer = Mem->PauseDuration;
			}
		}
		return;
	}

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	const bool bInRange = BB->GetValueAsBool(AEnemyAIController::BB_TargetInRange);

	// Feature 1a: cover vision cone — compute bTargetInPeekCone once per tick.
	// Only meaningful when posed in cover (arrived + has cover); outside cover bEffectiveLOS == bHasLOS.
	// PERF #10: trig values (cos/tan) cached in FFireMemory; cover data reused from Mem when arrived.
	bool bTargetInPeekCone = true;
	{
		const bool bHasCoverNowCone = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
		const UCoverPoseComponent* PoseConeComp = Enemy->GetCoverPoseComponent();
		const bool bPosedInCover = IsValid(PoseConeComp) && PoseConeComp->bInCover && bHasCoverNowCone;
		if (bPosedInCover && IsValid(Target) && IsValid(DA) && DA->CoverPeekConeHalfAngleDeg < 179.9f)
		{
			// Reuse Mem cover data when arrived (stable, avoids redundant BB read).
			const bool bUseMem = Mem->bArrivedAtSlot && Mem->ReseekCover.IsValid();
			FCoverData ConeData;
			bool bConeDataValid = false;
			if (bUseMem)
			{
				ConeData = Mem->ReseekCoverData;
				bConeDataValid = true;
			}
			else
			{
				const FCover ConeCover = ReadCoverFromBB(BB);
				if (ConeCover.IsValid()) { ConeData = ConeCover.Data; bConeDataValid = true; }
			}

			if (bConeDataValid)
			{
				// Cache trig when the DA angle changes (avoids per-tick cos).
				if (Mem->CachedConeHalfAngle != DA->CoverPeekConeHalfAngleDeg)
				{
					Mem->CachedConeHalfAngle = DA->CoverPeekConeHalfAngleDeg;
					Mem->CachedConeHalfCos = FMath::Cos(FMath::DegreesToRadians(DA->CoverPeekConeHalfAngleDeg));
				}

				const FVector FireFwd2D = UCoverGeometryStatics::GetFireArcForward(ConeData).GetSafeNormal2D();

				// Origin: the pawn's 2D location (not the cover point). Single unbiased cone —
				// the fire reach must never exceed what the torso AO can point at (the old
				// lean-biased extension fired at bearings the gun model couldn't reach).
				const FVector ConeOrigin2D = FVector(Pawn->GetActorLocation().X, Pawn->GetActorLocation().Y, 0.f);
				const FVector TargetLoc2D = FVector(Target->GetActorLocation().X, Target->GetActorLocation().Y, 0.f);
				const FVector ToTarget2D = (TargetLoc2D - ConeOrigin2D).GetSafeNormal();

				const float DotFwd = FVector::DotProduct(FireFwd2D, ToTarget2D);
				bTargetInPeekCone = (DotFwd >= Mem->CachedConeHalfCos);

				if (!bTargetInPeekCone && GetFlankBreakLogLevel() > 0)
				{
					const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotFwd, -1.f, 1.f)));
					if (FMath::Fmod(TickNow, 1.f) < (TickWorld ? TickWorld->GetDeltaSeconds() : 0.016f))
						UE_LOG(LogEnemyAI, Log,
							TEXT("[FLANKDBG] %s coneBlocked angle=%.1f halfCone=%.1f lean=%d"),
							*Pawn->GetName(), AngleDeg, DA->CoverPeekConeHalfAngleDeg,
							static_cast<int32>(PoseConeComp->LeanDirection));
				}
			}
		}
	}

	// Feature 1b: bEffectiveLOS applies the cone gate ONLY at fire-decision sites.
	// Do NOT use bEffectiveLOS for the pursue guard or any out-of-cover logic.
	// Heavy gate: when MaxAimYawDeg > 0, reject bearings beyond the aim resolver's yaw cap.
	bool bWithinAimYaw = true;
	if (IsValid(DA) && DA->MaxAimYawDeg > 0.f && IsValid(Target))
	{
		const FVector ToTarget2D = (Target->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
		const float YawDelta = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(FVector::DotProduct(Pawn->GetActorForwardVector().GetSafeNormal2D(), ToTarget2D), -1.f, 1.f)));
		bWithinAimYaw = (YawDelta <= DA->MaxAimYawDeg);
	}
	const bool bEffectiveLOS = bHasLOS && bTargetInPeekCone && bWithinAimYaw;

	// Feature 1c: pending-relocate timeout — a flanked enemy continuously firing can never reach
	// bNotFiring, so bRelocatePending defers forever. After CoverRelocatePendingTimeout, force it.
	// Gated: never fires while already moving to cover or pursuing — those phases handle
	// their own cover lifecycle and a stale timeout would hijack the chase.
	if (Mem->bRelocatePending && IsValid(DA) && DA->CoverRelocatePendingTimeout > 0.f
		&& Mem->RelocatePendingSetTime > 0.f
		&& Mem->Phase != EFireTaskPhase::Pursuing
		&& Mem->Phase != EFireTaskPhase::SeekingCover
		&& (TickNow - Mem->RelocatePendingSetTime) >= DA->CoverRelocatePendingTimeout)
	{
		// Stop firing so the pawn is clean for the relocate, then execute immediately.
		AWeaponBase* PendW = Enemy->GetCurrentWeapon();
		if (IsValid(PendW) && PendW->IsFiring()) PendW->StopFiring();

		const bool bHasCoverPend = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
		FCover PendCover;
		FCoverData PendCoverData;
		FCoverHandle PendHandle;
		if (bHasCoverPend)
		{
			PendCover = ReadCoverFromBB(BB);
			if (PendCover.IsValid()) { PendHandle = PendCover.Handle; PendCoverData = PendCover.Data; }
		}
		if (GetFlankBreakLogLevel() > 0)
			UE_LOG(LogEnemyAI, Log, TEXT("[FLANKDBG] %s pending-relocate timeout — force-executing"), *Pawn->GetName());
		// RelocatePendingSetTime and bRelocatePending cleared inside ExecuteRelocate.
		ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
			PendHandle, PendCoverData, DA, bHasLOS);
		return;
	}

	// Bug 2 fix: no LOS + out of range while still in Combat — pursue instead of failing.
	// Skip this guard for phases that are already handling movement (avoids path churn / slot release).
	if (!bInRange && !bHasLOS
		&& Mem->Phase != EFireTaskPhase::Fire
		&& Mem->Phase != EFireTaskPhase::Pursuing
		&& Mem->Phase != EFireTaskPhase::SeekingCover)
	{
		if (Awareness != EEnemyAwarenessState::Combat)
		{
			StopFireAndCleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		StopFireAndCleanUp(OwnerComp, Mem);
		// Cleanup dropped focus — restore target-tracking for the pursue / hold that follows.
		Controller->SetFocus(Target);

		if (bAggressive)
		{
			// Transition to pursue.
			Mem->Phase = EFireTaskPhase::Pursuing;
			const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
			const FVector PursueTarget = LastKnown.IsNearlyZero() ? Target->GetActorLocation() : LastKnown;
			Controller->MoveToLocation(PursueTarget, 100.f, false, true, false, true);
		}
		else
		{
			// Shaken/Broken: hold position, seek cover if exposed (cooldown-gated).
			const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (!bHasCover)
			{
				const float Now = TickNow;
				if ((Now - Mem->LastReseekCoverTime) >= SuppressedReseekCooldown)
				{
					Mem->LastReseekCoverTime = Now;
					TryReseekCover(OwnerComp, Mem, Controller, Pawn, Enemy, Target, DA, false);
				}
			}

			if (Mem->Phase != EFireTaskPhase::SeekingCover)
			{
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
				Mem->Phase = EFireTaskPhase::Pause;
				Mem->PauseDuration = RollPauseDuration(DA, bHunkered);
				Mem->PhaseTimer = Mem->PauseDuration;
			}
		}
		return;
	}

	// Phase 4: fetch suppression state once per tick (moved above Pursuing/SeekingCover so
	// early returns from those phases still refresh the edge-detect flag — Fix 4).
	USuppressionComponent* SupprComp = Enemy->GetSuppressionComponent();
	const bool bSuppressed = IsValid(SupprComp) && SupprComp->IsSuppressed();

	// --- Suppressed blind-fire: per-episode decision + edge detect ---
	// Roll once when a suppression episode STARTS. Reset when suppression clears.
	if (bSuppressed && !Mem->bWasSuppressedLastTick)
	{
		// New suppression episode — roll hide vs blind-fire
		const float TotalWeight = DA->SuppressedHideWeight + DA->SuppressedBlindFireWeight;
		if (TotalWeight > 0.f && DA->SuppressedBlindFireWeight > 0.f)
		{
			const float Roll = FMath::FRand() * TotalWeight;
			Mem->bBlindFireChosen = (Roll >= DA->SuppressedHideWeight);
		}
		else
		{
			Mem->bBlindFireChosen = false;
		}
		Mem->bBlindFireDecided = true;
	}
	else if (!bSuppressed && Mem->bWasSuppressedLastTick)
	{
		// Suggestion 9: capture unspent blind-fire burst time BEFORE ClearBlindFireState
		// zeros it, then subtract from PhaseTimer so the enemy doesn't idle out the residue.
		const float UnspentBlindBurst = Mem->BlindFireBurstTimer;

		// Suppression episode ended — reset decision + clean up any active blind fire
		ClearBlindFireState(Enemy, Mem);
		Mem->bBlindFireDecided = false;
		Mem->bBlindFireChosen = false;

		if (UnspentBlindBurst > 0.f)
			Mem->PhaseTimer = FMath::Max(0.f, Mem->PhaseTimer - UnspentBlindBurst);
	}
	Mem->bWasSuppressedLastTick = bSuppressed;

	// --- Handle Pursuing phase ---
	if (Mem->Phase == EFireTaskPhase::Pursuing)
	{
		if (Awareness != EEnemyAwarenessState::Combat)
		{
			Controller->StopMovement();
			// Full teardown — without it a latched cover pose (and its wall-facing focal point)
			// leaks into the non-combat branches and montage-slides the enemy indefinitely.
			StopFireAndCleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		// Morale dropped while pursuing — abort pursue, hold position (cooldown-gated reseek).
		if (!bAggressive)
		{
			Controller->StopMovement();
			const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (!bHasCover)
			{
				const float Now = TickNow;
				if ((Now - Mem->LastReseekCoverTime) >= SuppressedReseekCooldown)
				{
					Mem->LastReseekCoverTime = Now;
					TryReseekCover(OwnerComp, Mem, Controller, Pawn, Enemy, Target, DA, false);
				}
			}

			if (Mem->Phase != EFireTaskPhase::SeekingCover)
			{
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
				Mem->Phase = EFireTaskPhase::Pause;
				Mem->PauseDuration = RollPauseDuration(DA, bHunkered);
				Mem->PhaseTimer = Mem->PauseDuration;
			}
			return;
		}

		// Check if we regained LOS + range — resume firing.
		if (bHasLOS && bInRange)
		{
			Controller->StopMovement();
			Enemy->SetAimTarget(Target);
			Controller->SetFocus(Target);
			Mem->Phase = EFireTaskPhase::Acquire;
			Mem->PhaseTimer = DA->ReactionDelay * 0.5f; // halved reaction on re-engage
			return;
		}

		// Keep pursuing — update the move target if target has moved.
		const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		const FVector PursueTarget = LastKnown.IsNearlyZero() ? Target->GetActorLocation() : LastKnown;
		if (Controller->GetMoveStatus() != EPathFollowingStatus::Moving)
			Controller->MoveToLocation(PursueTarget, 100.f, false, true, false, true);
		return;
	}

	// --- Handle SeekingCover phase (Bug 1 fix) ---
	if (Mem->Phase == EFireTaskPhase::SeekingCover)
	{
		if (Awareness != EEnemyAwarenessState::Combat)
		{
			Controller->StopMovement();
			StopFireAndCleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		// Distance-gated arrival check (mirrors BTTask_MoveToCoverPoint). 2D — the arrival pos
		// sits on the cover's Z plane, not at capsule-centre height; with the tightened radii a
		// 3D check would eat most of the budget in vertical offset and mis-report "unreachable".
		const EPathFollowingStatus::Type SeekStatus = Controller->GetMoveStatus();
		const float DistToSlot = FVector::Dist2D(Pawn->GetActorLocation(), Mem->ReseekArrivalPos);
		const bool bArrived = (DistToSlot <= SeekCoverArrivalTickRadius)
			|| (SeekStatus == EPathFollowingStatus::Idle && DistToSlot <= SeekCoverArrivalIdleRadius);

		// Stall detection: recover if the relocate/reseek move stops closing on the cover (pinned in the open).
		bool bSeekStalled = false;
		if (!bArrived && IsValid(DA))
		{
			if (DistToSlot + DA->CoverMoveStallProgressEpsilon < Mem->SeekStallBestDist)
			{
				Mem->SeekStallBestDist = DistToSlot;
				Mem->SeekStallAccum = 0.f;
			}
			else
			{
				Mem->SeekStallAccum += DeltaSeconds;
				bSeekStalled = (Mem->SeekStallAccum >= DA->CoverMoveStallTimeout);
			}
		}

		if (bArrived)
		{
			BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);

			// Read cover data for crouch check — try fresh from CoverSystem, fall back to snapshot
			FCoverData ArrivalData = Mem->ReseekCoverData;
			ACoverSystem* CoverSys = Mem->CachedCoverSys.Get();
			if (CoverSys)
			{
				FCoverData FreshData;
				if (CoverSys->GetCoverData(Mem->ReseekCover, FreshData))
					ArrivalData = FreshData;
			}

			const bool bArrivalCrouched = UCoverGeometryStatics::GetCoverHeight(ArrivalData) == ECoverHeight::Crouch;
			if (ACharacter* Char = Cast<ACharacter>(Pawn))
			{
				if (bArrivalCrouched) Char->Crouch();
				else Char->UnCrouch();
			}

			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StopFiring();

			// Pose component: settled in cover. Clear any in-transit moving state, then set
			// lean side first (gap-tested corner) so the anim rise-edge picks the wall-correct idle.
			UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
			if (IsValid(PoseComp))
			{
				PoseComp->SetCoverMoving(false, ECoverLean::None);
				if (IsValid(Target))
				{
					const FVector ThreatLoc = GetPerceivedThreatLoc(Controller, Target);
					ECoverLean IdleSide = UCoverGeometryStatics::ChooseGapPeekSide(TickWorld, ArrivalData,
						bArrivalCrouched, ThreatLoc, Target, Pawn);
					if (IdleSide == ECoverLean::None)
						IdleSide = UCoverGeometryStatics::ResolveLeanSide(
							ArrivalData, bArrivalCrouched, ThreatLoc);
					PoseComp->SetLean(IdleSide);
				}
				const ECoverHeight Height = bArrivalCrouched ? ECoverHeight::Crouch : ECoverHeight::Stand;
				PoseComp->SetInCover(true, Height);
			}

			// Part B: reset dwell tracking on fresh arrival.
			// Restore cover-move speed and clear facing lock.
			RestoreCoverMoveSpeed(Enemy, Mem);
			Mem->bCoverMoveFacingActive = false;
			Mem->bArrivedAtSlot = false;
			Mem->SlotDwellTime = 0.f;
			Mem->CompromiseConsecutiveCount = 0;
			Mem->CompromiseEvalTimer = 0.f;
			Mem->bRelocatePending = false;
			Mem->RelocatePendingSetTime = 0.f;
			Mem->ExposeLosTimeoutCount = 0;
			Mem->PeekCyclesAtCover = 0;
			Mem->bSidePeekHopTried = false;
			Mem->bLadderForceOppositeSide = false;
			Mem->bLadderForceOverTop = false;
			Mem->LadderOppositeSide = ECoverLean::None;
			// Ladder swap-move arrival: continue at stage 2 instead of resetting.
			if (Mem->bLadderSwapMovePending)
			{
				Mem->LadderStage = 2;
				Mem->ExposeLosTimeoutCount = 0;
				Mem->bLadderSwapMovePending = false;
			}
			else
			{
				Mem->LadderStage = 0;
				Mem->bLadderSwapMovePending = false;
			}
			Mem->bShufflePending = false;
			Mem->ShuffleHoldTimer = 0.f;
			Mem->SeekStallBestDist = TNumericLimits<float>::Max(); Mem->SeekStallAccum = 0.f;
			Mem->CoverMoveArrivalTime = TickNow;

			if (GetCoverAnimLogLevel() > 0)
				UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s ARRIVE dist=%.0f coverLoc=(%.0f,%.0f,%.0f)"),
					*GetNameSafe(Pawn),
					FVector::Dist2D(Pawn->GetActorLocation(), Mem->ReseekArrivalPos),
					ArrivalData.Location.X, ArrivalData.Location.Y, ArrivalData.Location.Z);

			Enemy->SetAimTarget(Target);
			// Wall-aligned facing while posed — target-tracking yaw would spin the montage pose.
			ApplyCoverFacing(Controller, Pawn, ArrivalData);
			Mem->Phase = EFireTaskPhase::Acquire;
			Mem->PhaseTimer = DA->ReactionDelay * 0.5f;
			return;
		}

		// Path failed (idle but still far from cover) — cover unreachable. Also fires on stall.
		if (SeekStatus == EPathFollowingStatus::Idle || bSeekStalled)
		{
			if (bSeekStalled)
			{
				if (Controller) Controller->StopMovement();
				UE_LOG(LogEnemyAI, Log, TEXT("[COVER] %s stalled relocating to cover (dist=%.0f) — re-seeking"), *Pawn->GetName(), DistToSlot);
			}

			// Vacate the unreachable cover and hold position standing, re-seek next loop.
			RestoreCoverMoveSpeed(Enemy, Mem);
			Mem->bCoverMoveFacingActive = false;
			Mem->LadderStage = 0;
			Mem->bLadderSwapMovePending = false;
			UCoverReservationSubsystem* ResSub = Mem->CachedResSub.Get();
			if (IsValid(ResSub) && Mem->ReseekCover.IsValid())
			{
				ResSub->MarkVacated(Mem->ReseekCover, Controller);
				ResSub->ClearIntendedCover(Controller);
			}
			Mem->ReseekCover = FCoverHandle();
			Mem->ReseekCoverData = FCoverData();

			ClearCoverBB(BB);
			BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);

			// Reset cover pose + clear moving state
			UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
			if (IsValid(PoseComp))
			{
				PoseComp->SetCoverMoving(false, ECoverLean::None);
				PoseComp->ResetCoverPose();
			}

			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();

			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StopFiring();

			Enemy->SetAimTarget(Target);
			Controller->ClearFocus(EAIFocusPriority::Move);
			Controller->SetFocus(Target);
			Mem->Phase = EFireTaskPhase::Pause;
			Mem->PauseDuration = RollPauseDuration(DA, bHunkered);
			Mem->PhaseTimer = Mem->PauseDuration;
			return;
		}

		// Continue moving toward cover. Fire while moving (move-and-shoot).
		Enemy->SetAimTarget(Target);
		// Re-assert firing if the weapon auto-stopped mid-transit (e.g. suppression spike).
		if (bHasLOS && bInRange)
		{
			AWeaponBase* W = Enemy->GetCurrentWeapon();
			if (IsValid(W) && W->CanFire() && !W->IsFiring()) W->StartFiring();
		}
		return;
	}

	// --- Exposed-in-combat cover reseek ---
	// enemy.ForceCover: aggressive reseek regardless of morale (short cooldown). Natural combat:
	// Confident enemies also try to claim available cover on the DA cooldown — without this only
	// Shaken/Broken morale ever reseeks, so aggressive enemies fight whole engagements standing
	// in the open once the initial BT cover branch comes up empty.
	{
		const float ReseekCooldown = (GetForceCoverLevel() > 0)
			? ForceCoverReseekCooldown
			: (DA->bCombatReseeksCover ? DA->CombatReseekCooldown : -1.f);
		if (ReseekCooldown > 0.f
			&& !BB->GetValueAsBool(AEnemyAIController::BB_HasCover)
			&& Mem->Phase != EFireTaskPhase::SeekingCover && Mem->Phase != EFireTaskPhase::Pursuing
			&& (TickNow - Mem->LastReseekCoverTime) >= ReseekCooldown)
		{
			Mem->LastReseekCoverTime = TickNow;
			TryReseekCover(OwnerComp, Mem, Controller, Pawn, Enemy, Target, DA, bHasLOS);
			if (Mem->Phase == EFireTaskPhase::SeekingCover) return;
		}
	}

	Mem->PhaseTimer -= DeltaSeconds;

	// --- Blind-fire burst tick (runs in Pause phase while bBlindFiringNow) ---
	if (Mem->bBlindFiringNow && Mem->Phase == EFireTaskPhase::Pause)
	{
		Mem->BlindFireBurstTimer -= DeltaSeconds;
		if (Mem->BlindFireBurstTimer <= 0.f)
		{
			// Blind-fire burst finished — clean up, remain in Pause for the remaining pause duration
			ClearBlindFireState(Enemy, Mem);
			// PhaseTimer already holds the remaining pause time set when entering blind fire
		}
	}

	// --- Part B: flank-break / cover-compromise logic + open-ground reposition ---
	// enemy.ForceCover pins the enemy at its point — compromise/relocate logic off.
	if (DA->bCoverFlankBreakEnabled && IsValid(Target) && GetForceCoverLevel() == 0)
	{
		const bool bHasCoverNow = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);

		// Read current cover from the AICS CoverTarget BB key.
		// Baked cover geometry never changes — use the BB snapshot directly (PERF #9).
		FCover CurCover;
		FCoverData CurCoverData;
		bool bHasCoverObj = false;
		if (bHasCoverNow)
		{
			CurCover = ReadCoverFromBB(BB);
			if (CurCover.IsValid())
			{
				bHasCoverObj = true;
				CurCoverData = CurCover.Data;
			}
			else if ((TickNow - Mem->LastMiswiringLogTime) >= 5.f)
			{
				// Fix #6: HasCover=true but CoverTarget resolved invalid — BB mis-wiring
				Mem->LastMiswiringLogTime = TickNow;
				UE_LOG(LogEnemyAI, Warning,
					TEXT("[COVER] %s has BB_HasCover=true but CoverTarget key '%s' resolved invalid — check BB wiring"),
					*Pawn->GetName(), *CoverTargetKey.SelectedKeyName.ToString());
			}
		}

		const bool bSafePhase = (Mem->Phase == EFireTaskPhase::Pause
			|| Mem->Phase == EFireTaskPhase::Recover
			|| Mem->Phase == EFireTaskPhase::Acquire);
		const AWeaponBase* WeaponCheck = Enemy->GetCurrentWeapon();
		const bool bNotFiring = !IsValid(WeaponCheck) || !WeaponCheck->IsFiring();
		const float Now = TickNow;

		if (bHasCoverObj)
		{
			// Track physical arrival at the cover point.
			if (!Mem->bArrivedAtSlot)
			{
				if (FVector::Dist2D(Pawn->GetActorLocation(), CurCoverData.Location) <= FlankSlotArrivalRadius)
				{
					Mem->bArrivedAtSlot = true;
					Mem->SlotDwellTime = 0.f;
					Mem->CompromiseConsecutiveCount = 0;
					Mem->CompromiseEvalTimer = 0.f;
				}
			}

			if (Mem->bArrivedAtSlot)
			{
				Mem->SlotDwellTime += DeltaSeconds;
				Mem->CompromiseEvalTimer += DeltaSeconds;

				const bool bDwellMet   = Mem->SlotDwellTime >= DA->CoverCompromiseMinDwell;
				const bool bEvalDue    = Mem->CompromiseEvalTimer >= DA->CoverCompromiseEvalInterval;
				const bool bCooledDown = (Now - Mem->LastRelocateCompletedTime) >= DA->CoverRelocateCooldown;

				// ~1 Hz gate-state log so we see WHY the eval doesn't fire (before the eval gate).
				if (GetFlankBreakLogLevel() > 0 && FMath::Fmod(Now, 1.0f) < DeltaSeconds)
				{
					const float CooldownRem = DA->CoverRelocateCooldown - (Now - Mem->LastRelocateCompletedTime);
					UE_LOG(LogEnemyAI, Log,
						TEXT("[FLANKDBG-GATE] %s flankEnabled=%d hasCover=%d cover=(%.0f,%.0f,%.0f) bCrouch=%d arrived=%d dwell=%.2f dwellMet=%d evalTimer=%.2f cooldownRem=%.2f cooledDown=%d phase=%d"),
						*Pawn->GetName(),
						DA->bCoverFlankBreakEnabled ? 1 : 0,
						bHasCoverNow ? 1 : 0,
						CurCoverData.Location.X, CurCoverData.Location.Y, CurCoverData.Location.Z,
						(UCoverGeometryStatics::GetCoverHeight(CurCoverData) == ECoverHeight::Crouch) ? 1 : 0,
						Mem->bArrivedAtSlot ? 1 : 0,
						Mem->SlotDwellTime,
						bDwellMet ? 1 : 0,
						Mem->CompromiseEvalTimer,
						CooldownRem,
						bCooledDown ? 1 : 0,
						static_cast<int32>(Mem->Phase));
				}

				// FIX 2 — Detection: runs every eval interval regardless of phase / firing state
				// so flank compromise is detected even mid-burst. Cooldown gates the SLOW
				// relocate decision, not the detection itself — the fast path bypasses cooldown.
				if (bDwellMet && bEvalDue)
				{
					Mem->CompromiseEvalTimer = 0.f;

					const UCapsuleComponent* Cap = Enemy->GetCapsuleComponent();
					const float Standoff = (Cap ? Cap->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + DA->CoverStandoffPadding;

					bool bOutsideArc   = false;
					bool bBodyExposed  = false;
					float AngleDeg     = 0.f;
					const bool bCompromised = IsCoverCompromised(TickWorld, CurCoverData,
						Pawn->GetActorLocation(), Target, GetPerceivedThreatLoc(Controller, Target),
						DA->CoverFlankArcHalfAngleDeg, DA->CoverFlankArcSlackDeg,
						Standoff, Pawn,
						&bOutsideArc, &bBodyExposed, &AngleDeg);

					// Multi-threat enforcement on the HELD cover: exposure to a second KNOWN hostile
					// (companion/player at its perceived position) also counts as compromised — else an
					// enemy fighting one target sits wide open to the other indefinitely. Feeds the
					// debounced SLOW path only (the instant-break fast path stays primary-only), so a
					// no-better-cover situation churns at worst once per relocate cooldown.
					bool bExtraExposed = false;
					if (!bCompromised)
					{
						TArray<FEnemyKnownThreat> EvalExtraThreats;
						UCoverScoringStatics::GatherEnemyExtraThreats(Enemy, Target, EvalExtraThreats);
						for (const FEnemyKnownThreat& Extra : EvalExtraThreats)
						{
							if (!Extra.Actor) continue;
							if (!UCoverGeometryStatics::IsThreatCovered(TickWorld, CurCoverData, Extra.Location,
								Standoff, BodyProtectChestHeight, Extra.Actor, Pawn))
							{
								bExtraExposed = true;
								break;
							}
						}
					}

					// Hostile-adjacency enforcement on the HELD cover: a living hostile standing at
					// the point (player pushing this position) compromises it — the pick-time anchor
					// reject only sampled positions once. Same radii as pick time; rides the same
					// debounce so a hostile sprinting past can't trigger a vacate.
					bool bHostileAdjacent = false;
					if (!bCompromised && (DA->MinHostileCoverDistance > 0.f || DA->MinHostilePawnDistance > 0.f))
					{
						FHostileAnchors HeldAnchors;
						UCoverScoringStatics::GatherKnownHostileAnchors(TickWorld, Enemy, Controller, HeldAnchors);
						bHostileAdjacent = UCoverScoringStatics::IsNearHostileAnchor(CurCoverData.Location,
							HeldAnchors, DA->MinHostileCoverDistance, DA->MinHostilePawnDistance);
					}

					if (bCompromised || bExtraExposed || bHostileAdjacent)
						Mem->CompromiseConsecutiveCount = FMath::Min(Mem->CompromiseConsecutiveCount + 1, CompromiseDebounceRequired);
					else
						Mem->CompromiseConsecutiveCount = FMath::Max(0, Mem->CompromiseConsecutiveCount - 1);

					if (GetFlankBreakLogLevel() > 0)
					{
						const float CooldownRemaining = DA->CoverRelocateCooldown - (Now - Mem->LastRelocateCompletedTime);
						UE_LOG(LogEnemyAI, Log,
							TEXT("[FLANKDBG] %s cover=(%.0f,%.0f,%.0f)(bCrouch=%d) angle=%.1f bOutsideArc=%d bBodyProtected=%d extraExposed=%d hostileAdj=%d consec=%d cooldownRem=%.2f phase=%d arrived=%d eLOS=%d"),
							*Pawn->GetName(),
							CurCoverData.Location.X, CurCoverData.Location.Y, CurCoverData.Location.Z,
							(UCoverGeometryStatics::GetCoverHeight(CurCoverData) == ECoverHeight::Crouch) ? 1 : 0,
							AngleDeg,
							bOutsideArc ? 1 : 0,
							bBodyExposed ? 0 : 1,
							bExtraExposed ? 1 : 0,
							bHostileAdjacent ? 1 : 0,
							Mem->CompromiseConsecutiveCount,
							CooldownRemaining,
							static_cast<int32>(Mem->Phase),
							Mem->bArrivedAtSlot ? 1 : 0,
							bEffectiveLOS ? 1 : 0);
					}

					// Fast path: instant break when compromised (flanked, or a hostile standing on
					// the cover) + enemy sees the threat.
					// Bypasses safe-phase/not-firing/cooldown/min-peek-cycles gates.
					// Anti-flicker: requires 2 consecutive positive evals (~0.8s at default cadence)
					// so a single-frame flicker (e.g. player jump crossing the arc) can't trigger a vacate.
					// Skipped when ForceCover debug pin is on, or when DA opts out.
					if ((bCompromised || bHostileAdjacent) && DA->bCoverInstantBreakWithLOS && GetForceCoverLevel() == 0
						&& Mem->CompromiseConsecutiveCount >= 2 && bEffectiveLOS)
					{
						if (GetFlankBreakLogLevel() > 0)
							UE_LOG(LogEnemyAI, Log, TEXT("[FLANKDBG] %s INSTANT BREAK (compromised + LOS)"), *Pawn->GetName());

						Mem->CompromiseConsecutiveCount = 0;
						Mem->bRelocatePending = false;

						// Stop firing if active so the pawn is clean for relocate.
						AWeaponBase* InstW = Enemy->GetCurrentWeapon();
						if (IsValid(InstW) && InstW->IsFiring()) InstW->StopFiring();

						// Reset cover pose + uncrouch so ExecuteRelocate's transit is clean.
						UCoverPoseComponent* InstPose = Enemy->GetCoverPoseComponent();
						if (IsValid(InstPose))
						{
							InstPose->SetCoverMoving(false, ECoverLean::None);
							InstPose->ResetCoverPose();
						}
						if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();

						ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
							CurCover.Handle, CurCoverData, DA, bHasLOS);
						return;
					}

					// Slow path: debounced relocate (no LOS, or LOS fast path disabled).
					// Cooldown gate only applies here — fast path bypasses it.
					if (bCooledDown && Mem->CompromiseConsecutiveCount >= CompromiseDebounceRequired)
					{
						Mem->CompromiseConsecutiveCount = 0;
						if (bSafePhase && bNotFiring && Mem->PeekCyclesAtCover >= DA->MinPeekCyclesBeforeRelocate)
						{
							Mem->bRelocatePending = false;
							ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
								CurCover.Handle, CurCoverData, DA, bHasLOS);
						}
						else
						{
							Mem->bRelocatePending = true;
							Mem->RelocatePendingSetTime = TickNow;
						}
					}
				}

				// FIX 2 — Deferred commit: execute the pending relocate at the first safe non-firing phase.
				if (Mem->bRelocatePending && bSafePhase && bNotFiring && bCooledDown
					&& Mem->PeekCyclesAtCover >= DA->MinPeekCyclesBeforeRelocate)
				{
					Mem->bRelocatePending = false;
					Mem->CompromiseConsecutiveCount = 0;
					ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
						CurCover.Handle, CurCoverData, DA, bHasLOS);
				}
			}
		}
		else
		{
			// No cover.
			Mem->bArrivedAtSlot = false;
			Mem->SlotDwellTime = 0.f;
			Mem->CompromiseConsecutiveCount = 0;

			if (GetFlankBreakLogLevel() > 0 && FMath::Fmod(Now, 1.0f) < DeltaSeconds)
			{
				UE_LOG(LogEnemyAI, Log,
					TEXT("[FLANKDBG-GATE] %s NO COVER (hasCover=%d)"),
					*Pawn->GetName(),
					bHasCoverNow ? 1 : 0);
			}

			if (bSafePhase && bNotFiring && bHasLOS)
			{
				// Prefer getting back into cover (NOT gated on suppression). Fall back to open-ground strafe.
				if ((Now - Mem->LastReseekCoverTime) >= SuppressedReseekCooldown)
				{
					Mem->LastReseekCoverTime = Now;
					if (TryReseekCover(OwnerComp, Mem, Controller, Pawn, Enemy, Target, DA, bHasLOS)) return;
				}
				if ((Now - Mem->LastRelocateCompletedTime) >= DA->OpenGroundStrafeInterval)
				{
					FVector StrafePoint;
					if (TryOpenGroundStrafe(Pawn, Target, DA->OpenGroundStrafeRadius, StrafePoint))
					{
						Controller->MoveToLocation(StrafePoint, 80.f, false, true, false, true);
						Mem->LastRelocateCompletedTime = Now;
					}
				}
			}
		}
	}

	// --- Grenadier lob (in-task) ---
	// Replaces the BT lob branch: its HasLineOfSight observer aborted this task on every tuck
	// (tucked = no LOS), un-crouching and resetting the cover pose. The trigger now lives here so
	// the grenadier stays posed: accumulate LOS-blocked time while tucked-stationary, throw from
	// the tucked pose at the DA threshold. CanThrow() covers supply/cooldown/telegraph-in-progress.
	if (UEnemyGrenadierComponent* GrenComp = Enemy->GetGrenadierComponent())
	{
		if (bHasLOS)
		{
			Mem->GrenadeLosBlockedAccum = 0.f;
		}
		else if (Mem->Phase == EFireTaskPhase::Pause || Mem->Phase == EFireTaskPhase::Acquire)
		{
			const UCoverPoseComponent* GrenPose = Enemy->GetCoverPoseComponent();
			const bool bGrenTucked = !IsValid(GrenPose) || !GrenPose->bPeeking;
			const bool bGrenStationary = !Mem->bCoverMoveFacingActive && !Mem->bShufflePending
				&& !Mem->bDriftCorrecting && !Mem->bBlindFiringNow;
			if (bGrenTucked && bGrenStationary)
			{
				Mem->GrenadeLosBlockedAccum += DeltaSeconds;
				if (Mem->GrenadeLosBlockedAccum >= DA->GrenadeLobTriggerLOSBlockedTime)
				{
					// Full window between attempts whether the throw commits or fails (range/arc/
					// zero last-known) — no per-tick arc-solve retries.
					Mem->GrenadeLosBlockedAccum = 0.f;

					const AWeaponBase* GrenWeapon = Enemy->GetCurrentWeapon();
					const bool bGrenReloading = IsValid(GrenWeapon) && GrenWeapon->IsReloading();
					const FVector GrenLastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
					if (!bGrenReloading && GrenComp->CanThrow() && !GrenLastKnown.IsNearlyZero())
						GrenComp->TryThrowAt(GrenLastKnown);
				}
			}
		}
	}

	switch (Mem->Phase)
	{
	case EFireTaskPhase::Acquire:
		if (Mem->PhaseTimer <= 0.f)
		{
			// Suppression gate: stay in cover, extend into Pause instead of exposing
			if (bSuppressed)
			{
				const bool bHasCoverAcq = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
				if (!bHasCoverAcq)
				{
					// Bug 1 fix: suppressed + no cover -> try to find cover instead of looping forever.
					const float Now = TickNow;
					if ((Now - Mem->LastReseekCoverTime) >= SuppressedReseekCooldown)
					{
						Mem->LastReseekCoverTime = Now;
						if (TryReseekCover(OwnerComp, Mem, Controller, Pawn, Enemy, Target, DA, bHasLOS)) break;
					}
					// No cover found or cooldown active — no cover, stay standing, hold in Pause.
					if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
				}

				// Blind-fire path: stay hunkered, aim at last-known, fire with extra spread.
				// Never start a blind burst mid grenade wind-up (one-handed spray over a throw).
				if (Mem->bBlindFireDecided && Mem->bBlindFireChosen && bHasCoverAcq && !Mem->bBlindFiringNow
					&& !IsGrenadeTelegraphing(Enemy))
				{
					// Fix 1: gate on non-zero last-known — a zero last-known must fall back to hide.
					const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
					if (LastKnown.IsNearlyZero())
					{
						Mem->bBlindFireChosen = false;
						// Fall through to the normal suppressed-pause path below
					}
					else
					{
						// Fix 1: null the aim target so GetAIAimTarget returns null; set the
						// aim-location override so bullets resolve against last-known (fixed)
						// rather than tracking the live target through the wall.
						Enemy->SetAimTarget(nullptr);
						Enemy->SetAimLocationOverride(LastKnown);
						Controller->SetFocalPoint(LastKnown, EAIFocusPriority::Gameplay);

						Enemy->SetExtraSpreadDegrees(DA->BlindFireExtraSpreadDeg);
						UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
						if (IsValid(PoseComp)) PoseComp->SetBlindFiring(true);

						AWeaponBase* BFWeapon = Enemy->GetCurrentWeapon();
						if (IsValid(BFWeapon) && !BFWeapon->IsReloading()) BFWeapon->StartFiring();

						Mem->bBlindFiringNow = true;
						Mem->BlindFireBurstTimer = FMath::RandRange(DA->BlindFireBurstMin, DA->BlindFireBurstMax);

						// Park in Pause — blind-fire burst ticks down via the pre-switch blind-fire tick block
						Mem->Phase = EFireTaskPhase::Pause;
						Mem->PauseDuration = Mem->BlindFireBurstTimer + RollPauseDuration(DA, bHunkered);
						Mem->PhaseTimer = Mem->PauseDuration;
						break;
					}
				}

				{
					Mem->Phase = EFireTaskPhase::Pause;
					Mem->PauseDuration = RollPauseDuration(DA, bHunkered);
					Mem->PhaseTimer = Mem->PauseDuration;
				}
				break;
			}

			// Reload gate: stay in Acquire (tucked) while reloading — no peek until the mag is seated.
			{
				AWeaponBase* ReloadWeapon = Enemy->GetCurrentWeapon();
				if (IsValid(ReloadWeapon) && ReloadWeapon->IsReloading())
				{
					Mem->PhaseTimer = FMath::RandRange(0.15f, 0.25f);
					break;
				}
			}

			// Grenade-throw gate: stay tucked while the wind-up plays — no peek until release.
			if (IsGrenadeTelegraphing(Enemy))
			{
				Mem->PhaseTimer = FMath::RandRange(0.15f, 0.25f);
				break;
			}

			// Expose: read current cover from AICS CoverTarget BB key.
			// Baked geometry never changes — use the BB snapshot directly (PERF #9).
			const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			FCover ExpCover;
			FCoverData ExpCoverData;
			bool bHasCoverObj = false;
			if (bHasCover)
			{
				ExpCover = ReadCoverFromBB(BB);
				if (ExpCover.IsValid())
				{
					bHasCoverObj = true;
					ExpCoverData = ExpCover.Data;
				}
			}

			// Peek side by baked corner flags + LOS verify — the peek montage's root motion does
			// the actual step-out and return, so no movement is issued here. No side: crouch
			// cover stands up over the top (capsule up so eye/muzzle clear the wall).
			ECoverLean PeekSide = ECoverLean::None;
			if (bHasCoverObj)
			{
				switch (GetForceCoverPeekSide())
				{
				case 1: PeekSide = ECoverLean::Left; break;
				case 2: PeekSide = ECoverLean::Right; break;
				case 3: PeekSide = ECoverLean::Front; break;
				default:
				{
					const FVector ThreatLoc = GetPerceivedThreatLoc(Controller, Target);
					const bool bExpCrouched = UCoverGeometryStatics::GetCoverHeight(ExpCoverData) == ECoverHeight::Crouch;

					// Feature 3 ladder flags — consume before normal pick.
					if (Mem->bLadderForceOppositeSide)
					{
						Mem->bLadderForceOppositeSide = false;
						PeekSide = Mem->LadderOppositeSide;
						Mem->LadderOppositeSide = ECoverLean::None;
					}
					else if (Mem->bLadderForceOverTop)
					{
						Mem->bLadderForceOverTop = false;
						PeekSide = ECoverLean::Front;
					}
					else
					{
						PeekSide = UCoverGeometryStatics::ChooseGapPeekSide(TickWorld, ExpCoverData, bExpCrouched,
							ThreatLoc, Target, Pawn);

						// Feature 4: crouch cover with a side peek — roll to stand-peek over top.
						if (bExpCrouched && PeekSide != ECoverLean::None && PeekSide != ECoverLean::Front
							&& ExpCoverData.bFrontCoverCrouched
							&& FMath::FRand() < DA->CoverEndpointStandPeekChance)
						{
							if (GetCoverAnimLogLevel() > 0)
								UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s Feature4 roll: side->over-top"), *GetNameSafe(Pawn));
							PeekSide = ECoverLean::Front;
						}

						if (PeekSide == ECoverLean::None && bExpCrouched)
							PeekSide = ECoverLean::Front;
					}
					break;
				}
				}
				if (UCoverGeometryStatics::GetCoverHeight(ExpCoverData) == ECoverHeight::Crouch && PeekSide == ECoverLean::Front)
					if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
			}

			// FORCED side testing only: walk to the wall's end point on that side before peeking.
			// Natural combat never hops — the rule is positional: a point at a wall end carries the
			// matching side flag (ChooseGapPeekSide picks it), everything mid-wall goes over-top.
			const int32 ForcedSideNow = GetForceCoverPeekSide();
			if (bHasCoverObj && !Mem->bSidePeekHopTried
				&& (ForcedSideNow == 1 || ForcedSideNow == 2)
				&& (PeekSide == ECoverLean::Left || PeekSide == ECoverLean::Right))
			{
				{
					Mem->bSidePeekHopTried = true;
					FCover HopDest = FindSidePeekCover(TickWorld, Pawn, ExpCover, PeekSide,
						GetPerceivedThreatLoc(Controller, Target), DA, Controller, Target);
					if (HopDest.IsValid())
					{
						const UCapsuleComponent* HopCap = Enemy->GetCapsuleComponent();
						const float HopCapRadius = HopCap ? HopCap->GetScaledCapsuleRadius() : DefaultCapsuleRadius;
						const float HopStandoff = HopCapRadius + DA->CoverStandoffPadding;
						const FVector HopArrival = UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
							TickWorld, HopDest.Data, HopStandoff, HopCapRadius, DA->CoverCornerGap, Pawn);

						// Issue the move FIRST and only commit the cover swap when the request is
						// accepted — committing first strands the pawn in the open when the corner
						// hunker turns out unpathable (old cover vacated, new one unreachable, both
						// on post-vacate cooldown for the reseek).
						LogCoverMove(TEXT("side-peek-hop"), Pawn, Enemy);
						const EPathFollowingRequestResult::Type HopResult =
							Controller->MoveToLocation(HopArrival, 25.f, false, true, true, true);
						if (HopResult == EPathFollowingRequestResult::RequestSuccessful
							|| HopResult == EPathFollowingRequestResult::AlreadyAtGoal)
						{
							UCoverReservationSubsystem* ResSub = Mem->CachedResSub.Get();
							if (!ResSub && TickWorld) ResSub = TickWorld->GetSubsystem<UCoverReservationSubsystem>();
							if (IsValid(ResSub))
							{
								ResSub->MarkVacated(ExpCover.Handle, Controller);
								ResSub->SetIntendedCover(Controller, HopDest.Handle);
							}
							WriteCoverToBB(BB, HopDest);

							Mem->ReseekCover = HopDest.Handle;
							Mem->ReseekCoverData = HopDest.Data;
							Mem->ReseekArrivalPos = HopArrival;
							Mem->bArrivedAtSlot = false;
							Mem->SlotDwellTime = 0.f;
							Mem->CompromiseConsecutiveCount = 0;
							Mem->CompromiseEvalTimer = 0.f;
							Mem->bRelocatePending = false;
							Mem->ExposeLosTimeoutCount = 0;
							Mem->bLadderForceOppositeSide = false;
							Mem->bLadderForceOverTop = false;
							Mem->LadderOppositeSide = ECoverLean::None;
							Mem->LadderStage = 0;
							Mem->bLadderSwapMovePending = false;
							Mem->bShufflePending = false;
							Mem->ShuffleHoldTimer = 0.f;
							Mem->SeekStallBestDist = TNumericLimits<float>::Max();
							Mem->SeekStallAccum = 0.f;

							UCoverPoseComponent* HopPose = Enemy->GetCoverPoseComponent();
							if (IsValid(HopPose)) HopPose->ResetCoverPose();
							Controller->SetFocus(Target);
							if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
							Mem->Phase = EFireTaskPhase::SeekingCover;
							UE_LOG(LogEnemyAI, Log, TEXT("[COVER] %s side-peek hop (%s) -> (%.0f,%.0f,%.0f)"),
								*Pawn->GetName(), PeekSide == ECoverLean::Left ? TEXT("L") : TEXT("R"),
								HopDest.Data.Location.X, HopDest.Data.Location.Y, HopDest.Data.Location.Z);
							break;
						}

						// Move refused (no path to the corner) — keep the current cover and peek
						// in place; bSidePeekHopTried stops a retry loop on this point.
						UE_LOG(LogEnemyAI, Log, TEXT("[COVER] %s side-peek hop (%s) move refused (result=%d) — peeking in place"),
							*Pawn->GetName(), PeekSide == ECoverLean::Left ? TEXT("L") : TEXT("R"),
							static_cast<int32>(HopResult));
					}
				}
			}

			// Pose component: entering Expose
			UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
			if (IsValid(PoseComp) && bHasCoverObj)
			{
				if (PeekSide == ECoverLean::Front)
				{
					// Over-top pop-up: always standing so the muzzle clears the low wall; wall-aligned
					// (the widened ±75° aim clamp lets the gun track the player within a forward cone).
					PoseComp->SetInCover(true, ECoverHeight::Stand);
					PoseComp->SetLean(PeekSide);
					PoseComp->SetPeeking(true);
					ApplyCoverFacing(Controller, Pawn, ExpCoverData);
				}
				else
				{
					// Corner peek: preserve existing height logic; wall-aligned facing.
					// Self-heal: a task restart or aborted relocate can reset the pose while the BB
					// cover persists — re-assert so the anim side has the idle underneath the peek.
					if (!PoseComp->bInCover)
						PoseComp->SetInCover(true, UCoverGeometryStatics::GetCoverHeight(ExpCoverData));
					PoseComp->SetLean(PeekSide);
					PoseComp->SetPeeking(true);
					// Re-anchor the back-to-cover yaw every cycle — corrects any drift a foreign focus
					// (suppress/flank branch) accumulated, right before the montage steps out.
					if (bHasCoverObj)
						ApplyCoverFacing(Controller, Pawn, ExpCoverData);
				}
			}
			else if (bHasCoverObj)
			{
				// PoseComp invalid but we still need wall-facing.
				ApplyCoverFacing(Controller, Pawn, ExpCoverData);
			}

			if (GetCoverAnimLogLevel() > 0)
				UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s EXPOSE side=%d crouch=%d cycles=%d"),
					*GetNameSafe(Pawn), static_cast<int32>(PeekSide),
					(bHasCoverObj && UCoverGeometryStatics::GetCoverHeight(ExpCoverData) == ECoverHeight::Crouch) ? 1 : 0, Mem->PeekCyclesAtCover);

			Mem->ExposeLosWaitTimer = 0.f;
			Mem->Phase = EFireTaskPhase::Expose;
			{
				const float RolledExpose = FMath::RandRange(DA->ExposePhaseMin, DA->ExposePhaseMax);
				Mem->PhaseTimer = bHunkered ? RolledExpose * HunkerExposeScale : RolledExpose;
			}
		}
		break;

	case EFireTaskPhase::Expose:
	{
		// Suppression interrupt: duck back to Recover immediately
		if (bSuppressed)
		{
			const bool bSuppCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			FCover SuppCover;
			if (bSuppCover) SuppCover = ReadCoverFromBB(BB);
			const bool bSuppCrouchHeight = SuppCover.IsValid()
				&& UCoverGeometryStatics::GetCoverHeight(SuppCover.Data) == ECoverHeight::Crouch;
			if (SuppCover.IsValid())
			{
				if (ACharacter* Char = Cast<ACharacter>(Pawn))
				{
					if (bSuppCrouchHeight) Char->Crouch();
					else Char->UnCrouch();
				}
			}

			// Pose component: leaving Expose on suppression
			UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
			if (IsValid(PoseComp))
			{
				PoseComp->SetPeeking(false);
				PoseComp->SetLean(ECoverLean::None);
				if (SuppCover.IsValid())
				{
					const ECoverHeight SuppHeight = bSuppCrouchHeight ? ECoverHeight::Crouch : ECoverHeight::Stand;
					PoseComp->SetInCover(true, SuppHeight);
					ApplyCoverFacing(Controller, Pawn, SuppCover.Data);
				}
				else
				{
					PoseComp->ResetCoverPose();
				}
			}

			Mem->Phase = EFireTaskPhase::Recover;
			Mem->PhaseTimer = FMath::RandRange(DA->RecoverPhaseMin, DA->RecoverPhaseMax);
			break;
		}
		if (Mem->PhaseTimer <= 0.f)
		{
			// Never open fire until the eye->target trace is clear AND the target is within the
			// in-cover peek cone (feature 1b). Crouch cover clears after the un-crouch.
			if (!bEffectiveLOS)
			{
				Mem->ExposeLosWaitTimer += DeltaSeconds;
				if (Mem->ExposeLosWaitTimer >= DA->ExposeLosWaitMax)
				{
					Mem->ExposeLosWaitTimer = 0.f;
					++Mem->ExposeLosTimeoutCount;

					// Feature 3: failed-peek ladder — deterministic per-cover stage machine.
					// Threshold for the CURRENT stage; advance when the in-stage counter reaches it.
					{
						int32 StageThreshold = DA->MaxExposeLosTimeouts;
						switch (Mem->LadderStage)
						{
						case 1: StageThreshold = DA->LadderOverTopTimeouts; break;
						case 2: StageThreshold = DA->LadderSecondSideTimeouts; break;
						case 3: StageThreshold = DA->LadderOverTopTimeouts; break;
						default: break; // stage 0 uses MaxExposeLosTimeouts
						}

						if (Mem->ExposeLosTimeoutCount >= StageThreshold && DA->bCoverFlankBreakEnabled)
						{
							Mem->ExposeLosTimeoutCount = 0;

							const bool bHasCoverLadder = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
							FCover LadderCover;
							FCoverData LadderCoverData;
							if (bHasCoverLadder)
							{
								LadderCover = ReadCoverFromBB(BB);
								if (LadderCover.IsValid()) LadderCoverData = LadderCover.Data;
							}

							const bool bLadderCrouched = LadderCover.IsValid()
								? UCoverGeometryStatics::GetCoverHeight(LadderCoverData) == ECoverHeight::Crouch
								: false;
							const bool bHasOverTop = bLadderCrouched && LadderCoverData.bFrontCoverCrouched;

							// --- Lambda: side-swap action (enters stage 2) ---
							// Try in-place opposite side first, then wall-walk, then relocate.
							auto DoSideSwap = [&]() -> bool
							{
								if (!LadderCover.IsValid() || !IsValid(Target)) return false;

								// Determine the currently baked side to swap FROM.
								const bool bSwCrouched = bLadderCrouched;
								const bool bSWLeft  = bSwCrouched ? LadderCoverData.bLeftCoverCrouched  : LadderCoverData.bLeftCoverStanding;
								const bool bSWRight = bSwCrouched ? LadderCoverData.bRightCoverCrouched : LadderCoverData.bRightCoverStanding;
								const bool bExactlyOne = (bSWLeft != bSWRight);

								// 1) In-place opposite side via TryOppositeEndpointSide.
								if (bExactlyOne)
								{
									const ECoverLean BakedSide = bSWLeft ? ECoverLean::Left : ECoverLean::Right;
									const ECoverLean OppSide = UCoverGeometryStatics::TryOppositeEndpointSide(TickWorld, LadderCoverData,
										bSwCrouched, GetPerceivedThreatLoc(Controller, Target), Target, Pawn, BakedSide);
									if (OppSide != ECoverLean::None)
									{
										if (GetCoverAnimLogLevel() > 0)
											UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=%d action=swap-inplace side=%d"),
												*GetNameSafe(Pawn), Mem->LadderStage, static_cast<int32>(OppSide));
										Mem->bLadderForceOppositeSide = true;
										Mem->LadderOppositeSide = OppSide;
										Mem->LadderStage = 2;
										return true; // fall through to Recover
									}
								}

								// 2) Wall-walk to opposite-side end point via FindSidePeekCover + move.
								// Determine the side we want to walk TOWARD (opposite of the current peek).
								ECoverLean WalkSide = ECoverLean::None;
								if (bExactlyOne)
									WalkSide = bSWLeft ? ECoverLean::Right : ECoverLean::Left;
								else if (bSWLeft && bSWRight)
								{
									// Both sides baked — pick the one the pawn is NOT currently on.
									const ECoverLean CurSide = UCoverGeometryStatics::ChooseGapPeekSide(TickWorld, LadderCoverData,
										bSwCrouched, GetPerceivedThreatLoc(Controller, Target), Target, Pawn);
									if (CurSide == ECoverLean::Left)  WalkSide = ECoverLean::Right;
									else if (CurSide == ECoverLean::Right) WalkSide = ECoverLean::Left;
								}

								if (WalkSide != ECoverLean::None)
								{
									if (TryLadderSwapMove(OwnerComp, Mem, Controller, Pawn, Enemy,
										LadderCover, WalkSide, Target, DA))
									{
										if (GetCoverAnimLogLevel() > 0)
											UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=%d action=swap-move side=%d"),
												*GetNameSafe(Pawn), Mem->LadderStage, static_cast<int32>(WalkSide));
										return true; // move in flight — break below
									}
								}

								// 3) Neither possible — relocate (or ForceCover-skip + reset).
								if (GetForceCoverLevel() != 0)
								{
									if (GetCoverAnimLogLevel() > 0)
										UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=%d action=relocate skipped (ForceCover)"),
											*GetNameSafe(Pawn), Mem->LadderStage);
									Mem->LadderStage = 0;
									Mem->ExposeLosTimeoutCount = 0;
									return true; // fall through to Recover
								}
								if (GetCoverAnimLogLevel() > 0)
									UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=%d action=relocate"),
										*GetNameSafe(Pawn), Mem->LadderStage);
								ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
									LadderCover.Handle, LadderCoverData, DA, bHasLOS);
								return false; // break handled by caller (relocate already changes phase)
							};

							// --- Stage machine: evaluate the current stage's advance action ---
							bool bLadderHandled = false;
							bool bLadderBreak = false; // true = must break out of the Expose switch

							if (bLadderCrouched)
							{
								// CROUCH cover: stages 0→1(overtop)→2(second side)→3(overtop)→relocate.
								switch (Mem->LadderStage)
								{
								case 0:
									if (bHasOverTop)
									{
										if (GetCoverAnimLogLevel() > 0)
											UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=0 action=overtop"), *GetNameSafe(Pawn));
										Mem->bLadderForceOverTop = true;
										Mem->LadderStage = 1;
										bLadderHandled = true;
									}
									else
									{
										// No over-top: go straight to side-swap.
										bLadderHandled = DoSideSwap();
										// Break if phase changed (move or relocate); stay if nothing was possible.
										if (Mem->Phase != EFireTaskPhase::Expose) bLadderBreak = true;
									}
									break;
								case 1:
									// Over-top A done → side-swap action.
									bLadderHandled = DoSideSwap();
									// Break if phase changed (move or relocate); stay if nothing was possible.
									if (Mem->Phase != EFireTaskPhase::Expose)
										bLadderBreak = true;
									break;
								case 2:
									if (bHasOverTop)
									{
										if (GetCoverAnimLogLevel() > 0)
											UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=2 action=overtop"), *GetNameSafe(Pawn));
										Mem->bLadderForceOverTop = true;
										Mem->LadderStage = 3;
										bLadderHandled = true;
									}
									else
									{
										// No over-top: relocate directly.
										if (GetForceCoverLevel() != 0)
										{
											if (GetCoverAnimLogLevel() > 0)
												UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=2 action=relocate skipped (ForceCover)"), *GetNameSafe(Pawn));
											Mem->LadderStage = 0;
											Mem->ExposeLosTimeoutCount = 0;
											bLadderHandled = true;
										}
										else
										{
											if (GetCoverAnimLogLevel() > 0)
												UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=2 action=relocate"), *GetNameSafe(Pawn));
											ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
												LadderCover.Handle, LadderCoverData, DA, bHasLOS);
											bLadderBreak = true;
										}
									}
									break;
								case 3:
									// Over-top B done → relocate.
									if (GetForceCoverLevel() != 0)
									{
										if (GetCoverAnimLogLevel() > 0)
											UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=3 action=relocate skipped (ForceCover)"), *GetNameSafe(Pawn));
										Mem->LadderStage = 0;
										Mem->ExposeLosTimeoutCount = 0;
										bLadderHandled = true;
									}
									else
									{
										if (GetCoverAnimLogLevel() > 0)
											UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=3 action=relocate"), *GetNameSafe(Pawn));
										ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
											LadderCover.Handle, LadderCoverData, DA, bHasLOS);
										bLadderBreak = true;
									}
									break;
								default:
									break;
								}
							}
							else
							{
								// STAND cover: no over-top stages. Stage 0→swap→stage 2→relocate.
								switch (Mem->LadderStage)
								{
								case 0:
									bLadderHandled = DoSideSwap();
									// Break if phase changed (move or relocate); stay if nothing was possible.
									if (Mem->Phase != EFireTaskPhase::Expose)
										bLadderBreak = true;
									break;
								case 2:
									if (GetForceCoverLevel() != 0)
									{
										if (GetCoverAnimLogLevel() > 0)
											UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=2 action=relocate skipped (ForceCover)"), *GetNameSafe(Pawn));
										Mem->LadderStage = 0;
										Mem->ExposeLosTimeoutCount = 0;
										bLadderHandled = true;
									}
									else
									{
										if (GetCoverAnimLogLevel() > 0)
											UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s LADDER stage=2 action=relocate"), *GetNameSafe(Pawn));
										ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
											LadderCover.Handle, LadderCoverData, DA, bHasLOS);
										bLadderBreak = true;
									}
									break;
								default:
									break;
								}
							}

							if (bLadderBreak)
								break;
							// Ladder set flags — fall through to the Recover tuck below
							// so the next Expose cycle consumes the forced side/overtop immediately.
						}
					}

					// Normal timeout (or ladder step 2/2b) — recover and re-loop.
					// Tuck back in: re-crouch if cover is crouch height, else ensure uncrouched.
					const bool bTimeoutCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
					FCover TimeoutCover;
					bool bTimeoutCrouchHeight = false;
					if (bTimeoutCover)
					{
						TimeoutCover = ReadCoverFromBB(BB);
						bTimeoutCrouchHeight = TimeoutCover.IsValid()
							&& UCoverGeometryStatics::GetCoverHeight(TimeoutCover.Data) == ECoverHeight::Crouch;
						if (TimeoutCover.IsValid())
						{
							if (ACharacter* Char = Cast<ACharacter>(Pawn))
							{
								if (bTimeoutCrouchHeight) Char->Crouch();
								else Char->UnCrouch();
							}
						}
					}

					// Pose component: leaving Expose on timeout
					UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
					if (IsValid(PoseComp))
					{
						PoseComp->SetPeeking(false);
						PoseComp->SetLean(ECoverLean::None);
						if (TimeoutCover.IsValid())
						{
							const ECoverHeight TimeoutHeight = bTimeoutCrouchHeight ? ECoverHeight::Crouch : ECoverHeight::Stand;
							PoseComp->SetInCover(true, TimeoutHeight);
							ApplyCoverFacing(Controller, Pawn, TimeoutCover.Data);
						}
						else
						{
							PoseComp->ResetCoverPose();
						}
					}

					Mem->Phase = EFireTaskPhase::Recover;
					Mem->PhaseTimer = FMath::RandRange(DA->RecoverPhaseMin, DA->RecoverPhaseMax);
				}
				break; // hold in Expose (PhaseTimer stays <= 0, re-enters here next tick)
			}

			Mem->ExposeLosWaitTimer = 0.f;
			Mem->ExposeLosTimeoutCount = 0; // Contact regained — ladder starts over.
			Mem->LadderStage = 0;
			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StartFiring();

			Mem->NoLosGraceTimer = 0.f;
			{
				const float BurstScale = bHunkered ? HunkerBurstScale : 1.f;
				Mem->BurstDuration = FMath::RandRange(DA->BurstDurationMin * BurstScale, DA->BurstDurationMax * BurstScale);
			}
			Mem->Phase = EFireTaskPhase::Fire;
			Mem->PhaseTimer = Mem->BurstDuration;
		}
		break;
	}

	case EFireTaskPhase::Fire:
		// LOS hysteresis: lost effective LOS mid-burst stops fire only after a grace window.
		// Feature 1b: uses bEffectiveLOS (cone-gated) — same gate as Expose entry and fire start.
		if (!bEffectiveLOS)
		{
			Mem->NoLosGraceTimer += DeltaSeconds;
			if (Mem->NoLosGraceTimer >= DA->FireLosLostGrace)
			{
				AWeaponBase* W = Enemy->GetCurrentWeapon();
				if (IsValid(W) && W->IsFiring()) W->StopFiring();
			}
		}
		else
		{
			Mem->NoLosGraceTimer = 0.f;
			AWeaponBase* W = Enemy->GetCurrentWeapon();
			if (IsValid(W) && !W->IsFiring()) W->StartFiring();
		}

		// Suppression interrupt: stop firing and duck back
		if (bSuppressed)
		{
			AWeaponBase* SuppWeapon = Enemy->GetCurrentWeapon();
			if (IsValid(SuppWeapon))
				SuppWeapon->StopFiring();

			const bool bSuppCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			FCover SuppCover;
			bool bSuppCrouchHeight = false;
			if (bSuppCover)
			{
				SuppCover = ReadCoverFromBB(BB);
				bSuppCrouchHeight = SuppCover.IsValid()
					&& UCoverGeometryStatics::GetCoverHeight(SuppCover.Data) == ECoverHeight::Crouch;
				if (SuppCover.IsValid())
				{
					if (ACharacter* Char = Cast<ACharacter>(Pawn))
					{
						if (bSuppCrouchHeight) Char->Crouch();
						else Char->UnCrouch();
					}
				}
			}

			// Pose component: leaving Expose on suppression during Fire
			UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
			if (IsValid(PoseComp))
			{
				PoseComp->SetPeeking(false);
				PoseComp->SetLean(ECoverLean::None);
				if (SuppCover.IsValid())
				{
					const ECoverHeight SuppHeight = bSuppCrouchHeight ? ECoverHeight::Crouch : ECoverHeight::Stand;
					PoseComp->SetInCover(true, SuppHeight);
					ApplyCoverFacing(Controller, Pawn, SuppCover.Data);
				}
				else
				{
					PoseComp->ResetCoverPose();
				}
			}

			Mem->Phase = EFireTaskPhase::Recover;
			Mem->PhaseTimer = FMath::RandRange(DA->RecoverPhaseMin, DA->RecoverPhaseMax);
			break;
		}
		if (Mem->PhaseTimer <= 0.f)
		{
			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon))
				Weapon->StopFiring();

			// Recover: re-crouch if cover is crouch height (the peek montage's Return section
			// brings the body home — no step-back move needed); else ensure uncrouched and
			// re-face the wall so stand covers don't drift onto the walk-in yaw.
			const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			FCover RecCover;
			bool bRecCrouchHeight = false;
			if (bHasCover)
			{
				RecCover = ReadCoverFromBB(BB);
				bRecCrouchHeight = RecCover.IsValid()
					&& UCoverGeometryStatics::GetCoverHeight(RecCover.Data) == ECoverHeight::Crouch;
				if (RecCover.IsValid())
				{
					if (ACharacter* Char = Cast<ACharacter>(Pawn))
					{
						if (bRecCrouchHeight) Char->Crouch();
						else Char->UnCrouch();
					}
				}

				// One full peek-fire cycle completed at this cover — relocate/shuffle unlock.
				++Mem->PeekCyclesAtCover;
				if (GetCoverAnimLogLevel() > 0)
					UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s RECOVER cycles=%d"),
						*GetNameSafe(Pawn), Mem->PeekCyclesAtCover);
			}

			// Pose component: entering Recover
			UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
			if (IsValid(PoseComp))
			{
				PoseComp->SetPeeking(false);
				PoseComp->SetLean(ECoverLean::None);
				if (RecCover.IsValid())
				{
					const ECoverHeight RecHeight = bRecCrouchHeight ? ECoverHeight::Crouch : ECoverHeight::Stand;
					PoseComp->SetInCover(true, RecHeight);
					ApplyCoverFacing(Controller, Pawn, RecCover.Data);
				}
				else
				{
					PoseComp->ResetCoverPose();
				}
			}

			Mem->Phase = EFireTaskPhase::Recover;
			Mem->PhaseTimer = FMath::RandRange(DA->RecoverPhaseMin, DA->RecoverPhaseMax);
		}
		break;

	case EFireTaskPhase::Recover:
		if (Mem->PhaseTimer <= 0.f)
		{
			Mem->PauseDuration = RollPauseDuration(DA, bHunkered);
			Mem->Phase = EFireTaskPhase::Pause;
			Mem->PhaseTimer = Mem->PauseDuration;
		}
		break;

	case EFireTaskPhase::Pause:
		if (Mem->PhaseTimer <= 0.f && !Mem->bBlindFiringNow)
		{
			// --- Pop-up cover lob: hold the exposed pose through the wind-up, duck back on release ---
			// Runs before the generic telegraph hold so we own the pop-up's duck-back. The pose was set
			// stand+front+peek when the throw committed; keep it until the grenade leaves, then re-tuck.
			if (Mem->bGrenadeLobPopUp)
			{
				if (IsGrenadeTelegraphing(Enemy))
				{
					Mem->PhaseTimer = 0.1f;
					break;
				}
				// Released (or cancelled): duck back into cover and settle before the next peek.
				if (UCoverPoseComponent* PopPose = Enemy->GetCoverPoseComponent())
				{
					PopPose->SetPeeking(false);
					PopPose->SetLean(ECoverLean::None);
					PopPose->SetInCover(true, ECoverHeight::Crouch);
				}
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
				Mem->bGrenadeLobPopUp = false;
				Mem->Phase = EFireTaskPhase::Recover;
				Mem->PhaseTimer = FMath::RandRange(DA->RecoverPhaseMin, DA->RecoverPhaseMax);
				break;
			}

			// Hold the pause while a grenade wind-up plays — no shuffle/relocate/peek mid-throw.
			if (IsGrenadeTelegraphing(Enemy))
			{
				Mem->PhaseTimer = 0.25f;
				break;
			}

			// --- Grenadier proactive cover lob (chance-based, over the top) ---
			// Fires during live engagement — independent of the LOS-blocked hiding lob above — when the
			// enemy holds crouch cover with an over-the-top firing side. CanThrow()/cooldown/supply
			// throttle the rate; the shared CanThrow() gate means it never double-throws with the hiding
			// lob. Presentation splits tucked (crouch montage, stays hunkered) vs pop-up (stands over the
			// wall, stand montage, ducks back) via the DA fields.
			if (UEnemyGrenadierComponent* LobComp = Enemy->GetGrenadierComponent())
			{
				const bool bHasCoverLob = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
				const AWeaponBase* LobWeapon = Enemy->GetCurrentWeapon();
				const bool bLobReloading = IsValid(LobWeapon) && LobWeapon->IsReloading();
				if (bHasCoverLob && !bSuppressed && !bLobReloading && IsValid(Target)
					&& LobComp->CanThrow() && DA->GrenadeCoverLobChance > 0.f)
				{
					const FCover LobCover = ReadCoverFromBB(BB);
					const bool bCrouchOverTop = LobCover.IsValid()
						&& UCoverGeometryStatics::GetCoverHeight(LobCover.Data) == ECoverHeight::Crouch
						&& LobCover.Data.bFrontCoverCrouched;
					if (bCrouchOverTop)
					{
						const FVector LobTarget = GetPerceivedThreatLoc(Controller, Target);
						const float LobDistSq = FVector::DistSquared2D(Pawn->GetActorLocation(), LobTarget);
						// Cheap range pre-check (TryThrowAt re-checks from the socket) — skips the pose
						// churn of committing a pop-up only for the throw to fail out-of-range.
						const bool bInLobRange = !LobTarget.IsNearlyZero()
							&& LobDistSq >= FMath::Square(DA->GrenadeMinRange)
							&& LobDistSq <= FMath::Square(DA->GrenadeMaxRange);
						if (bInLobRange && FMath::FRand() < DA->GrenadeCoverLobChance)
						{
							if (FMath::FRand() < DA->GrenadeCoverLobPopUpChance)
							{
								// Pop up over the wall: stand + front + peek so the stand throw montage
								// plays and the grenade clears the low cover. Pose is set BEFORE TryThrowAt
								// because the telegraph → montage-select chain is synchronous and reads the
								// cover pose height. bGrenadeLobPopUp holds it and drives the duck-back.
								UCoverPoseComponent* LobPose = Enemy->GetCoverPoseComponent();
								if (IsValid(LobPose))
								{
									LobPose->SetInCover(true, ECoverHeight::Stand);
									LobPose->SetLean(ECoverLean::Front);
									LobPose->SetPeeking(true);
								}
								if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
								ApplyCoverFacing(Controller, Pawn, LobCover.Data);

								if (LobComp->TryThrowAt(LobTarget))
								{
									Mem->bGrenadeLobPopUp = true;
									Mem->PhaseTimer = 0.1f;
									break;
								}

								// Throw refused (arc unsolvable) — revert the pop-up pose, fall through.
								if (IsValid(LobPose))
								{
									LobPose->SetPeeking(false);
									LobPose->SetLean(ECoverLean::None);
									LobPose->SetInCover(true, ECoverHeight::Crouch);
								}
								if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
							}
							else if (LobComp->TryThrowAt(LobTarget))
							{
								// Tucked lob: stay hunkered. The generic telegraph hold above keeps the
								// pause and blocks peeks until release; the crouch pose selects the crouch
								// throw montage, and the grenade still arcs over the low wall.
								Mem->PhaseTimer = 0.25f;
								break;
							}
						}
					}
				}
			}

			// --- Cover shuffle roll (Feature B) ---
			// Fix 3: skip shuffle when a flank relocate is pending — shuffle moves same-wall
			// (still flanked) and resets the debounce, delaying the legitimate relocate.
			const int32 ForceRepo = GetForceCoverRepositionLevel();

			// Mode 2: forced full relocate — stop firing, read cover, relocate.
			if (ForceRepo == 2 && !Mem->bRelocatePending)
			{
				const bool bHasCoverForce = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
				if (bHasCoverForce)
				{
					FCover ForceCover = ReadCoverFromBB(BB);
					if (ForceCover.IsValid() && IsValid(Target))
					{
						AWeaponBase* W = Enemy->GetCurrentWeapon();
						if (IsValid(W) && W->IsFiring()) W->StopFiring();
						if (GetCoverAnimLogLevel() > 0)
							UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s FORCED reposition (mode=%d)"), *GetNameSafe(Pawn), ForceRepo);
						ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
							ForceCover.Handle, ForceCover.Data, DA, bHasLOS);
						break;
					}
				}
			}

			// --- DBNO standoff retreat (player downed, enemy inside the ring -> vacate and re-seek;
			// the FallBack posture bias + ring avoid-penalty steer the re-seek outside the standoff).
			// Cover validated BEFORE consuming so a bad BB read doesn't burn the one-shot + cooldown. ---
			if (DA->DBNOStandoffRadius > 0.f && !Mem->bRelocatePending && !bSuppressed
				&& GetForceCoverLevel() == 0 && GetForceCoverRepositionLevel() == 0 && IsValid(Target))
			{
				UEnemyPostureComponent* RetreatPosture = Enemy->GetPostureComponent();
				if (IsValid(RetreatPosture) && RetreatPosture->GetPosture() == EEnemyPosture::FallBack
					&& RetreatPosture->HasRetreatRequest())
				{
					const bool bHasCoverRet = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
					const FCover RetCurCover = bHasCoverRet ? ReadCoverFromBB(BB) : FCover();
					if (RetCurCover.IsValid() && RetreatPosture->ConsumeRetreatRequest())
					{
						AWeaponBase* RetWeapon = Enemy->GetCurrentWeapon();
						if (IsValid(RetWeapon) && RetWeapon->IsFiring()) RetWeapon->StopFiring();
						ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
							RetCurCover.Handle, RetCurCover.Data, DA, bHasLOS);
						break;
					}
				}
			}

			// --- Posture advance (Press held long enough -> proactively take closer cover) ---
			// Same safety gates as shuffle: clean Pause, not suppressed, no pending flank relocate,
			// at least one full peek cycle done here. NotifyAdvanceExecuted only on commit, so a
			// rejected candidate doesn't burn the advance cooldown. Squad window staggers advances
			// to one member at a time; an unclaimed window leaves the request latched for later.
			if (DA->bPostureSystemEnabled && !Mem->bRelocatePending && !bSuppressed
				&& GetForceCoverLevel() == 0 && GetForceCoverRepositionLevel() == 0
				&& Mem->PeekCyclesAtCover >= DA->MinPeekCyclesBeforeRelocate && IsValid(Target))
			{
				UEnemySquad* Squad = Enemy->GetSquad();
				UEnemyPostureComponent* Posture = Enemy->GetPostureComponent();
				if (IsValid(Posture) && Posture->GetPosture() == EEnemyPosture::Press
					&& (!IsValid(Squad) || Squad->CanClaimAdvanceWindow())
					&& Posture->ConsumeAdvanceRequest())
				{
					const bool bHasCoverAdv = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
					const FCover AdvCurCover = bHasCoverAdv ? ReadCoverFromBB(BB) : FCover();
					if (AdvCurCover.IsValid()
						&& TryAdvanceRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
							AdvCurCover, DA, bHasLOS))
					{
						Posture->NotifyAdvanceExecuted();
						if (IsValid(Squad)) Squad->RecordAdvance();
						break;
					}
				}
			}

			if ((ForceRepo == 1 || (DA->CoverShuffleWeight > 0.f && !bSuppressed
				&& GetForceCoverLevel() == 0
				&& Mem->PeekCyclesAtCover >= DA->MinPeekCyclesBeforeRelocate))
				&& !Mem->bRelocatePending)
			{
				const bool bHasCoverPause = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
				// Read cover once for both impulse check and shuffle search.
				FCover PauseCover = bHasCoverPause ? ReadCoverFromBB(BB) : FCover();
				// Angle-relocate impulse: boost effective shuffle weight when current point
				// lacks a threat-facing side flag (favors moving to a better-angled point).
				// The boosted roll only commits if the result has a threat-facing flag (prevents
				// permanent churn on flag-less walls with no flagged neighbours).
				float EffectiveShuffleWeight = DA->CoverShuffleWeight;
				bool bBoostedRoll = false;
				if (bHasCoverPause && IsValid(Target) && PauseCover.IsValid())
				{
					const bool bCurHasFlag = UCoverGeometryStatics::HasThreatFacingSideFlag(
						PauseCover.Data, GetPerceivedThreatLoc(Controller, Target),
						UCoverGeometryStatics::GetCoverHeight(PauseCover.Data) == ECoverHeight::Crouch);
					if (!bCurHasFlag)
					{
						EffectiveShuffleWeight = FMath::Min(EffectiveShuffleWeight * DA->CoverAngleShuffleMultiplier, 1.f);
						bBoostedRoll = true;
					}
				}
				if (bHasCoverPause && (ForceRepo == 1 || FMath::FRand() < EffectiveShuffleWeight))
				{
					if (PauseCover.IsValid() && IsValid(Target))
					{
						const FVector ThreatLoc = GetPerceivedThreatLoc(Controller, Target);
						const bool bRelaxedSearch = (ForceRepo == 1);
						FCover ShuffleDest = FindShuffleCover(TickWorld, Pawn, PauseCover,
							ThreatLoc, DA, Controller, Target, bRelaxedSearch);
						// Discard boosted-roll shuffles to candidates that lack a
						// threat-facing flag — the boost exists to reach flagged points,
						// not to churn on flag-less walls.
						const bool bShuffleValid = ShuffleDest.IsValid()
							&& !(bBoostedRoll && ForceRepo != 1 && !UCoverGeometryStatics::HasThreatFacingSideFlag(
								ShuffleDest.Data, ThreatLoc,
								UCoverGeometryStatics::GetCoverHeight(ShuffleDest.Data) == ECoverHeight::Crouch));
						if (bShuffleValid)
						{
							if (GetCoverAnimLogLevel() > 0 && ForceRepo == 1)
								UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s FORCED reposition (mode=%d)"), *GetNameSafe(Pawn), ForceRepo);
							if (CommitSameWallShuffle(OwnerComp, Mem, Controller, Pawn, Enemy,
								PauseCover, ShuffleDest, Target, DA))
								break;
						}
						else if (ForceRepo == 1 && GetCoverAnimLogLevel() > 0)
						{
							UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s FORCED shuffle: no valid dest"), *GetNameSafe(Pawn));
						}
					}
				}
			}

			// Drift correction: genuine accumulated displacement (e.g. a shove) can walk the pawn
			// off its point — step back to the hunker before the next peek. Threshold is the
			// idle-arrival tolerance the enemy was accepted at, NOT the tighter CoverDriftCorrectDist:
			// arrival can settle the pawn up to SeekCoverArrivalIdleRadius from the hunker, so a
			// lower bar reads a freshly-settled, unmoving enemy as "drifted" and corrects every cycle.
			// Peeks are in-place (no root-motion), so real drift only comes from external displacement.
			{
				const bool bHasCoverDrift = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
				if (bHasCoverDrift)
				{
					FCover DriftCover = ReadCoverFromBB(BB);
					if (DriftCover.IsValid())
					{
						const UCapsuleComponent* DriftCap = Enemy->GetCapsuleComponent();
						const float DriftCapRadius = DriftCap ? DriftCap->GetScaledCapsuleRadius() : DefaultCapsuleRadius;
						const float DriftStandoff = DriftCapRadius + DA->CoverStandoffPadding;
						// MUST match the edge-aligned arrival position or a corner-snapped enemy reads
						// as permanently drifted and step-corrects every pause cycle.
						const FVector DriftHunker = UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
							TickWorld, DriftCover.Data, DriftStandoff, DriftCapRadius, DA->CoverCornerGap, Pawn);
						if (FVector::Dist2D(Pawn->GetActorLocation(), DriftHunker) > SeekCoverArrivalIdleRadius)
						{
							LogCoverMove(TEXT("drift-correct"), Pawn, Enemy);
							// Keep the body tucked facing out while the move steers toward the wall:
							// the per-tick re-assert (top of TickTask) overrides path-following's facing.
							Mem->bDriftCorrecting = true;
							Mem->DriftFacingCover = DriftCover.Data;
							Mem->DriftArrivalPos = DriftHunker;
							Controller->MoveToLocation(DriftHunker, CoverDriftCorrectDist, false, true, true, true);
							Mem->PhaseTimer = 0.6f; // let the step finish before peeking again
							break;
						}
					}
				}
			}

			// Proactive tucked reload: top up behind cover when ammo won't sustain a burst.
			if (DA->bReloadWhileTuckedWhenLow && BB->GetValueAsBool(AEnemyAIController::BB_HasCover))
			{
				AWeaponBase* ReloadW = Enemy->GetCurrentWeapon();
				if (IsValid(ReloadW) && !ReloadW->IsReloading())
				{
					const UWeaponDataAsset* WDA = ReloadW->GetWeaponData();
					const int32 MagSize = IsValid(WDA) ? WDA->MagazineSize : 0;
					if (MagSize > 0 && static_cast<float>(ReloadW->GetCurrentAmmo()) / MagSize < DA->TuckedReloadAmmoFraction)
						ReloadW->Reload();
				}
			}

			// Loop back to acquire (re-check target change for settle reset).
			// UpdateCombatFocus keeps the wall-aligned focal point while posed in cover.
			Enemy->SetAimTarget(Target);
			UpdateCombatFocus(Controller, Enemy, Target);
			Mem->ExposeLosWaitTimer = 0.f;
			Mem->Phase = EFireTaskPhase::Acquire;
			Mem->PhaseTimer = 0.f; // no reaction delay on subsequent loops
		}
		break;

	default:
		break;
	}
}

EBTNodeResult::Type UBTTask_EnemyCombatFire::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FFireMemory* Mem = CastInstanceNodeMemory<FFireMemory>(NodeMemory);
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (Controller) Controller->StopMovement();
	StopFireAndCleanUp(OwnerComp, Mem);
	return EBTNodeResult::Aborted;
}

void UBTTask_EnemyCombatFire::StopFireAndCleanUp(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	// Clear blind-fire state before stopping fire (restores spread + pose flag)
	if (Mem) ClearBlindFireState(Enemy, Mem);

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
		Weapon->StopFiring();

	// Cancel an in-flight grenade wind-up (parity with the old lob task's AbortTask) —
	// no-op unless telegraphing. The cancel broadcast stops the throw montage.
	if (UEnemyGrenadierComponent* GrenComp = Enemy->GetGrenadierComponent())
		GrenComp->CancelThrow();
	if (Mem) Mem->bGrenadeLobPopUp = false;

	Enemy->SetAimTarget(nullptr);
	Enemy->SetHasTargetLOS(false);

	if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();

	// Restore cover-move walk speed before clearing the pose (in case the shuffle was in flight).
	if (Mem) RestoreCoverMoveSpeed(Enemy, Mem);
	if (Mem)
	{
		Mem->bCoverMoveFacingActive = false;
		Mem->bLadderForceOppositeSide = false;
		Mem->bLadderForceOverTop = false;
		Mem->LadderOppositeSide = ECoverLean::None;
		Mem->LadderStage = 0;
		Mem->bLadderSwapMovePending = false;
		Mem->bRelocatePending = false;
		Mem->RelocatePendingSetTime = 0.f;
		ClearPendingShuffle(Enemy, Mem);
	}

	// Reset cover pose on task end; drop any wall-facing focal point so the next branch
	// doesn't inherit cover-aligned yaw. Paths that continue combat re-set focus themselves.
	UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
	if (IsValid(PoseComp))
	{
		PoseComp->SetCoverMoving(false, ECoverLean::None);
		PoseComp->ResetCoverPose();
	}
	if (Controller)
	{
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
		Controller->ClearFocus(EAIFocusPriority::Move);
	}

	if (Mem)
	{
		// If we own a reseek cover, clear intent + BB keys (no MarkVacated — cleanup
		// should not start a post-vacate cooldown; deliberate vacates in ExecuteRelocate
		// and SeekingCover-fail keep their MarkVacated stamps).
		if (Mem->ReseekCover.IsValid())
		{
			UCoverReservationSubsystem* ResSub = Mem->CachedResSub.Get();
			if (IsValid(ResSub) && IsValid(Controller))
			{
				ResSub->ClearIntendedCover(Controller);
			}
			Mem->ReseekCover = FCoverHandle();
			Mem->ReseekCoverData = FCoverData();

			// Clear the BB keys THIS task wrote (only when we owned the cover).
			UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
			if (BB)
			{
				ClearCoverBB(BB);
				BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
			}
		}
	}

	if (Controller)
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

bool UBTTask_EnemyCombatFire::TryReseekCover(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem,
	AAIController* Controller, APawn* Pawn, AEnemyCharacter* Enemy, AActor* Target,
	const UEnemyArchetypeData* DA, bool bHasLOS) const
{
	if (!Controller || !Pawn || !Enemy || !Target || !DA) return false;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	UWorld* World = OwnerComp.GetWorld();
	if (!World || !BB) return false;

	// Use cached pointers; re-acquire if stale
	ACoverSystem* CoverSys = Mem->CachedCoverSys.Get();
	if (!CoverSys)
	{
		CoverSys = ACoverSystem::GetCoverSystem(World);
		Mem->CachedCoverSys = CoverSys;
	}
	if (!CoverSys) return false;

	UCoverReservationSubsystem* ResSub = Mem->CachedResSub.Get();
	if (!ResSub)
	{
		ResSub = World->GetSubsystem<UCoverReservationSubsystem>();
		Mem->CachedResSub = ResSub;
	}

	// Release any cover we still hold before reclaiming.
	FCoverHandle JustReleased;
	if (Mem->ReseekCover.IsValid())
	{
		JustReleased = Mem->ReseekCover;
		if (IsValid(ResSub))
		{
			ResSub->MarkVacated(Mem->ReseekCover, Controller);
			ResSub->ClearIntendedCover(Controller);
		}
		Mem->ReseekCover = FCoverHandle();
		Mem->ReseekCoverData = FCoverData();
	}

	const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	const float Standoff = (Capsule ? Capsule->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + DA->CoverStandoffPadding;

	// Query AICS for covers within search radius
	TArray<FCover> Candidates;
	Candidates.Reserve(64);
	const FBoxSphereBounds SearchBounds(Pawn->GetActorLocation(), FVector(DA->CoverSearchRadius), DA->CoverSearchRadius);
	CoverSys->GetCoverDataWithinBounds(SearchBounds, Candidates);

	// Best-score pick using the shared scoring formula.
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector ThreatLoc = GetPerceivedThreatLoc(Controller, Target);

	const int32 ReseekForcedHeight = GetForceCoverHeightLocal();
	const float FlankArcCos = FMath::Cos(FMath::DegreesToRadians(DA->CoverFlankArcHalfAngleDeg));
	const FCoverScoreParams ScoreParams = UCoverScoringStatics::MakeParamsForEnemy(Enemy);

	// Multi-threat: extra sighted hostiles beyond Target (empty when the archetype disables it).
	TArray<FEnemyKnownThreat> ReseekExtraThreats;
	UCoverScoringStatics::GatherEnemyExtraThreats(Enemy, Target, ReseekExtraThreats);

	// Hostile anchors for the claim-collision reject (same rule as the EQS CoverIntent filter).
	FHostileAnchors ReseekHostileAnchors;
	if (DA->MinHostileCoverDistance > 0.f || DA->MinHostilePawnDistance > 0.f)
		UCoverScoringStatics::GatherHostileAnchors(World, Pawn, Controller, ReseekHostileAnchors);

	TArray<FScoredCover> Scored;
	Scored.Reserve(Candidates.Num());

	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid()) continue;
		if (Candidate.Handle == JustReleased) continue;

		const FCoverData& Data = Candidate.Data;

		// Debug: forced cover height filter
		if (ShouldSkipForForcedHeight(Data, ReseekForcedHeight)) continue;

		// Skip occupied (single lookup — treat occupied-by-self as available)
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Controller) continue;
		// Skip covers intended by another agent (claim race guard)
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, Controller))
			continue;
		// Skip covers on post-vacate cooldown
		if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, Controller, DA->CoverRelocateCooldown))
			continue;
		// Skip covers next to a hostile or a hostile's declared destination (claim collision)
		if (UCoverScoringStatics::IsNearHostileAnchor(Data.Location, ReseekHostileAnchors,
			DA->MinHostileCoverDistance, DA->MinHostilePawnDistance))
			continue;

		// CRITICAL #1: fire-arc gate — same as FindProtectiveCover
		const FVector ToThreat2D = (ThreatLoc - Data.Location).GetSafeNormal2D();
		const FVector FireFwd = UCoverGeometryStatics::GetFireArcForward(Data);
		if (FVector::DotProduct(FireFwd, ToThreat2D) < FlankArcCos)
			continue;

		// Must be able to peek-shoot
		const bool bCrouched = UCoverGeometryStatics::GetCoverHeight(Data) == ECoverHeight::Crouch;
		if (!UCoverGeometryStatics::CanPeekShoot(World, Data, bCrouched, ThreatLoc,
			BodyProtectChestHeight, Target, Pawn))
			continue;

		// Must actually shield the body from the threat — auto-generated points ring every
		// obstacle, including its exposed faces; without this gate a reseek "covers" in the open.
		if (!UCoverGeometryStatics::IsThreatCovered(World, Data, ThreatLoc,
			Standoff, BodyProtectChestHeight, Target, Pawn))
			continue;

		// Shared scoring formula — every candidate here passed the body-shield gate above.
		const float DistSq = FVector::DistSquared(PawnLoc, Data.Location);
		float Score = UCoverScoringStatics::ScoreCandidate(
			World, Data, PawnLoc, ThreatLoc, /*bBodyProtected*/ true, ScoreParams);
		Score = UCoverScoringStatics::ApplyMultiThreatPenalty(Score, World, Data,
			Standoff, BodyProtectChestHeight, Pawn, ReseekExtraThreats, DA->MultiThreatExposurePenalty);

		Scored.Add({ Score, DistSq, Candidate });
	}

	const FCover BestCover = PickBestScoredCover(World, Scored, PawnLoc, ThreatLoc, Target, Pawn, DA);
	if (!BestCover.IsValid()) return false;

	// Write the new cover to the CoverTarget BB key (occupancy service auto-occupies)
	WriteCoverToBB(BB, BestCover);
	Mem->ReseekCover = BestCover.Handle;
	Mem->ReseekCoverData = BestCover.Data;

	// Set intended cover in reservation subsystem
	if (IsValid(ResSub))
		ResSub->SetIntendedCover(Controller, BestCover.Handle);

	// Arrival is always the hunker position — never the exposed corner; the peek montage's
	// root motion steps out from there.
	const FCoverData& FoundData = BestCover.Data;
	const UCapsuleComponent* ReseekCap = Enemy->GetCapsuleComponent();
	const float ReseekCapRadius = ReseekCap ? ReseekCap->GetScaledCapsuleRadius() : DefaultCapsuleRadius;
	const FVector ArrivalPos = UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
		Pawn->GetWorld(), FoundData, Standoff, ReseekCapRadius, DA->CoverCornerGap, Pawn);

	Mem->ReseekArrivalPos = ArrivalPos;

	// Transit hygiene: drop any latched pose (the montage would floor-slide the move) and
	// resume target-tracking yaw for the walk. Clear any in-flight cover-move state so a
	// stale facing lock from a previous shuffle/ladder move doesn't keep the pawn wall-aligned.
	RestoreCoverMoveSpeed(Enemy, Mem);
	Mem->bCoverMoveFacingActive = false;
	ClearPendingShuffle(Enemy, Mem);

	// Cancel an in-flight grenade wind-up before the transit — same reasoning as ExecuteRelocate.
	if (UEnemyGrenadierComponent* GrenComp = Enemy->GetGrenadierComponent())
		GrenComp->CancelThrow();
	Mem->bGrenadeLobPopUp = false;

	UCoverPoseComponent* ReseekPose = Enemy->GetCoverPoseComponent();
	if (IsValid(ReseekPose))
	{
		ReseekPose->SetCoverMoving(false, ECoverLean::None);
		ReseekPose->ResetCoverPose();
	}
	if (IsValid(Target)) Controller->SetFocus(Target);

	if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();

	LogCoverMove(TEXT("reseek"), Pawn, Enemy);
	Controller->MoveToLocation(ArrivalPos, 25.f, false, true, true, true);

	if (bHasLOS)
	{
		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon)) Weapon->StartFiring();
	}

	Mem->SeekStallBestDist = TNumericLimits<float>::Max();
	Mem->SeekStallAccum = 0.f;
	Mem->LadderStage = 0;
	Mem->bLadderSwapMovePending = false;
	Mem->Phase = EFireTaskPhase::SeekingCover;

	UE_LOG(LogEnemyAI, Log, TEXT("[COVER] %s re-seeking cover from open -> (%.0f,%.0f,%.0f)"),
		*Pawn->GetName(), FoundData.Location.X, FoundData.Location.Y, FoundData.Location.Z);
	return true;
}

void UBTTask_EnemyCombatFire::ExecuteRelocate(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem,
	AAIController* Controller, APawn* Pawn, AEnemyCharacter* Enemy,
	AActor* Target, const FCoverHandle& CurCoverHandle, const FCoverData& CurCoverData,
	const UEnemyArchetypeData* DA, bool bHasLOS,
	const FCover* PreselectedCover) const
{
	Mem->bRelocatePending = false;
	Mem->RelocatePendingSetTime = 0.f;

	// Restore cover-move speed if a shuffle was in flight when relocate fires.
	RestoreCoverMoveSpeed(Enemy, Mem);
	Mem->bCoverMoveFacingActive = false;
	Mem->bLadderForceOppositeSide = false;
	Mem->bLadderForceOverTop = false;
	Mem->LadderOppositeSide = ECoverLean::None;
	Mem->LadderStage = 0;
	Mem->bLadderSwapMovePending = false;
	ClearPendingShuffle(Enemy, Mem);

	// Defensive: clear blind-fire if active before relocating
	ClearBlindFireState(Enemy, Mem);

	// Cancel an in-flight grenade wind-up — a relocate mid-telegraph would run off with the
	// throw montage playing and StartFiring over it (the cancel broadcast stops the montage).
	if (UEnemyGrenadierComponent* GrenComp = Enemy->GetGrenadierComponent())
		GrenComp->CancelThrow();
	Mem->bGrenadeLobPopUp = false;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;

	UWorld* World = OwnerComp.GetWorld();
	if (!World) return;

	const float Now = World->GetTimeSeconds();

	// Vacate the current cover
	UCoverReservationSubsystem* ResSub = Mem->CachedResSub.Get();
	if (!ResSub) ResSub = World->GetSubsystem<UCoverReservationSubsystem>();
	if (CurCoverHandle.IsValid() && IsValid(ResSub))
	{
		ResSub->MarkVacated(CurCoverHandle, Controller);
		ResSub->ClearIntendedCover(Controller);
	}

	ClearCoverBB(BB);
	BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
	Mem->ReseekCover = FCoverHandle();
	Mem->ReseekCoverData = FCoverData();

	// Reset cover pose on relocate start; resume target-tracking yaw for the transit.
	// Clear Move-priority focal point so the old path segment doesn't fight target focus.
	UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
	if (IsValid(PoseComp))
	{
		PoseComp->SetCoverMoving(false, ECoverLean::None);
		PoseComp->ResetCoverPose();
	}
	Controller->ClearFocus(EAIFocusPriority::Move);
	if (IsValid(Target)) Controller->SetFocus(Target);

	if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();

	const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	const float Standoff = (Capsule ? Capsule->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + DA->CoverStandoffPadding;

	FCover NewCover = (PreselectedCover && PreselectedCover->IsValid())
		? *PreselectedCover
		: FindProtectiveCover(World, Pawn, Target, DA, Standoff, CurCoverHandle, Controller);
	if (NewCover.IsValid())
	{
		// Write the new cover to the CoverTarget BB key (occupancy service auto-occupies)
		WriteCoverToBB(BB, NewCover);
		Mem->ReseekCover = NewCover.Handle;
		Mem->ReseekCoverData = NewCover.Data;

		// Set intended cover
		if (IsValid(ResSub))
			ResSub->SetIntendedCover(Controller, NewCover.Handle);

		// Arrival is always the hunker position — never the exposed corner; the peek montage's
		// root motion steps out from there.
		const FCoverData& NewData = NewCover.Data;
		const UCapsuleComponent* RelocCap = Enemy->GetCapsuleComponent();
		const float RelocCapRadius = RelocCap ? RelocCap->GetScaledCapsuleRadius() : DefaultCapsuleRadius;
		const FVector ArrivalPos = UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
			Pawn->GetWorld(), NewData, Standoff, RelocCapRadius, DA->CoverCornerGap, Pawn);

		Mem->ReseekArrivalPos = ArrivalPos;
		Mem->bArrivedAtSlot = false;
		Mem->SlotDwellTime = 0.f;
		Mem->LastRelocateCompletedTime = Now;
		LogCoverMove(TEXT("relocate"), Pawn, Enemy);
		Controller->MoveToLocation(ArrivalPos, 25.f, false, true, true, true);

		AWeaponBase* MoveW = Enemy->GetCurrentWeapon();
		if (IsValid(MoveW) && bHasLOS) MoveW->StartFiring();
		Enemy->SetAimTarget(Target);
		Mem->SeekStallBestDist = TNumericLimits<float>::Max(); Mem->SeekStallAccum = 0.f;
		Mem->Phase = EFireTaskPhase::SeekingCover;

		if (GetFlankBreakLogLevel() > 0)
			UE_LOG(LogEnemyAI, Log, TEXT("[FLANKDBG] %s relocate -> protective cover (%.0f,%.0f,%.0f)"),
				*Pawn->GetName(), NewData.Location.X, NewData.Location.Y, NewData.Location.Z);
	}
	else
	{
		FVector StrafePoint;
		if (TryOpenGroundStrafe(Pawn, Target, DA->OpenGroundStrafeRadius, StrafePoint, DA->FlankBreakRetreatBias))
		{
			LogCoverMove(TEXT("strafe"), Pawn, Enemy);
			Controller->MoveToLocation(StrafePoint, 80.f, false, true, false, true);
		}
		Mem->LastRelocateCompletedTime = Now;
		// No protective cover found — re-enter the fire loop standing (top-of-function UnCrouch
		// already ensured we're upright) so the enemy doesn't hang in Expose with bHasCover==false.
		Mem->bArrivedAtSlot = false;
		Mem->Phase = EFireTaskPhase::Pause;
		{
			const UEnemyMoraleComponent* MC = Enemy->GetMoraleComponent();
			const bool bHunk = IsValid(MC) && MC->GetMoraleState() == EMoraleState::Broken;
			Mem->PhaseTimer = RollPauseDuration(DA, bHunk);
		}

		if (GetFlankBreakLogLevel() > 0)
			UE_LOG(LogEnemyAI, Log, TEXT("[FLANKDBG] %s relocate -> no protective cover, strafe / hold standing"), *Pawn->GetName());
	}
}

bool UBTTask_EnemyCombatFire::TryAdvanceRelocate(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem,
	AAIController* Controller, APawn* Pawn, AEnemyCharacter* Enemy, AActor* Target,
	const FCover& CurCover, const UEnemyArchetypeData* DA, bool bHasLOS) const
{
	UWorld* World = OwnerComp.GetWorld();
	if (!World || !IsValid(Pawn) || !IsValid(Enemy) || !IsValid(Target) || !IsValid(DA)) return false;
	if (!CurCover.IsValid()) return false;

	const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	const float Standoff = (Capsule ? Capsule->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + DA->CoverStandoffPadding;

	// Press-shifted params (MakeParamsForEnemy) make FindProtectiveCover prefer closer covers.
	FCover NewCover = FindProtectiveCover(World, Pawn, Target, DA, Standoff, CurCover.Handle, Controller);
	if (!NewCover.IsValid()) return false;

	const FVector ThreatLoc = GetPerceivedThreatLoc(Controller, Target);

	// Gate 1: the pick must genuinely close ground toward the threat.
	const float CurDist = FVector::Dist2D(CurCover.Data.Location, ThreatLoc);
	const float NewDist = FVector::Dist2D(NewCover.Data.Location, ThreatLoc);
	if (NewDist > CurDist - DA->PostureAdvanceMinGain) return false;

	// Gate 2: stickiness — a voluntary move must beat the held cover by the margin.
	const FCoverScoreParams Params = UCoverScoringStatics::MakeParamsForEnemy(Enemy);
	if (Params.StickinessMargin > 0.f)
	{
		// Penalise BOTH sides for multi-threat exposure — without this the held cover gets a
		// free pass and an exposure-driven advance can never clear the margin.
		TArray<FEnemyKnownThreat> ExtraThreats;
		UCoverScoringStatics::GatherEnemyExtraThreats(Enemy, Target, ExtraThreats);

		const FVector PawnLoc = Pawn->GetActorLocation();
		float CurScore = UCoverScoringStatics::ScoreCandidate(
			World, CurCover.Data, PawnLoc, ThreatLoc, /*bBodyProtected*/ true, Params);
		CurScore = UCoverScoringStatics::ApplyMultiThreatPenalty(CurScore, World, CurCover.Data,
			Standoff, BodyProtectChestHeight, Pawn, ExtraThreats, DA->MultiThreatExposurePenalty);
		float NewScore = UCoverScoringStatics::ScoreCandidate(
			World, NewCover.Data, PawnLoc, ThreatLoc, /*bBodyProtected*/ true, Params);
		NewScore = UCoverScoringStatics::ApplyMultiThreatPenalty(NewScore, World, NewCover.Data,
			Standoff, BodyProtectChestHeight, Pawn, ExtraThreats, DA->MultiThreatExposurePenalty);
		if (NewScore < CurScore + Params.StickinessMargin) return false;
	}

	if (GetFlankBreakLogLevel() > 0)
		UE_LOG(LogEnemyAI, Log, TEXT("[POSTURE] %s ADVANCE relocate: %.0fcm -> %.0fcm from threat"),
			*Pawn->GetName(), CurDist, NewDist);

	ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target,
		CurCover.Handle, CurCover.Data, DA, bHasLOS, &NewCover);
	return true;
}

void UBTTask_EnemyCombatFire::ClearBlindFireState(AEnemyCharacter* Enemy, FFireMemory* Mem) const
{
	if (!IsValid(Enemy) || !Mem) return;
	if (Mem->bBlindFiringNow)
	{
		Enemy->SetExtraSpreadDegrees(0.f);
		// Fix 1: clear the aim-location override set on blind-fire entry so the weapon
		// stops resolving against last-known. Aim target is restored at Pause exit (:1392)
		// or by StopFireAndCleanUp on task teardown.
		Enemy->ClearAimLocationOverride();
		UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent();
		if (IsValid(PoseComp)) PoseComp->SetBlindFiring(false);
		AWeaponBase* W = Enemy->GetCurrentWeapon();
		if (IsValid(W) && W->IsFiring()) W->StopFiring();
	}
	Mem->bBlindFiringNow = false;
	Mem->BlindFireBurstTimer = 0.f;
}

/** Composite shuffle score: side-flag bonus + wall-end projection + angle improvement - distance penalty. */
static float ScoreShuffleCandidate(const FCover& Candidate, const FVector& ThreatLocation,
	const FVector& WallLateral, const FVector& CurrentCoverLoc, float CurAngleDot, float Dist, float MaxDist,
	const UEnemyArchetypeData* DA)
{
	float Score = 0.f;
	const bool bCrouched = UCoverGeometryStatics::GetCoverHeight(Candidate.Data) == ECoverHeight::Crouch;

	// Side-flag bonus
	if (UCoverGeometryStatics::HasThreatFacingSideFlag(Candidate.Data, ThreatLocation, bCrouched))
		Score += DA->ShuffleSideFlagWeight;

	// Wall-end lateral projection: how far toward the wall end (normalized 0-1)
	if (MaxDist > 0.f)
	{
		const FVector WallLat2D = WallLateral.GetSafeNormal2D();
		Score += DA->ShuffleWallEndWeight * (FMath::Abs(FVector::DotProduct(Candidate.Data.Location - CurrentCoverLoc, WallLat2D)) / MaxDist);
	}

	// Angle-to-threat improvement vs current point (positive = better alignment)
	const FVector CandFireFwd = UCoverGeometryStatics::GetFireArcForward(Candidate.Data);
	const FVector CandToThreat = (ThreatLocation - Candidate.Data.Location).GetSafeNormal2D();
	const float AngleImprovement = FVector::DotProduct(CandFireFwd, CandToThreat) - CurAngleDot;
	Score += DA->ShuffleAngleWeight * AngleImprovement;

	// Distance penalty (normalized)
	if (MaxDist > 0.f)
		Score -= DA->ShuffleDistancePenalty * (Dist / MaxDist);

	return Score;
}

FCover UBTTask_EnemyCombatFire::FindShuffleCover(UWorld* World, const APawn* Pawn,
	const FCover& CurrentCover, const FVector& ThreatLocation,
	const UEnemyArchetypeData* DA, AController* Controller,
	AActor* Target, bool bRelaxed) const
{
	if (!World || !IsValid(Pawn) || !CurrentCover.IsValid() || !IsValid(DA))
		return FCover();

	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	if (!CoverSys) return FCover();

	UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();

	const FVector PawnLoc = Pawn->GetActorLocation();

	// Resolve standoff for body-protection gate
	const AEnemyCharacter* EnemyChar = Cast<AEnemyCharacter>(Pawn);
	const UCapsuleComponent* Cap = EnemyChar ? EnemyChar->GetCapsuleComponent() : nullptr;
	const float Standoff = (Cap ? Cap->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + DA->CoverStandoffPadding;

	// Fix 7: clamp degenerate distance band (Min > Max) to avoid zero-width search.
	const float EffectiveDistMax = FMath::Max(DA->ShuffleDistanceMin, DA->ShuffleDistanceMax);
	if (!bRelaxed && DA->ShuffleDistanceMin > DA->ShuffleDistanceMax)
	{
		UE_LOG(LogEnemyAI, Warning,
			TEXT("[COVER] %s ShuffleDistanceMin (%.0f) > ShuffleDistanceMax (%.0f) — clamping Max to Min"),
			*Pawn->GetName(), DA->ShuffleDistanceMin, DA->ShuffleDistanceMax);
	}

	// Relaxed mode: accept any distance > ~25cm, wide max to cover full wall widths (~325cm+).
	static constexpr float RelaxedDistFloor = 25.f;
	static constexpr float RelaxedDistMax = 600.f;
	const float SearchDistMin = bRelaxed ? RelaxedDistFloor : DA->ShuffleDistanceMin;
	const float SearchDistMax = bRelaxed ? RelaxedDistMax : EffectiveDistMax;

	TArray<FCover> Candidates;
	Candidates.Reserve(32);
	const FBoxSphereBounds SearchBounds(PawnLoc, FVector(SearchDistMax), SearchDistMax);
	CoverSys->GetCoverDataWithinBounds(SearchBounds, Candidates);

	// Precompute current-point angle to threat for improvement scoring
	const FVector Lateral = CurrentCover.Data.Rotation.RotateVector(FVector::RightVector);
	const FVector CurToThreat = (ThreatLocation - CurrentCover.Data.Location).GetSafeNormal2D();
	const FVector CurFireFwd = UCoverGeometryStatics::GetFireArcForward(CurrentCover.Data);
	const float CurAngleDot = FVector::DotProduct(CurFireFwd, CurToThreat);

	// Perpendicular-offset gate (mirrors FindSidePeekCover): IsSameWall is direction-only, so a
	// parallel wall a few metres over shares the facing and would otherwise pass. Applied in both
	// natural and relaxed modes — relaxed's wider 600cm distance band makes a nearby parallel wall
	// reachable, which is exactly the bug this gate closes.
	const FVector WallNormal2D = CurrentCover.Data.DirectionToWall.GetSafeNormal2D();

	// PERF: hoist trig out of the per-candidate loop.
	const float FlankArcCos = FMath::Cos(FMath::DegreesToRadians(DA->CoverFlankArcHalfAngleDeg));

	// Hostile anchors for the claim-collision reject (same rule as the EQS CoverIntent filter).
	FHostileAnchors ShuffleHostileAnchors;
	if (DA->MinHostileCoverDistance > 0.f || DA->MinHostilePawnDistance > 0.f)
		UCoverScoringStatics::GatherHostileAnchors(World, Pawn, Controller, ShuffleHostileAnchors);

	FCover BestCover;
	float BestScore = -FLT_MAX;

	const int32 ShuffleForcedHeight = GetForceCoverHeightLocal();

	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid()) continue;
		if (Candidate.Handle == CurrentCover.Handle) continue;

		// Debug: forced cover height filter
		if (ShouldSkipForForcedHeight(Candidate.Data, ShuffleForcedHeight)) continue;

		// Same wall gate
		if (!UCoverGeometryStatics::IsSameWall(CurrentCover.Data, Candidate.Data)) continue;

		// Reject candidates on a parallel wall offset perpendicular to the current wall plane.
		const FVector Delta2D = FVector(Candidate.Data.Location.X - CurrentCover.Data.Location.X,
			Candidate.Data.Location.Y - CurrentCover.Data.Location.Y, 0.f);
		if (FMath::Abs(FVector::DotProduct(Delta2D, WallNormal2D)) > SameWallMaxPerpOffset) continue;

		// Occupancy gate
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Controller) continue;

		// Intended-by-other gate (claim race guard)
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, Controller))
			continue;

		// Post-vacate cooldown gate
		if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, Controller, DA->CoverRelocateCooldown))
			continue;

		// Claim-collision gate: next to a hostile or a hostile's declared destination
		if (UCoverScoringStatics::IsNearHostileAnchor(Candidate.Data.Location, ShuffleHostileAnchors,
			DA->MinHostileCoverDistance, DA->MinHostilePawnDistance))
			continue;

		// Distance band gate
		const float DistSq = FVector::DistSquared(PawnLoc, Candidate.Data.Location);
		const float Dist = FMath::Sqrt(DistSq);
		if (Dist < SearchDistMin || Dist > SearchDistMax) continue;

		// Fix 5: fire-arc gate — same as FindProtectiveCover/TryReseekCover. Shuffle
		// neighbours outside the DA's fire arc arrive compromised and immediately thrash.
		const FVector ToThreat2D = (ThreatLocation - Candidate.Data.Location).GetSafeNormal2D();
		const FVector FireFwd = UCoverGeometryStatics::GetFireArcForward(Candidate.Data);
		if (FVector::DotProduct(FireFwd, ToThreat2D) < FlankArcCos)
			continue;

		// Fix 2: pass Target (not nullptr) so CanPeekShoot/IsThreatCovered don't read a
		// trace-hit on the threat's own body as "blocked" or "covered" respectively.
		// Relaxed mode skips both CanPeekShoot and body-protection gates.
		if (!bRelaxed)
		{
			if (!UCoverGeometryStatics::CanPeekShoot(World, Candidate.Data,
				UCoverGeometryStatics::GetCoverHeight(Candidate.Data) == ECoverHeight::Crouch,
				ThreatLocation, BodyProtectChestHeight, Target, Pawn))
				continue;

			// Body-protection gate (mirrors companion Finding 4 when archetype requires it)
			if (DA->bRelocateRequiresBodyProtection)
			{
				if (!UCoverGeometryStatics::IsThreatCovered(World, Candidate.Data, ThreatLocation,
					Standoff, BodyProtectChestHeight, Target, Pawn))
					continue;
			}
		}

		// Composite score (replaces nearest-wins)
		const float Score = ScoreShuffleCandidate(Candidate, ThreatLocation, Lateral,
			CurrentCover.Data.Location, CurAngleDot, Dist, SearchDistMax, DA);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestCover = Candidate;
		}
	}

	return BestCover;
}

FCover UBTTask_EnemyCombatFire::FindSidePeekCover(UWorld* World, const APawn* Pawn,
	const FCover& CurrentCover, ECoverLean Side, const FVector& ThreatLocation,
	const UEnemyArchetypeData* DA, AController* Controller, AActor* Target) const
{
	if (!World || !IsValid(Pawn) || !CurrentCover.IsValid() || !IsValid(DA))
		return FCover();
	if (Side != ECoverLean::Left && Side != ECoverLean::Right)
		return FCover();

	ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(World);
	if (!CoverSys) return FCover();

	UCoverReservationSubsystem* ResSub = World->GetSubsystem<UCoverReservationSubsystem>();

	const FVector PawnLoc = Pawn->GetActorLocation();
	const float SearchDist = FMath::Max(DA->ShuffleDistanceMax, 600.f);

	TArray<FCover> Candidates;
	Candidates.Reserve(32);
	const FBoxSphereBounds SearchBounds(PawnLoc, FVector(SearchDist), SearchDist);
	CoverSys->GetCoverDataWithinBounds(SearchBounds, Candidates);

	// Side direction: Right lean = +Lateral, Left = -Lateral (GetLeanPeekPosition convention).
	const FVector Lateral = CurrentCover.Data.Rotation.RotateVector(FVector::RightVector);
	const FVector SideDir = (Side == ECoverLean::Right) ? Lateral : -Lateral;

	// Pick the SIDE-EXTREME same-wall point (max projection toward the wall end), not the
	// nearest — mid-wall points can carry a lean flag and a nearest pick parks the pawn short
	// of the corner. Flagged extreme preferred; unflagged extreme is the forced-debug fallback.
	FCover BestFlagged, BestAny;
	float BestFlaggedProj = 25.f, BestAnyProj = 25.f; // require meaningfully toward the side

	// 2D distance cap: IsSameWall is direction-only (dot ≥ 0.94) so parallel walls across the
	// map pass. Cap to SearchDist so candidates are genuinely on the same physical wall.
	const float SameWallMaxDistSq = SearchDist * SearchDist;
	// Perpendicular-offset cap: nearby parallel walls (e.g. second crate row behind) share
	// the same facing and pass the distance cap; reject when the candidate is too far from the
	// current cover's wall plane.
	const FVector WallNormal2D = CurrentCover.Data.DirectionToWall.GetSafeNormal2D();

	const int32 SidePeekForcedHeight = GetForceCoverHeightLocal();

	// Hostile anchors for the claim-collision reject (same rule as the EQS CoverIntent filter).
	FHostileAnchors SidePeekHostileAnchors;
	if (DA->MinHostileCoverDistance > 0.f || DA->MinHostilePawnDistance > 0.f)
		UCoverScoringStatics::GatherHostileAnchors(World, Pawn, Controller, SidePeekHostileAnchors);

	for (const FCover& Candidate : Candidates)
	{
		if (!Candidate.IsValid()) continue;
		if (Candidate.Handle == CurrentCover.Handle) continue;

		// Same wall + same derived height gate — hop along the wall, never across the room.
		const FCoverData& Data = Candidate.Data;
		if (UCoverGeometryStatics::GetCoverHeight(Data) != UCoverGeometryStatics::GetCoverHeight(CurrentCover.Data)) continue;
		if (!UCoverGeometryStatics::IsSameWall(CurrentCover.Data, Candidate.Data)) continue;

		// Reject candidates beyond the distance cap (parallel walls elsewhere in the level).
		if (FVector::DistSquared2D(CurrentCover.Data.Location, Data.Location) > SameWallMaxDistSq) continue;

		// Reject candidates on a parallel wall offset perpendicular to the current wall plane.
		const FVector Delta2D = FVector(Data.Location.X - CurrentCover.Data.Location.X, Data.Location.Y - CurrentCover.Data.Location.Y, 0.f);
		if (FMath::Abs(FVector::DotProduct(Delta2D, WallNormal2D)) > SameWallMaxPerpOffset) continue;

		// Debug: forced cover height filter
		if (ShouldSkipForForcedHeight(Data, SidePeekForcedHeight)) continue;

		// Occupancy / claim-race / cooldown gates (mirror FindShuffleCover).
		AController* Occupant = CoverSys->GetOccupyingController(Candidate.Handle);
		if (Occupant && Occupant != Controller) continue;
		if (IsValid(ResSub) && ResSub->IsCoverIntendedByOther(Candidate.Handle, Controller))
			continue;
		if (IsValid(ResSub) && ResSub->IsOnPostVacateCooldown(Candidate.Handle, Controller, DA->CoverRelocateCooldown))
			continue;
		// Claim-collision gate: next to a hostile or a hostile's declared destination
		if (UCoverScoringStatics::IsNearHostileAnchor(Data.Location, SidePeekHostileAnchors,
			DA->MinHostileCoverDistance, DA->MinHostilePawnDistance))
			continue;

		const float Proj = FVector::DotProduct(Data.Location - CurrentCover.Data.Location, SideDir);

		const bool bSideCrouched = UCoverGeometryStatics::GetCoverHeight(Data) == ECoverHeight::Crouch;
		const bool bSideFlag = (Side == ECoverLean::Left)
			? (bSideCrouched ? Data.bLeftCoverCrouched : Data.bLeftCoverStanding)
			: (bSideCrouched ? Data.bRightCoverCrouched : Data.bRightCoverStanding);

		if (bSideFlag && Proj > BestFlaggedProj) { BestFlaggedProj = Proj; BestFlagged = Candidate; }
		if (Proj > BestAnyProj) { BestAnyProj = Proj; BestAny = Candidate; }
	}

	return BestFlagged.IsValid() ? BestFlagged : BestAny;
}

FString UBTTask_EnemyCombatFire::GetStaticDescription() const
{
	return TEXT("Peek-fire loop: Acquire → Expose → Fire → Recover → Pause (stays InProgress while Combat)");
}
