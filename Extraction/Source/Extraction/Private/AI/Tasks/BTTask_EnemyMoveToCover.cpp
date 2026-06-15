// BTTask_EnemyMoveToCover — claims a cover slot and moves the enemy to it.

#include "BTTask_EnemyMoveToCover.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "EnemySquad.h"
#include "EnemySquadSubsystem.h"
#include "HealthComponent.h"
#include "SuppressionComponent.h"
#include "WeaponBase.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "CoverSlotTypes.h"

// Arrival distance thresholds (cm)
static constexpr float CoverArrivalAcceptRadius = 40.f;
static constexpr float CoverArrivalTickRadius = 50.f;
static constexpr float CoverArrivalIdleRadius = 200.f;

UBTTask_EnemyMoveToCover::UBTTask_EnemyMoveToCover()
{
	NodeName = TEXT("Enemy Move To Cover");
	bNotifyTick = true;
}

uint16 UBTTask_EnemyMoveToCover::GetInstanceMemorySize() const
{
	return sizeof(FMoveToCoverMemory);
}

EBTNodeResult::Type UBTTask_EnemyMoveToCover::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FMoveToCoverMemory* Mem = reinterpret_cast<FMoveToCoverMemory*>(NodeMemory);
	new (Mem) FMoveToCoverMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		ReleaseClaim(BB, Pawn);
		return EBTNodeResult::Failed;
	}

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	const UEnemyArchetypeData* DA = Enemy ? Enemy->GetArchetypeData() : nullptr;
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	// Point-blank override: skip cover entirely so the selector falls through to open-ground fire
	if (DA->PointBlankFireRange > 0.f)
	{
		const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), Target->GetActorLocation());
		if (DistSq <= FMath::Square(DA->PointBlankFireRange))
		{
			ReleaseClaim(BB, Pawn);
			return EBTNodeResult::Failed;
		}
	}

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	// Reuse an already-claimed slot if we still own it and it's still valid
	{
		AAICoverSlot* ExistingSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
		if (IsValid(ExistingSlot) && ExistingSlot->IsClaimedBy(Pawn))
		{
			BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);
			if (ExistingSlot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
			Controller->SetFocus(Target);
			return EBTNodeResult::Succeeded;
		}
	}

	ReleaseClaim(BB, Pawn);

	UCoverRegistrySubsystem* Registry = Pawn->GetWorld()->GetSubsystem<UCoverRegistrySubsystem>();
	if (!Registry) return EBTNodeResult::Failed;

	AAICoverSlot* Slot = ScoreSlotsWithSpacing(Pawn, Enemy, Target, DA, Registry);
	if (!Slot || !Slot->TryClaim(Pawn))
	{
		BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
		BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
		return EBTNodeResult::Failed;
	}

	Mem->bSlotClaimed = true;
	Mem->CachedEnemy = Enemy;
	Mem->CachedDA = DA;
	BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, Slot);

	const FVector PawnLoc = Pawn->GetActorLocation();
	constexpr float DefaultCapsuleRadius = 34.f;
	const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	const float Standoff = (Capsule ? Capsule->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + DA->CoverStandoffPadding;
	FVector ArrivalPos;
	if (Slot->Height == ECoverHeight::Stand)
	{
		float PeekAlpha;
		ArrivalPos = Slot->GetStandPeekPosition(Target->GetActorLocation(), Standoff, PeekAlpha);
	}
	else
	{
		ArrivalPos = Slot->GetBehindCoverPosition(Slot->GetAlphaFromLocation(PawnLoc), Standoff);
	}
	ArrivalPos.Z = PawnLoc.Z;
	Mem->ArrivalPos = ArrivalPos;

	if (FVector::Dist(PawnLoc, ArrivalPos) <= CoverArrivalAcceptRadius)
	{
		BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);
		if (Slot->Height == ECoverHeight::Crouch)
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
		Controller->SetFocus(Target);
		return EBTNodeResult::Succeeded;
	}

	const EPathFollowingRequestResult::Type MoveResult = Controller->MoveToLocation(ArrivalPos, CoverArrivalAcceptRadius, false, true, true, true);
	UE_LOG(LogEnemyAI, Verbose, TEXT("[COVER] %s MoveTo (%.0f,%.0f,%.0f) dist=%.0f result=%d (0=Failed 1=AlreadyAtGoal 2=RequestSuccessful)"),
		*Pawn->GetName(), ArrivalPos.X, ArrivalPos.Y, ArrivalPos.Z, FVector::Dist(PawnLoc, ArrivalPos), (int32)MoveResult);
	Mem->bMoveIssued = true;

	// Advance-fire setup
	if (DA->bFireWhileAdvancing && IsValid(Enemy))
	{
		Enemy->SetAimTarget(Target);
		Controller->SetFocus(Target);
		Enemy->SetExtraSpreadDegrees(DA->AdvanceFireExtraSpreadDeg);
		Mem->FirePhase = EMoveShootFirePhase::Acquire;
		Mem->FireTimer = DA->ReactionDelay;
	}

	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyMoveToCover::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FMoveToCoverMemory* Mem = reinterpret_cast<FMoveToCoverMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn)
	{
		StopAdvanceFire(OwnerComp, Mem, false);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	if (!Mem->bMoveIssued) return;

	UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
	if (!PF)
	{
		StopAdvanceFire(OwnerComp, Mem, false);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
	if (!IsValid(Slot))
	{
		ReleaseClaim(BB, Pawn);
		StopAdvanceFire(OwnerComp, Mem, false);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Use the stored arrival position — do NOT re-project from live pawn location
	const FVector PawnLoc = Pawn->GetActorLocation();
	const float Dist = FVector::Dist(PawnLoc, Mem->ArrivalPos);
	const EPathFollowingStatus::Type Status = PF->GetStatus();

	const bool bArrived = (Dist <= CoverArrivalTickRadius) || (Status == EPathFollowingStatus::Idle && Dist <= CoverArrivalIdleRadius);

	// --- Advance-fire tick (throttled to ~10 Hz via accumulator) ---
	Mem->FireTickAccum += DeltaSeconds;
	constexpr float FireTickInterval = 0.1f;

	AEnemyCharacter* Enemy = Mem->CachedEnemy.Get();
	const UEnemyArchetypeData* DA = Mem->CachedDA.Get();

	if (IsValid(Enemy) && IsValid(DA) && DA->bFireWhileAdvancing && !bArrived && Mem->FireTickAccum >= FireTickInterval)
	{
		const float FireDelta = Mem->FireTickAccum;
		Mem->FireTickAccum = 0.f;

		AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

		// Lost target mid-advance: stop firing but keep moving
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

			// Suppression gate: stop firing, hold in Pause so it resumes cleanly
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
						UE_LOG(LogEnemyAI, Log, TEXT("[COVER-ADVANCE] %s Acquire->Firing bHasLOS=%d burstDur=%.2f"),
							*Pawn->GetName(), (int32)bHasLOS, Mem->FireTimer);
					}
					break;
				}

				case EMoveShootFirePhase::Firing:
				{
					// No LOS mid-burst: stop and go to Pause so we resume on re-acquisition
					if (!bHasLOS)
					{
						AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
						if (IsValid(Weapon)) Weapon->StopFiring();
						Mem->bFiring = false;
						Mem->FirePhase = EMoveShootFirePhase::Pause;
						Mem->FireTimer = FMath::RandRange(DA->AdvanceFireBurstPauseMin, DA->AdvanceFireBurstPauseMax);
						UE_LOG(LogEnemyAI, Log, TEXT("[COVER-ADVANCE] %s Firing->Pause (LOS lost)"), *Pawn->GetName());
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
						UE_LOG(LogEnemyAI, Log, TEXT("[COVER-ADVANCE] %s Firing->Pause (burst done)"), *Pawn->GetName());
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
						UE_LOG(LogEnemyAI, Log, TEXT("[COVER-ADVANCE] %s Pause->Acquire"), *Pawn->GetName());
					}
					break;
				}
				}
			}
		}
	}

	if (!bArrived && Status == EPathFollowingStatus::Moving) return;

	if (bArrived)
	{
		BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);
		if (Slot->Height == ECoverHeight::Crouch)
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
		StopAdvanceFire(OwnerComp, Mem, true);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	// Path failed
	ReleaseClaim(BB, Pawn);
	StopAdvanceFire(OwnerComp, Mem, false);
	return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
}

EBTNodeResult::Type UBTTask_EnemyMoveToCover::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FMoveToCoverMemory* Mem = reinterpret_cast<FMoveToCoverMemory*>(NodeMemory);

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (Controller) Controller->StopMovement();

	StopAdvanceFire(OwnerComp, Mem, false);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ReleaseClaim(BB, Pawn);
	return EBTNodeResult::Aborted;
}

void UBTTask_EnemyMoveToCover::ReleaseClaim(UBlackboardComponent* BB, APawn* Pawn) const
{
	if (!BB) return;

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
	if (IsValid(Slot) && IsValid(Pawn))
		Slot->Release(Pawn);

	BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
	BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
}

void UBTTask_EnemyMoveToCover::StopAdvanceFire(UBehaviorTreeComponent& OwnerComp, FMoveToCoverMemory* Mem, bool bKeepFocus) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
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

AAICoverSlot* UBTTask_EnemyMoveToCover::ScoreSlotsWithSpacing(APawn* Pawn, const AEnemyCharacter* Enemy,
	AActor* Target, const UEnemyArchetypeData* DA, UCoverRegistrySubsystem* Registry) const
{
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector TargetLoc = Target->GetActorLocation();

	TArray<AAICoverSlot*> Candidates;
	Candidates.Reserve(16);
	Registry->GetSlotsInRadius(PawnLoc, DA->CoverSearchRadius, Candidates);

	TArray<FVector> AllyPositions;
	UEnemySquadSubsystem* SquadSub = Pawn->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
	UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;

	if (Squad)
	{
		const TArray<TWeakObjectPtr<AEnemyCharacter>>& Members = Squad->GetMembers();
		AllyPositions.Reserve(Members.Num());
		for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
		{
			AEnemyCharacter* Ally = M.Get();
			if (!IsValid(Ally) || Ally == Pawn) continue;
			UHealthComponent* AllyHP = Ally->GetHealthComponent();
			if (IsValid(AllyHP) && AllyHP->IsDead()) continue;

			FVector Pos = Ally->GetActorLocation();
			AEnemyAIController* AllyAIC = Cast<AEnemyAIController>(Ally->GetController());
			if (AllyAIC)
			{
				UBlackboardComponent* AllyBB = AllyAIC->GetBlackboardComponent();
				AAICoverSlot* AllySlot = AllyBB ? Cast<AAICoverSlot>(AllyBB->GetValueAsObject(AEnemyAIController::BB_CoverSlot)) : nullptr;
				if (IsValid(AllySlot)) Pos = AllySlot->GetActorLocation();
			}
			AllyPositions.Add(Pos);
		}
	}

	UWorld* World = Pawn->GetWorld();

	const float MinSpacing = DA->MinAllySpacing;
	AAICoverSlot* BestSlot = nullptr;
	float BestScore = -1.f;

	for (AAICoverSlot* Slot : Candidates)
	{
		if (!IsValid(Slot) || Slot->IsClaimed()) continue;
		if (Slot->IsOnPostVacateCooldownFor(Pawn, 0.f)) continue;

		// Hide-only stand cover reject
		if (Slot->Height == ECoverHeight::Stand && !Slot->bIsPeekableCornerStart && !Slot->bIsPeekableCornerEnd)
			continue;

		if (!Slot->IsTargetInFireArc(TargetLoc)) continue;

		if (!AAICoverSlot::HasLOSToThreat(World, Slot, TargetLoc, Target)) continue;

		float Score = UCoverRegistrySubsystem::ScoreSlotFor(Slot, PawnLoc, Target, DA->CoverSearchRadius);
		if (Score < 0.f) continue;

		if (MinSpacing > 0.f)
		{
			const FVector SlotLoc = Slot->GetActorLocation();
			for (const FVector& AllyPos : AllyPositions)
			{
				if (FVector::Dist(SlotLoc, AllyPos) < MinSpacing)
				{
					Score *= 0.2f;
					break;
				}
			}
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestSlot = Slot;
		}
	}

	return BestSlot;
}

FString UBTTask_EnemyMoveToCover::GetStaticDescription() const
{
	return TEXT("Find and move to best cover slot from registry");
}
