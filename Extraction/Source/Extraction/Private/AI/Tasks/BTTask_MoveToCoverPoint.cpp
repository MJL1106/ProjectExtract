// BTTask_MoveToCoverPoint — latent move to an AICS cover point with advance-fire + stall detection.

#include "BTTask_MoveToCoverPoint.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "CoverSystem.h"
#include "CoverGeometryStatics.h"
#include "CoverReservationSubsystem.h"
#include "CoverPoseComponent.h"
#include "CompanionCharacter.h"
#include "CompanionAIController.h"
#include "CompanionTuningDataAsset.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
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
static constexpr float DefaultCapsuleRadius     = 34.f;
static constexpr float FireTickInterval         = 0.1f;

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

	// Set HasCover false at start (mirror old task's ReleaseClaim semantics)
	if (HasCoverKey.SelectedKeyName != NAME_None)
		BB->SetValueAsBool(HasCoverKey.SelectedKeyName, false);

	// Read the cover from the BB via the typed key accessor (avoids GetOuter null for non-instanced nodes)
	const FCover Cover = BB->GetValue<UBlackboardKeyType_Cover>(CoverTargetKey.GetSelectedKeyID());
	if (!Cover.IsValid()) return EBTNodeResult::Failed;

	// Companion cover-commit gate: cover must be worth the trip (nearby) AND needed (under fire).
	// Declining fails the task — the BT's ForceSuccess decorator routes on to open-engage stand-fighting.
	// Enemies are unaffected; their commit policy lives in the fire task's FSM.
	if (const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn))
	{
		const ACompanionAIController* CompCtrl = Cast<ACompanionAIController>(Controller);
		if (const UCompanionTuningDataAsset* Tuning = CompCtrl ? CompCtrl->GetTuning() : nullptr)
		{
			const float CommitDist = FVector::Dist(Pawn->GetActorLocation(), Cover.Data.Location);
			const USuppressionComponent* Supp = Companion->GetSuppressionComponent();
			const bool bUnderFire = (Supp && Supp->IsSuppressed())
				|| Companion->IsSuppressed(Tuning->CoverCommitUnderFireWindow);
			const bool bTooFar = CommitDist > Tuning->CoverCommitMaxDistance;
			const bool bNotNeeded = Tuning->bCoverCommitRequiresUnderFire && !bUnderFire;
			if (bTooFar || bNotNeeded)
			{
				UE_LOG(LogCompanionAI, Log, TEXT("%s: cover-commit DECLINED dist=%.0f max=%.0f underFire=%d — stand-fighting"),
					*Pawn->GetName(), CommitDist, Tuning->CoverCommitMaxDistance, bUnderFire ? 1 : 0);
				BB->ClearValue(CoverTargetKey.GetSelectedKeyID());
				return EBTNodeResult::Failed;
			}
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
	float CapsuleRadius = DefaultCapsuleRadius;
	if (const ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (const UCapsuleComponent* Cap = Char->GetCapsuleComponent())
			CapsuleRadius = Cap->GetScaledCapsuleRadius();
	}
	const float StandoffPadding = IsValid(DA) ? DA->CoverStandoffPadding : DefaultStandoffPadding;
	const float Standoff = CapsuleRadius + StandoffPadding;

	// Enemies: edge-aligned — endpoint covers snap laterally to a fixed gap from the wall corner
	// so peeks clear consistently regardless of bake spacing. Companions (no enemy DA) keep the
	// plain approach position; their combat task doesn't edge-align. Z from the pawn (nav height).
	FVector ArrivalPos = IsValid(DA)
		? UCoverGeometryStatics::GetEdgeAlignedHunkerPosition(
			OwnerComp.GetWorld(), Cover.Data, Standoff, CapsuleRadius, DA->CoverCornerGap, Pawn)
		: UCoverGeometryStatics::GetApproachPosition(Cover.Data, PawnLoc, Standoff);
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

	// Wait while still moving and not stalled
	if (!bArrived && Status != EPathFollowingStatus::Idle && !bStalled) return;

	if (bArrived)
	{
		// Read fresh cover data for arrival handling; fall back to cached snapshot if the live read fails
		FCoverData Data;
		ACoverSystem* CoverSys = ACoverSystem::GetCoverSystem(Pawn->GetWorld());
		const bool bGotFresh = CoverSys && CoverSys->GetCoverData(Mem->CoverHandle, Data);
		if (!bGotFresh) Data = Mem->CachedCoverData;

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

FString UBTTask_MoveToCoverPoint::GetStaticDescription() const
{
	return FString::Printf(TEXT("Move to AICS cover point%s"),
		bFireWhileAdvancing ? TEXT(" (advance-fire)") : TEXT(""));
}
