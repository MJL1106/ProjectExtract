// BTTask_EnemyMoveAndShoot — cover-anchored strafe-while-firing combat behaviour.

#include "BTTask_EnemyMoveAndShoot.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "SuppressionComponent.h"
#include "WeaponBase.h"
#include "AICoverSlot.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"

UBTTask_EnemyMoveAndShoot::UBTTask_EnemyMoveAndShoot()
{
	NodeName = TEXT("Enemy Move And Shoot");
	bNotifyTick = true;
}

uint16 UBTTask_EnemyMoveAndShoot::GetInstanceMemorySize() const
{
	return sizeof(FMoveAndShootMemory);
}

EBTNodeResult::Type UBTTask_EnemyMoveAndShoot::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FMoveAndShootMemory* Mem = reinterpret_cast<FMoveAndShootMemory*>(NodeMemory);
	new (Mem) FMoveAndShootMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	// Self-gate: archetype opt-in
	if (!DA->bUsesMoveAndShoot) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	// Range gate
	const float DistToTarget = FVector::Dist(Pawn->GetActorLocation(), Target->GetActorLocation());
	if (DistToTarget < DA->MoveAndShootMinRange || DistToTarget > DA->MoveAndShootMaxRange)
		return EBTNodeResult::Failed;

	// Suppression gate at entry
	USuppressionComponent* SupprComp = Enemy->GetSuppressionComponent();
	if (IsValid(SupprComp) && SupprComp->IsSuppressed()) return EBTNodeResult::Failed;

	// Determine anchor: cover slot if available, else current position
	const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
	if (bHasCover && IsValid(Slot))
	{
		Mem->AnchorPoint = Slot->GetActorLocation();
	}
	else
	{
		Mem->AnchorPoint = Pawn->GetActorLocation();
	}

	// Set strafe speed via the canonical single-writer API
	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Strafe);

	// Stand up if crouched (e.g. from a crouch-height cover slot) so MaxWalkSpeed governs, not MaxWalkSpeedCrouched
	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (Char->bIsCrouched) Char->UnCrouch();
	}

	// Aim setup
	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);

	// Fire sub-loop: start in Acquire with ReactionDelay
	Mem->FirePhase = EMoveShootFirePhase::Acquire;
	Mem->FireTimer = DA->ReactionDelay;

	// Strafe sub-loop: pick first strafe point after a short initial delay
	Mem->StrafePhase = EMoveShootStrafePhase::Waiting;
	const float SafeMin = FMath::Min(DA->StrafeIntervalMin, DA->StrafeIntervalMax);
	Mem->StrafeTimer = FMath::RandRange(SafeMin * 0.3f, SafeMin);

	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyMoveAndShoot::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FMoveAndShootMemory* Mem = reinterpret_cast<FMoveAndShootMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn)
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy))
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA))
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Target validation
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Range check — exit gate is widened by hysteresis to prevent boundary churn
	const float DistToTarget = FVector::Dist(Pawn->GetActorLocation(), Target->GetActorLocation());
	if (DistToTarget < (DA->MoveAndShootMinRange - DA->MoveAndShootRangeHysteresis)
		|| DistToTarget > (DA->MoveAndShootMaxRange + DA->MoveAndShootRangeHysteresis))
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// No-LOS give-up: accumulate time without LOS, bail if exceeded
	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	if (!bHasLOS)
	{
		Mem->NoLosAccumulator += DeltaSeconds;
		if (Mem->NoLosAccumulator >= DA->MoveAndShootNoLosGiveUpTime)
		{
			CleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}
	}
	else
	{
		Mem->NoLosAccumulator = 0.f;
	}

	// Re-affirm aim each tick
	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);

	// Suppression state
	USuppressionComponent* SupprComp = Enemy->GetSuppressionComponent();
	const bool bSuppressed = IsValid(SupprComp) && SupprComp->IsSuppressed();

	// === SUPPRESSION GATE ===
	if (bSuppressed)
	{
		// Stop firing if currently firing
		if (Mem->bFiring)
		{
			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StopFiring();
			Mem->bFiring = false;
		}

		// Stop movement
		if (Mem->StrafePhase == EMoveShootStrafePhase::Moving)
		{
			Controller->StopMovement();
			Mem->StrafePhase = EMoveShootStrafePhase::Waiting;
		}

		// Crouch if in cover vicinity and not already crouched
		if (!Mem->bSuppressedCrouched)
		{
			const bool bHasCoverSuppr = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
			if (bHasCoverSuppr)
			{
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
				Mem->bSuppressedCrouched = true;
			}
		}
		Mem->bSuppressedHoldingPosition = true;

		// Reset fire phase to Acquire so we have a reaction delay when suppression clears
		Mem->FirePhase = EMoveShootFirePhase::Acquire;
		Mem->FireTimer = DA->ReactionDelay;
		return; // hold position, wait for suppression to clear
	}

	// Recover from suppression
	if (Mem->bSuppressedHoldingPosition)
	{
		Mem->bSuppressedHoldingPosition = false;
		if (Mem->bSuppressedCrouched)
		{
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
			Mem->bSuppressedCrouched = false;
		}
		// Re-seed strafe timer so we start moving again soon
		Mem->StrafeTimer = FMath::RandRange(0.2f, 0.6f);
		Mem->StrafePhase = EMoveShootStrafePhase::Waiting;
	}

	// === FIRE SUB-LOOP ===
	Mem->FireTimer -= DeltaSeconds;

	switch (Mem->FirePhase)
	{
	case EMoveShootFirePhase::Acquire:
		if (Mem->FireTimer <= 0.f)
		{
			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon))
			{
				Weapon->StartFiring();
				Mem->bFiring = true;
			}
			Mem->FirePhase = EMoveShootFirePhase::Firing;
			Mem->FireTimer = FMath::RandRange(DA->BurstDurationMin, DA->BurstDurationMax);
		}
		break;

	case EMoveShootFirePhase::Firing:
		if (Mem->FireTimer <= 0.f)
		{
			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StopFiring();
			Mem->bFiring = false;
			Mem->FirePhase = EMoveShootFirePhase::Pause;
			Mem->FireTimer = FMath::RandRange(DA->BurstPauseMin, DA->BurstPauseMax);
		}
		break;

	case EMoveShootFirePhase::Pause:
		if (Mem->FireTimer <= 0.f)
		{
			Mem->FirePhase = EMoveShootFirePhase::Acquire;
			Mem->FireTimer = 0.f; // no reaction delay on subsequent loops
		}
		break;
	}

	// === STRAFE SUB-LOOP ===
	// Guard against mis-authored Min > Max
	const float StrafeIntMin = FMath::Min(DA->StrafeIntervalMin, DA->StrafeIntervalMax);
	const float StrafeIntMax = FMath::Max(DA->StrafeIntervalMin, DA->StrafeIntervalMax);

	Mem->StrafeTimer -= DeltaSeconds;

	switch (Mem->StrafePhase)
	{
	case EMoveShootStrafePhase::Waiting:
		if (Mem->StrafeTimer <= 0.f)
		{
			// Refresh anchor: re-read cover slot each repick so orbit re-centres if slot is lost
			// (does NOT claim/release the slot — holding the claim while orbiting is intended)
			{
				const bool bStillHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
				AAICoverSlot* CurrentSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
				if (bStillHasCover && IsValid(CurrentSlot))
					Mem->AnchorPoint = CurrentSlot->GetActorLocation();
				else
					Mem->AnchorPoint = Pawn->GetActorLocation();
			}

			FVector StrafePoint;
			if (SolveStrafePoint(Pawn, Target, Mem, DA, StrafePoint))
			{
				const EPathFollowingRequestResult::Type MoveResult =
					Controller->MoveToLocation(StrafePoint, StrafeArrivalAcceptance, false, true, false, true);

				if (MoveResult == EPathFollowingRequestResult::Failed)
				{
					++Mem->ConsecutiveMoveFailures;
					if (Mem->ConsecutiveMoveFailures >= MaxConsecutiveMoveFailures)
					{
						CleanUp(OwnerComp, Mem);
						return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
					}
					// Retry after the next interval
					Mem->StrafeTimer = FMath::RandRange(StrafeIntMin, StrafeIntMax);
				}
				else
				{
					Mem->ConsecutiveMoveFailures = 0;
					Mem->StrafePhase = EMoveShootStrafePhase::Moving;
				}
			}
			else
			{
				// No valid point found — hold on anchor, try again next interval
				Mem->StrafeTimer = FMath::RandRange(StrafeIntMin, StrafeIntMax);
			}
		}
		break;

	case EMoveShootStrafePhase::Moving:
	{
		UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
		if (!PF)
		{
			CleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		const EPathFollowingStatus::Type Status = PF->GetStatus();
		if (Status == EPathFollowingStatus::Moving) break; // still en route

		// Arrived or path finished — schedule next strafe pick
		Mem->StrafePhase = EMoveShootStrafePhase::Waiting;
		Mem->StrafeTimer = FMath::RandRange(StrafeIntMin, StrafeIntMax);
		break;
	}
	}
}

EBTNodeResult::Type UBTTask_EnemyMoveAndShoot::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FMoveAndShootMemory* Mem = reinterpret_cast<FMoveAndShootMemory*>(NodeMemory);

	AAIController* Controller = OwnerComp.GetAIOwner();
	if (Controller) Controller->StopMovement();

	CleanUp(OwnerComp, Mem);
	return EBTNodeResult::Aborted;
}

bool UBTTask_EnemyMoveAndShoot::SolveStrafePoint(APawn* Pawn, AActor* Target,
	const FMoveAndShootMemory* Mem, const UEnemyArchetypeData* DA, FVector& OutPoint) const
{
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Pawn->GetWorld());
	if (!NavSys) return false;

	UWorld* World = Pawn->GetWorld();
	if (!World) return false;

	const FVector TargetLoc = Target->GetActorLocation();
	const FVector Anchor = Mem->AnchorPoint;
	const float Radius = DA->StrafeRadius;
	const FVector NavExtent(DA->StrafeNavProjectExtent, DA->StrafeNavProjectExtent, DA->StrafeNavProjectExtent);

	FCollisionQueryParams LOSParams(SCENE_QUERY_STAT(MoveAndShootLOS), false);
	LOSParams.AddIgnoredActor(Pawn);
	LOSParams.AddIgnoredActor(Target);

	for (int32 Attempt = 0; Attempt < DA->StrafeLosRetryCount; ++Attempt)
	{
		// Random point within StrafeRadius of anchor
		const float RandAngle = FMath::FRandRange(0.f, 2.f * UE_PI);
		const float RandRadius = FMath::FRandRange(Radius * 0.3f, Radius);
		const FVector Candidate = Anchor + FVector(FMath::Cos(RandAngle) * RandRadius,
			FMath::Sin(RandAngle) * RandRadius, 0.f);

		// Nav-project
		FNavLocation NavLoc;
		if (!NavSys->ProjectPointToNavigation(Candidate, NavLoc, NavExtent))
			continue;

		const FVector Point = NavLoc.Location;

		// LOS gate: can this point see the target?
		const FVector EyePos = Point + FVector(0.f, 0.f, EyeHeightOffset);
		FHitResult Hit;
		const bool bHasLOS = !World->LineTraceSingleByChannel(Hit, EyePos, TargetLoc, ECC_Visibility, LOSParams)
			|| Hit.GetActor() == Target;

		if (!bHasLOS) continue;

		// Don't pick a point too close to where we already are
		const float DistFromPawn = FVector::Dist2D(Pawn->GetActorLocation(), Point);
		if (DistFromPawn < StrafeArrivalAcceptance) continue;

		OutPoint = Point;
		return true;
	}

	return false;
}

void UBTTask_EnemyMoveAndShoot::CleanUp(UBehaviorTreeComponent& OwnerComp, FMoveAndShootMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);

	if (IsValid(Enemy))
	{
		// Stop firing
		if (Mem->bFiring)
		{
			AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
			if (IsValid(Weapon)) Weapon->StopFiring();
			Mem->bFiring = false;
		}

		// Clear aim
		Enemy->SetAimTarget(nullptr);

		// Restore combat speed via the canonical single-writer API
		Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

		// Un-crouch if we crouched due to suppression
		if (Mem->bSuppressedCrouched)
		{
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();
			Mem->bSuppressedCrouched = false;
		}
	}

	if (Controller)
	{
		Controller->StopMovement();
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

FString UBTTask_EnemyMoveAndShoot::GetStaticDescription() const
{
	return TEXT("Cover-anchored strafe-while-firing: orbit anchor, burst fire, LOS-gated nav-projected strafe picks");
}
