// BTTask_EnemyCombatFire — latent peek-fire loop driven by a NodeMemory state machine.
// Bugs 1+2 fix: while AwarenessState==Combat, never return Failed to the Selector.
// Instead: pursue (no LOS/range) or re-seek cover (suppressed in the open).

#include "BTTask_EnemyCombatFire.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "SuppressionComponent.h"
#include "WeaponBase.h"
#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "CoverSlotTypes.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

static constexpr float ExposePhaseDuration  = 0.2f;  // settle before first shot
static constexpr float RecoverPhaseDuration = 0.15f; // re-crouch settle after burst
static constexpr float DefaultCapsuleRadius = 34.f;
static constexpr float SeekCoverArrivalTickRadius = 50.f;   // mirrors BTTask_EnemyMoveToCover
static constexpr float SeekCoverArrivalIdleRadius = 75.f;   // small margin over accept radius — a stop further out is treated as a failed move
/** Arrival radius used when checking whether the enemy has reached their slot (Part B dwell). */
static constexpr float FlankSlotArrivalRadius = 120.f;
/** Consecutive compromise-positive evaluations required before triggering a relocate (debounce). */
static constexpr int32 CompromiseDebounceRequired = 2;
/** Chest height (cm) for the body-protection trace from a candidate's behind-cover position. */
static constexpr float BodyProtectChestHeight = 60.f;

/** True when the candidate slot's hunkered body position is geometry-shielded from ThreatLoc:
 *  a chest-height trace from GetBehindCoverPosition(...) to the threat is BLOCKED by world geometry
 *  (threat + pawn ignored, so a hit means a wall is interposed). Inverse of "can peek-shoot". */
static bool IsSlotBodyProtected(UWorld* World, const AAICoverSlot* Slot, float Alpha, float Standoff,
	const FVector& ThreatLoc, const AActor* IgnoreThreat, const APawn* IgnorePawn)
{
	if (!World || !IsValid(Slot)) return false;

	const FVector BodyPos = Slot->GetBehindCoverPosition(Alpha, Standoff) + FVector(0.f, 0.f, BodyProtectChestHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyRelocateBodyProtect), false);
	if (IgnoreThreat) Params.AddIgnoredActor(IgnoreThreat);
	if (IgnorePawn)   Params.AddIgnoredActor(IgnorePawn);

	FHitResult Hit;
	// ECC_Visibility — the channel cover walls provably block (it stops bullets and gates
	// BB_HasLineOfSight). A squadmate can transiently read as protection here (rare, self-corrects
	// on the next re-eval); accepted over the risk of a channel the cover mesh may not block.
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, BodyPos, ThreatLoc, ECC_Visibility, Params);
	return bBlocked && Hit.GetActor() != IgnoreThreat;
}

/** Returns true when the current cover slot no longer protects against Target.
 *  Arc test uses the same IsTargetInFireArc the registry picker uses (GetActorForwardVector),
 *  widened by ArcSlackDeg so a recently-selected slot doesn't immediately re-trigger.
 *  Body-LOS test probes from the enemy's chest (capsule-derived) to the target. */
static bool IsSlotCompromised(const AAICoverSlot* Slot, const FVector& PawnLoc,
	const AActor* Target, float ArcSlackDeg, UWorld* World, APawn* Pawn)
{
	if (!IsValid(Slot) || !IsValid(Target) || !World) return false;

	const FVector TargetLoc = Target->GetActorLocation();

	// Arc test: mirror IsTargetInFireArc but widen by ArcSlackDeg so the picker and compromise
	// check use the same forward vector (GetActorForwardVector, not GetForwardDirection).
	const FVector ToTarget = (TargetLoc - Slot->GetActorLocation()).GetSafeNormal2D();
	const FVector SlotFwd = Slot->GetActorForwardVector().GetSafeNormal2D();
	const float Dot = FVector::DotProduct(SlotFwd, ToTarget);
	const float WidenedHalfArcDeg = Slot->FireArcDegrees * 0.5f + ArcSlackDeg;
	const bool bOutsideArc = Dot < FMath::Cos(FMath::DegreesToRadians(WidenedHalfArcDeg));

	// Body-LOS test: trace from enemy chest to target — if clear, cover no longer interposed.
	// Chest height derived from the pawn's capsule so crouched/short archetypes probe correctly (fix #8).
	const ACharacter* PawnChar = Cast<ACharacter>(Pawn);
	const UCapsuleComponent* Cap = PawnChar ? PawnChar->GetCapsuleComponent() : nullptr;
	const float ChestOffset = Cap ? Cap->GetScaledCapsuleHalfHeight() * 0.6f : 80.f;
	const FVector ChestPos = PawnLoc + FVector(0.f, 0.f, ChestOffset);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyFlankCompromise), false);
	QueryParams.AddIgnoredActor(Pawn);
	QueryParams.AddIgnoredActor(Target);
	// ECC_Visibility — the channel cover walls provably block (matches the weapon/LOS traces).
	const bool bBodyLOSClear = !World->LineTraceTestByChannel(ChestPos, TargetLoc, ECC_Visibility, QueryParams);

	return bOutsideArc || bBodyLOSClear;
}

/** Enemy-only protective relocate pick. Iterates unclaimed in-radius slots; keeps those in-arc +
 *  able to peek-shoot (HasLOSToThreat) and — when DA->bRelocateRequiresBodyProtection — whose
 *  hunkered body is geometry-shielded; scores via the registry formula + protective bonus, nearer-
 *  is-better tiebreak. Returns nullptr when none qualify. Does NOT touch FindBestCoverFor. */
static AAICoverSlot* FindProtectiveCover(UCoverRegistrySubsystem* Registry, UWorld* World,
	const APawn* Pawn, AActor* Target, const UEnemyArchetypeData* DA, float Standoff)
{
	if (!IsValid(Registry) || !World || !IsValid(Pawn) || !IsValid(Target) || !IsValid(DA))
		return nullptr;

	const FVector PawnLoc   = Pawn->GetActorLocation();
	const FVector ThreatLoc = Target->GetActorLocation();

	TArray<AAICoverSlot*> Candidates;
	Candidates.Reserve(16);
	Registry->GetSlotsInRadius(PawnLoc, DA->CoverSearchRadius, Candidates);

	AAICoverSlot* BestSlot = nullptr;
	float BestScore = -1.f;
	float BestDistSq = FLT_MAX;

	for (AAICoverSlot* Slot : Candidates)
	{
		if (!IsValid(Slot)) continue;
		if (Slot->IsOnPostVacateCooldownFor(Pawn, DA->CoverRelocateCooldown)) continue;
		if (Slot->Height == ECoverHeight::Stand && !Slot->bIsPeekableCornerStart && !Slot->bIsPeekableCornerEnd)
			continue;
		if (!Slot->IsTargetInFireArc(ThreatLoc)) continue;
		if (!AAICoverSlot::HasLOSToThreat(World, Slot, ThreatLoc, Target)) continue;

		const float Alpha = Slot->GetAlphaFromLocation(PawnLoc);
		const bool bBodyProtected = IsSlotBodyProtected(World, Slot, Alpha, Standoff, ThreatLoc, Target, Pawn);
		if (DA->bRelocateRequiresBodyProtection && !bBodyProtected) continue;

		float Score = UCoverRegistrySubsystem::ScoreSlotFor(Slot, PawnLoc, Target, DA->CoverSearchRadius);
		if (Score < 0.f) continue;
		if (bBodyProtected) Score += DA->ProtectiveCoverScoreBonus;

		const float DistSq = FVector::DistSquared(PawnLoc, Slot->GetActorLocation());
		const bool bBetter = Score > BestScore;
		const bool bTie    = FMath::IsNearlyEqual(Score, BestScore) && DistSq < BestDistSq;
		if (bBetter || bTie)
		{
			BestScore = Score;
			BestDistSq = DistSq;
			BestSlot = Slot;
		}
	}
	return BestSlot;
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

UBTTask_EnemyCombatFire::UBTTask_EnemyCombatFire()
{
	NodeName = TEXT("Enemy Combat Fire");
	bNotifyTick = true;
}

uint16 UBTTask_EnemyCombatFire::GetInstanceMemorySize() const
{
	return sizeof(FFireMemory);
}

EBTNodeResult::Type UBTTask_EnemyCombatFire::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FFireMemory* Mem = reinterpret_cast<FFireMemory*>(NodeMemory);
	new (Mem) FFireMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	// Only allow Failed from ExecuteTask when NOT in combat — Bug 2 fix.
	const EEnemyAwarenessState Awareness = static_cast<EEnemyAwarenessState>(
		BB->GetValueAsEnum(AEnemyAIController::BB_AwarenessState));

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		if (Awareness == EEnemyAwarenessState::Combat)
		{
			// No target yet but combat aware — pursue last known location.
			Mem->Phase = EFireTaskPhase::Pursuing;
			Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
			const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
			if (!LastKnown.IsNearlyZero())
				Controller->MoveToLocation(LastKnown, 100.f, false, true, false, true);
			return EBTNodeResult::InProgress;
		}
		return EBTNodeResult::Failed;
	}

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	const bool bInRange = BB->GetValueAsBool(AEnemyAIController::BB_TargetInRange);
	if (!bInRange && !bHasLOS)
	{
		if (Awareness == EEnemyAwarenessState::Combat)
		{
			// Out of range/LOS but combat — pursue.
			Mem->Phase = EFireTaskPhase::Pursuing;
			Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
			const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
			const FVector PursueTarget = LastKnown.IsNearlyZero() ? Target->GetActorLocation() : LastKnown;
			Controller->MoveToLocation(PursueTarget, 100.f, false, true, false, true);
			return EBTNodeResult::InProgress;
		}
		return EBTNodeResult::Failed;
	}

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	// Start in Acquire phase
	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);
	Mem->Phase = EFireTaskPhase::Acquire;
	Mem->PhaseTimer = DA->ReactionDelay;

	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyCombatFire::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FFireMemory* Mem = reinterpret_cast<FFireMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const EEnemyAwarenessState Awareness = static_cast<EEnemyAwarenessState>(
		BB->GetValueAsEnum(AEnemyAIController::BB_AwarenessState));

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

	// Bug 2 fix: only allow Failed when NOT in Combat.
	if (!IsValid(Target))
	{
		StopFireAndCleanUp(OwnerComp, Mem);
		if (Awareness != EEnemyAwarenessState::Combat)
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

		// Still in Combat with no target — pursue last known.
		Mem->Phase = EFireTaskPhase::Pursuing;
		const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		if (!LastKnown.IsNearlyZero() && Controller->GetMoveStatus() != EPathFollowingStatus::Moving)
			Controller->MoveToLocation(LastKnown, 100.f, false, true, false, true);
		return;
	}

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	const bool bInRange = BB->GetValueAsBool(AEnemyAIController::BB_TargetInRange);

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

		// Transition to pursue.
		StopFireAndCleanUp(OwnerComp, Mem);
		Mem->Phase = EFireTaskPhase::Pursuing;
		const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		const FVector PursueTarget = LastKnown.IsNearlyZero() ? Target->GetActorLocation() : LastKnown;
		Controller->MoveToLocation(PursueTarget, 100.f, false, true, false, true);
		return;
	}

	// --- Handle Pursuing phase ---
	if (Mem->Phase == EFireTaskPhase::Pursuing)
	{
		if (Awareness != EEnemyAwarenessState::Combat)
		{
			Controller->StopMovement();
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
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

		// Distance-gated arrival check (mirrors BTTask_EnemyMoveToCover).
		// Arrival relies SOLELY on this task's authoritative distance gate — a parallel
		// writer setting BB_HasCover=true while we're still in transit must not short-circuit us.
		const EPathFollowingStatus::Type SeekStatus = Controller->GetMoveStatus();
		const float DistToSlot = FVector::Dist(Pawn->GetActorLocation(), Mem->ReseekArrivalPos);
		const bool bArrived = (DistToSlot <= SeekCoverArrivalTickRadius)
			|| (SeekStatus == EPathFollowingStatus::Idle && DistToSlot <= SeekCoverArrivalIdleRadius);

		// Stall detection: recover if the relocate/reseek move stops closing on the slot (pinned in the open).
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

			AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			if (IsValid(Slot) && Slot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();

			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StopFiring();

			if (Mem->bSuppressCrouchedNoCover)
			{
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
				Mem->bSuppressCrouchedNoCover = false;
			}

			// Part B: reset dwell tracking on fresh arrival.
			Mem->bArrivedAtSlot = false;
			Mem->SlotDwellTime = 0.f;
			Mem->CompromiseConsecutiveCount = 0;
			Mem->CompromiseEvalTimer = 0.f;
			Mem->bRelocatePending = false;
			Mem->ExposeLosTimeoutCount = 0;
			Mem->SeekStallBestDist = TNumericLimits<float>::Max(); Mem->SeekStallAccum = 0.f;

			Enemy->SetAimTarget(Target);
			Controller->SetFocus(Target);
			Mem->Phase = EFireTaskPhase::Acquire;
			Mem->PhaseTimer = DA->ReactionDelay * 0.5f;
			return;
		}

		// Path failed (idle but still far from slot) — slot unreachable. Also fires on stall.
		if (SeekStatus == EPathFollowingStatus::Idle || bSeekStalled)
		{
			if (bSeekStalled)
			{
				if (Controller) Controller->StopMovement();
				UE_LOG(LogEnemyAI, Log, TEXT("[COVER] %s stalled relocating to cover (dist=%.0f) — crouch-in-place, re-seeking"), *Pawn->GetName(), DistToSlot);
			}

			// Release the unreachable slot and fall back to crouch-in-place.
			AAICoverSlot* BadSlot = Mem->ReseekSlot.Get();
			if (IsValid(BadSlot) && IsValid(Pawn))
			{
				BadSlot->MarkVacatedForSwitch(Pawn);
				BadSlot->Release(Pawn);
				Mem->ReseekSlot = nullptr;
			}
			BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
			BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);

			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
			Mem->bSuppressCrouchedNoCover = true;

			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StopFiring();

			Enemy->SetAimTarget(Target);
			Controller->SetFocus(Target);
			Mem->Phase = EFireTaskPhase::Pause;
			Mem->PauseDuration = FMath::RandRange(DA->BurstPauseMin, DA->BurstPauseMax);
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

	Mem->PhaseTimer -= DeltaSeconds;

	// Phase 4: fetch suppression state once per tick
	USuppressionComponent* SupprComp = Enemy->GetSuppressionComponent();
	const bool bSuppressed = IsValid(SupprComp) && SupprComp->IsSuppressed();

	// --- Part B: flank-break / cover-compromise logic + open-ground reposition ---
	if (DA->bCoverFlankBreakEnabled && IsValid(Target))
	{
		const bool bHasCoverNow = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
		AAICoverSlot* CurSlot = bHasCoverNow
			? Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot))
			: nullptr;

		const bool bSafePhase = (Mem->Phase == EFireTaskPhase::Pause
			|| Mem->Phase == EFireTaskPhase::Recover
			|| Mem->Phase == EFireTaskPhase::Acquire);
		const AWeaponBase* WeaponCheck = Enemy->GetCurrentWeapon();
		const bool bNotFiring = !IsValid(WeaponCheck) || !WeaponCheck->IsFiring();
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

		if (IsValid(CurSlot))
		{
			// Track physical arrival at the slot. Measure against the pawn's projection onto the
			// cover LINE (where it actually stands), not the slot centre — a stand-cover enemy parks
			// at the peekable corner, which on a long wall is far from the centre (would never "arrive").
			if (!Mem->bArrivedAtSlot)
			{
				const FVector OnLine = CurSlot->GetLocationAtAlpha(CurSlot->GetAlphaFromLocation(Pawn->GetActorLocation()));
				if (FVector::Dist2D(Pawn->GetActorLocation(), OnLine) <= FlankSlotArrivalRadius)
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

				// FIX 2 — Detection: runs every eval interval regardless of phase / firing state
				// so flank compromise is detected even mid-burst.
				if (bDwellMet && bEvalDue && bCooledDown)
				{
					Mem->CompromiseEvalTimer = 0.f;

					if (IsSlotCompromised(CurSlot, Pawn->GetActorLocation(), Target, DA->CoverFlankArcSlackDeg, GetWorld(), Pawn))
						++Mem->CompromiseConsecutiveCount;
					else
						Mem->CompromiseConsecutiveCount = 0;

					if (Mem->CompromiseConsecutiveCount >= CompromiseDebounceRequired)
					{
						Mem->CompromiseConsecutiveCount = 0;
						if (bSafePhase && bNotFiring)
						{
							Mem->bRelocatePending = false;
							ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target, CurSlot, DA, bHasLOS);
						}
						else
							Mem->bRelocatePending = true;
					}
				}

				// FIX 2 — Deferred commit: execute the pending relocate at the first safe non-firing phase.
				if (Mem->bRelocatePending && bSafePhase && bNotFiring && bCooledDown)
				{
					Mem->bRelocatePending = false;
					Mem->CompromiseConsecutiveCount = 0;
					ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target, CurSlot, DA, bHasLOS);
				}
			}
		}
		else
		{
			// No cover slot.
			Mem->bArrivedAtSlot = false;
			Mem->SlotDwellTime = 0.f;
			Mem->CompromiseConsecutiveCount = 0;

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
					const UWorld* World = Pawn->GetWorld();
					const float Now = World ? World->GetTimeSeconds() : 0.f;
					if ((Now - Mem->LastReseekCoverTime) >= SuppressedReseekCooldown)
					{
						Mem->LastReseekCoverTime = Now;
						if (TryReseekCover(OwnerComp, Mem, Controller, Pawn, Enemy, Target, DA, bHasLOS)) break;
					}
					// No cover found or cooldown active — fall back to crouch-in-place (safe).
					if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
					Mem->bSuppressCrouchedNoCover = true;
				}
				Mem->Phase = EFireTaskPhase::Pause;
				Mem->PauseDuration = FMath::RandRange(DA->BurstPauseMin, DA->BurstPauseMax);
				Mem->PhaseTimer = Mem->PauseDuration;
				break;
			}

			// Un-crouch if we previously crouched without cover due to suppression
			if (Mem->bSuppressCrouchedNoCover)
			{
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
				Mem->bSuppressCrouchedNoCover = false;
			}

			// Expose: un-crouch if in crouch cover; step sideways otherwise
			AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);

			if (bHasCover && IsValid(Slot) && Slot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();

			// Stand-height: step toward the peekable corner ONCE on entering Expose so the
			// LOS-hold below doesn't re-issue the path move on every tick.
			if (bHasCover && IsValid(Slot) && Slot->Height != ECoverHeight::Crouch)
			{
				const float PeekAlpha = Slot->GetAlphaFromLocation(Pawn->GetActorLocation());
				const float LineLen   = FMath::Max(Slot->GetLineLength(), 1.f);
				float TargetAlpha;
				if (Slot->bIsPeekableCornerStart && Slot->bIsPeekableCornerEnd)
					TargetAlpha = (PeekAlpha <= 0.5f) ? 0.f : 1.f;
				else if (Slot->bIsPeekableCornerStart)
					TargetAlpha = 0.f;
				else
					TargetAlpha = 1.f;
				const float StepDir   = (TargetAlpha < PeekAlpha) ? -1.f : 1.f;
				const float StepAlpha = FMath::Clamp(PeekAlpha + StepDir * PeekLateralOffset / LineLen, 0.f, 1.f);
				Controller->MoveToLocation(Slot->GetLocationAtAlpha(StepAlpha), 30.f, false, true, false, true);
			}

			Mem->ExposeLosWaitTimer = 0.f;
			Mem->Phase = EFireTaskPhase::Expose;
			Mem->PhaseTimer = ExposePhaseDuration;
		}
		break;

	case EFireTaskPhase::Expose:
		// Suppression interrupt: duck back to Recover immediately
		if (bSuppressed)
		{
			AAICoverSlot* SuppSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			const bool bSuppCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (bSuppCover && IsValid(SuppSlot) && SuppSlot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
			Mem->Phase = EFireTaskPhase::Recover;
			Mem->PhaseTimer = RecoverPhaseDuration;
			break;
		}
		if (Mem->PhaseTimer <= 0.f)
		{
			// Never open fire until the eye->target trace is clear. Crouch cover clears after
			// the un-crouch (done at Acquire->Expose); stand cover clears at the peekable corner.
			if (!bHasLOS)
			{
				Mem->ExposeLosWaitTimer += DeltaSeconds;
				if (Mem->ExposeLosWaitTimer >= DA->ExposeLosWaitMax)
				{
					Mem->ExposeLosWaitTimer = 0.f;
					++Mem->ExposeLosTimeoutCount;

					// FIX 1: too many consecutive LOS-less Expose attempts → slot is unworkable; relocate.
					if (Mem->ExposeLosTimeoutCount >= DA->MaxExposeLosTimeouts && DA->bCoverFlankBreakEnabled)
					{
						Mem->ExposeLosTimeoutCount = 0;
						const bool bHasCoverExp = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
						AAICoverSlot* ExpSlot = bHasCoverExp
							? Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot))
							: nullptr;
						ExecuteRelocate(OwnerComp, Mem, Controller, Pawn, Enemy, Target, ExpSlot, DA, bHasLOS);
						break;
					}

					// Normal timeout — recover and re-loop; flank-break may relocate next pass.
					Mem->Phase = EFireTaskPhase::Recover;
					Mem->PhaseTimer = RecoverPhaseDuration;
				}
				break; // hold in Expose (PhaseTimer stays <= 0, re-enters here next tick)
			}

			Mem->ExposeLosWaitTimer = 0.f;
			Mem->ExposeLosTimeoutCount = 0; // FIX 1: reset counter — LOS acquired, fire opening.
			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StartFiring();

			Mem->NoLosGraceTimer = 0.f;
			Mem->BurstDuration = FMath::RandRange(DA->BurstDurationMin, DA->BurstDurationMax);
			Mem->Phase = EFireTaskPhase::Fire;
			Mem->PhaseTimer = Mem->BurstDuration;
		}
		break;

	case EFireTaskPhase::Fire:
		// LOS hysteresis: lost LOS mid-burst stops fire only after a grace window (avoids per-frame
		// chop from the 0.25s perception cadence); LOS regained re-asserts fire without re-Acquire.
		if (!bHasLOS)
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

			AAICoverSlot* SuppSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			const bool bSuppCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (bSuppCover && IsValid(SuppSlot) && SuppSlot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();

			Mem->Phase = EFireTaskPhase::Recover;
			Mem->PhaseTimer = RecoverPhaseDuration;
			break;
		}
		if (Mem->PhaseTimer <= 0.f)
		{
			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon))
				Weapon->StopFiring();

			// Recover: re-crouch if slot is crouch height
			AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
			const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (bHasCover && IsValid(Slot) && Slot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();

			Mem->Phase = EFireTaskPhase::Recover;
			Mem->PhaseTimer = RecoverPhaseDuration;
		}
		break;

	case EFireTaskPhase::Recover:
		if (Mem->PhaseTimer <= 0.f)
		{
			Mem->PauseDuration = FMath::RandRange(DA->BurstPauseMin, DA->BurstPauseMax);
			Mem->Phase = EFireTaskPhase::Pause;
			Mem->PhaseTimer = Mem->PauseDuration;
		}
		break;

	case EFireTaskPhase::Pause:
		if (Mem->PhaseTimer <= 0.f)
		{
			// Loop back to acquire (re-check target change for settle reset)
			Enemy->SetAimTarget(Target);
			Controller->SetFocus(Target);
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
	FFireMemory* Mem = reinterpret_cast<FFireMemory*>(NodeMemory);
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

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
		Weapon->StopFiring();

	Enemy->SetAimTarget(nullptr);

	if (Mem)
	{
		if (Mem->bSuppressCrouchedNoCover)
		{
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
			Mem->bSuppressCrouchedNoCover = false;
		}

		// Release any cover slot we claimed during SeekingCover.
		AAICoverSlot* ReseekSlotPtr = Mem->ReseekSlot.Get();
		if (IsValid(ReseekSlotPtr) && IsValid(Pawn))
		{
			ReseekSlotPtr->Release(Pawn);
			Mem->ReseekSlot = nullptr;

			// Clear the BB keys THIS task wrote (only when we owned the slot).
			UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
			if (BB)
			{
				BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
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
	const UWorld* World = Pawn->GetWorld();
	UCoverRegistrySubsystem* Registry = World ? World->GetSubsystem<UCoverRegistrySubsystem>() : nullptr;
	if (!Registry || !BB) return false;

	// Release any slot we still hold before reclaiming.
	AAICoverSlot* JustReleased = nullptr;
	if (AAICoverSlot* Prev = Mem->ReseekSlot.Get())
	{
		if (IsValid(Prev))
		{
			Prev->MarkVacatedForSwitch(Pawn);
			Prev->Release(Pawn);
			JustReleased = Prev;
		}
		Mem->ReseekSlot = nullptr;
	}

	AAICoverSlot* FoundSlot = Registry->FindBestCoverFor(
		Pawn->GetActorLocation(), Target, DA->CoverSearchRadius, nullptr, Pawn, DA->CoverRelocateCooldown);
	if (!IsValid(FoundSlot) || FoundSlot == JustReleased || !FoundSlot->TryClaim(Pawn)) return false;

	BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, FoundSlot);
	Mem->ReseekSlot = FoundSlot;

	const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	const float Standoff = (Capsule ? Capsule->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + DA->CoverStandoffPadding;

	float ReseekAlpha;
	const FVector ArrivalPos = (FoundSlot->Height == ECoverHeight::Stand)
		? FoundSlot->GetStandPeekPosition(Target->GetActorLocation(), Standoff, ReseekAlpha)
		: FoundSlot->GetBehindCoverPosition(FoundSlot->GetAlphaFromLocation(Pawn->GetActorLocation()), Standoff);

	Mem->ReseekArrivalPos = ArrivalPos;

	if (Mem->bSuppressCrouchedNoCover)
	{
		if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
		Mem->bSuppressCrouchedNoCover = false;
	}

	Controller->MoveToLocation(ArrivalPos, 50.f, false, true, true, true);

	if (bHasLOS)
	{
		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon)) Weapon->StartFiring();
	}

	Mem->SeekStallBestDist = TNumericLimits<float>::Max();
	Mem->SeekStallAccum = 0.f;
	Mem->Phase = EFireTaskPhase::SeekingCover;

	UE_LOG(LogEnemyAI, Log, TEXT("[COVER] %s re-seeking cover from open -> %s"), *Pawn->GetName(), *FoundSlot->GetName());
	return true;
}

void UBTTask_EnemyCombatFire::ExecuteRelocate(UBehaviorTreeComponent& OwnerComp, FFireMemory* Mem,
	AAIController* Controller, APawn* Pawn, AEnemyCharacter* Enemy,
	AActor* Target, AAICoverSlot* CurSlot, const UEnemyArchetypeData* DA,
	bool bHasLOS) const
{
	Mem->bRelocatePending = false;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	if (IsValid(CurSlot))
	{
		CurSlot->MarkVacatedForSwitch(Pawn);
		CurSlot->Release(Pawn);
	}
	BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
	BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
	Mem->ReseekSlot = nullptr;

	UCoverRegistrySubsystem* Registry = GetWorld()->GetSubsystem<UCoverRegistrySubsystem>();
	const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	const float Standoff = (Capsule ? Capsule->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + DA->CoverStandoffPadding;

	AAICoverSlot* NewSlot = FindProtectiveCover(Registry, GetWorld(), Pawn, Target, DA, Standoff);
	if (IsValid(NewSlot) && NewSlot->TryClaim(Pawn))
	{
		BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, NewSlot);
		Mem->ReseekSlot = NewSlot;

		float PeekAlpha;
		Mem->ReseekArrivalPos = (NewSlot->Height == ECoverHeight::Stand)
			? NewSlot->GetStandPeekPosition(Target->GetActorLocation(), Standoff, PeekAlpha)
			: NewSlot->GetBehindCoverPosition(NewSlot->GetAlphaFromLocation(Pawn->GetActorLocation()), Standoff);

		Mem->bArrivedAtSlot = false;
		Mem->SlotDwellTime = 0.f;
		Mem->LastRelocateCompletedTime = Now;
		Controller->MoveToLocation(Mem->ReseekArrivalPos, 50.f, false, true, true, true);

		AWeaponBase* MoveW = Enemy->GetCurrentWeapon();
		if (IsValid(MoveW) && bHasLOS) MoveW->StartFiring();
		Enemy->SetAimTarget(Target);
		Mem->SeekStallBestDist = TNumericLimits<float>::Max(); Mem->SeekStallAccum = 0.f;
		Mem->Phase = EFireTaskPhase::SeekingCover;
	}
	else
	{
		FVector StrafePoint;
		if (TryOpenGroundStrafe(Pawn, Target, DA->OpenGroundStrafeRadius, StrafePoint, DA->FlankBreakRetreatBias))
			Controller->MoveToLocation(StrafePoint, 80.f, false, true, false, true);
		Mem->LastRelocateCompletedTime = Now;
		// No protective slot found — re-enter the fire loop cleanly so the enemy doesn't hang
		// in Expose with bHasCover==false and re-accumulate ExposeLosWaitTimer next tick.
		if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
		Mem->bSuppressCrouchedNoCover = true;
		Mem->bArrivedAtSlot = false;
		Mem->Phase = EFireTaskPhase::Pause;
		Mem->PhaseTimer = FMath::RandRange(DA->BurstPauseMin, DA->BurstPauseMax);
	}
}

FString UBTTask_EnemyCombatFire::GetStaticDescription() const
{
	return TEXT("Peek-fire loop: Acquire → Expose → Fire → Recover → Pause (stays InProgress while Combat)");
}
