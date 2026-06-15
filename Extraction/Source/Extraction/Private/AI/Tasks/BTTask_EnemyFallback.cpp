// BTTask_EnemyFallback — broken-morale retreat: deep cover, turtle, rare peek-fire.

#include "BTTask_EnemyFallback.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "SuppressionComponent.h"
#include "WeaponBase.h"
#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"


// Arrival distance thresholds (cm)
static constexpr float FallbackArrivalAcceptRadius = 40.f;
static constexpr float FallbackArrivalTickRadius = 50.f;
static constexpr float FallbackArrivalIdleRadius = 200.f;

UBTTask_EnemyFallback::UBTTask_EnemyFallback()
{
	NodeName = TEXT("Enemy Fallback (Broken)");
	bNotifyTick = true;
}

uint16 UBTTask_EnemyFallback::GetInstanceMemorySize() const
{
	return sizeof(FFallbackMemory);
}

EBTNodeResult::Type UBTTask_EnemyFallback::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FFallbackMemory* Mem = reinterpret_cast<FFallbackMemory*>(NodeMemory);
	new (Mem) FFallbackMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	if (!FindDeepCover(OwnerComp, Mem))
	{
		if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
		Mem->Phase = EFallbackPhase::Holding;
		Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);
		Mem->CoverRetryTimer = CoverRetryInterval;
	}

	return EBTNodeResult::InProgress;
}

bool UBTTask_EnemyFallback::FindDeepCover(UBehaviorTreeComponent& OwnerComp, FFallbackMemory* Mem)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return false;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return false;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return false;

	// Threat location: prefer combat target, fall back to last known
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	FVector ThreatLoc = IsValid(Target)
		? Target->GetActorLocation()
		: BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);

	UCoverRegistrySubsystem* Registry = Pawn->GetWorld()->GetSubsystem<UCoverRegistrySubsystem>();
	if (!Registry) return false;

	ReleaseClaim(BB, Pawn);

	const float SearchRadius = DA->CoverSearchRadius * CoverSearchRadiusMultiplier;
	const FVector PawnLoc = Pawn->GetActorLocation();
	UWorld* World = Pawn->GetWorld();

	TArray<AAICoverSlot*> Candidates;
	Registry->GetSlotsInRadius(PawnLoc, SearchRadius, Candidates);

	AAICoverSlot* BestSlot = nullptr;
	float BestScore = -1.f;

	for (AAICoverSlot* Slot : Candidates)
	{
		if (!IsValid(Slot) || Slot->IsClaimed()) continue;
		if (Slot == Mem->LastFailedSlot.Get()) continue;

		// Hide-only stand cover reject (mirror MoveToCover)
		if (Slot->Height == ECoverHeight::Stand && !Slot->bIsPeekableCornerStart && !Slot->bIsPeekableCornerEnd)
			continue;

		if (!Slot->IsTargetInFireArc(ThreatLoc)) continue;

		// LOS probe (via shared helper — mirror MoveToCover)
		if (!AAICoverSlot::HasLOSToThreat(World, Slot, ThreatLoc, Target)) continue;

		const float BaseCoverScore = UCoverRegistrySubsystem::ScoreSlotFor(Slot, PawnLoc, Target, SearchRadius);
		const float DistFromThreat = FVector::Dist(Slot->GetActorLocation(), ThreatLoc);
		const float Score = BaseCoverScore + DistFromThreat * DistanceFromThreatWeight;

		if (Score > BestScore)
		{
			BestScore = Score;
			BestSlot = Slot;
		}
	}

	if (!BestSlot || !BestSlot->TryClaim(Pawn))
	{
		Mem->LastFailedSlot = BestSlot;
		return false;
	}

	const float SlotDistFromThreat = FVector::Dist(BestSlot->GetActorLocation(), ThreatLoc);
	UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s chose slot '%s' dist-from-threat=%.0f"),
		*Pawn->GetName(), *BestSlot->GetName(), SlotDistFromThreat);

	Mem->LastFailedSlot.Reset();
	Mem->bSlotClaimed = true;
	BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, BestSlot);

	constexpr float DefaultCapsuleRadius = 34.f;
	const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	const float Standoff = (Capsule ? Capsule->GetScaledCapsuleRadius() : DefaultCapsuleRadius) + DA->CoverStandoffPadding;
	FVector ArrivalPos = BestSlot->GetBehindCoverPosition(BestSlot->GetAlphaFromLocation(PawnLoc), Standoff);
	ArrivalPos.Z = PawnLoc.Z;
	Mem->ArrivalPos = ArrivalPos;

	if (FVector::Dist(PawnLoc, ArrivalPos) <= FallbackArrivalAcceptRadius)
	{
		BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);
		if (BestSlot->Height == ECoverHeight::Crouch)
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
		Mem->Phase = EFallbackPhase::Holding;
		Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);
		UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s -> Holding (already at slot, Z=%.0f)"),
			*Pawn->GetName(), ArrivalPos.Z);

		// Face the threat (or last known if no live target)
		if (IsValid(Target))
			Controller->SetFocus(Target);
		else
			Controller->SetFocalPoint(ThreatLoc, EAIFocusPriority::Gameplay);

		return true;
	}

	Controller->MoveToLocation(ArrivalPos, FallbackArrivalAcceptRadius, false, true, true, true);
	Mem->Phase = EFallbackPhase::Moving;
	UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s -> Moving to slot '%s'"),
		*Pawn->GetName(), *BestSlot->GetName());

	// Advance-fire setup: face + aim at target while retreating (mirrors MoveToCover)
	if (DA->bFireWhileAdvancing && IsValid(Target))
	{
		Enemy->SetAimTarget(Target);
		Controller->SetFocus(Target);
		Enemy->SetExtraSpreadDegrees(DA->AdvanceFireExtraSpreadDeg);
		Mem->FirePhase = EMoveShootFirePhase::Acquire;
		Mem->FireTimer = DA->ReactionDelay;
		Mem->bFiring = false;
	}

	return true;
}

void UBTTask_EnemyFallback::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FFallbackMemory* Mem = reinterpret_cast<FFallbackMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	switch (Mem->Phase)
	{
	case EFallbackPhase::FindCover:
	{
		UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s phase=FindCover"), *Pawn->GetName());
		if (!FindDeepCover(OwnerComp, Mem))
		{
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
			Mem->Phase = EFallbackPhase::Holding;
			Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);
			Mem->CoverRetryTimer = CoverRetryInterval;
			UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s no cover found -> Holding (no slot)"), *Pawn->GetName());
		}
		break;
	}

	case EFallbackPhase::Moving:
	{
		UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
		if (!PF)
		{
			StopFallbackFire(OwnerComp, Mem, false);
			StopFireAndCleanUp(OwnerComp);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
		if (!IsValid(Slot))
		{
			StopFallbackFire(OwnerComp, Mem, false);
			ReleaseClaim(BB, Pawn);
			Mem->Phase = EFallbackPhase::FindCover;
			break;
		}

		// Use the stored arrival position — do NOT re-project from live pawn location
		const FVector PawnLoc = Pawn->GetActorLocation();
		const float Dist = FVector::Dist(PawnLoc, Mem->ArrivalPos);
		const EPathFollowingStatus::Type Status = PF->GetStatus();
		const bool bArrived = (Dist <= FallbackArrivalTickRadius) || (Status == EPathFollowingStatus::Idle && Dist <= FallbackArrivalIdleRadius);

		// --- Advance-fire tick (throttled to ~10 Hz via accumulator) ---
		const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
		Mem->FireTickAccum += DeltaSeconds;
		constexpr float FireTickInterval = 0.1f;

		if (IsValid(DA) && DA->bFireWhileAdvancing && !bArrived && Mem->FireTickAccum >= FireTickInterval)
		{
			const float FireDelta = Mem->FireTickAccum;
			Mem->FireTickAccum = 0.f;

			AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

			// Re-affirm focus each tick so facing stays locked on player
			if (IsValid(Target))
			{
				const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
				if (bHasLOS)
					Controller->SetFocus(Target);
				else
					Controller->SetFocalPoint(BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation), EAIFocusPriority::Gameplay);
				Enemy->SetAimTarget(Target);

				USuppressionComponent* SupprComp = Enemy->GetSuppressionComponent();
				const bool bSuppressed = IsValid(SupprComp) && SupprComp->IsSuppressed();

				if (bSuppressed && Mem->bFiring)
				{
					AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
					if (IsValid(Weapon)) Weapon->StopFiring();
					Mem->bFiring = false;
					Mem->FirePhase = EMoveShootFirePhase::Acquire;
					UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s fire-Stop (suppressed, keep moving)"), *Pawn->GetName());
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
							UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s Acquire->Firing bHasLOS=%d burstDur=%.2f"),
								*Pawn->GetName(), (int32)bHasLOS, Mem->FireTimer);
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
							UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s Firing->Pause (LOS lost)"), *Pawn->GetName());
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
							UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s Firing->Pause (burst done)"), *Pawn->GetName());
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
							UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s Pause->Acquire"), *Pawn->GetName());
						}
						break;
					}
					}
				}
			}
			else
			{
				// Lost target mid-retreat: stop firing, keep moving
				if (Mem->bFiring)
				{
					AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
					if (IsValid(Weapon)) Weapon->StopFiring();
					Mem->bFiring = false;
					UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s fire-Stop (target lost mid-move)"), *Pawn->GetName());
				}
				Mem->FirePhase = EMoveShootFirePhase::Acquire;
			}
		}

		if (!bArrived && Status == EPathFollowingStatus::Moving) return;

		if (bArrived)
		{
			BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);

			if (Slot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
			Mem->Phase = EFallbackPhase::Holding;
			Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);

			// Face the threat on arrival; StopFallbackFire keeps focus if we have a live target
			AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
			StopFallbackFire(OwnerComp, Mem, IsValid(Target));

			if (!IsValid(Target))
				Controller->SetFocalPoint(BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation), EAIFocusPriority::Gameplay);

			UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s arrived at slot '%s' Z=%.0f -> Holding (focus kept=%d)"),
				*Pawn->GetName(), *Slot->GetName(), Pawn->GetActorLocation().Z, (int32)IsValid(Target));
			return;
		}

		// Path failed — record slot so FindDeepCover skips it, then retry
		StopFallbackFire(OwnerComp, Mem, false);
		Mem->LastFailedSlot = Slot;
		ReleaseClaim(BB, Pawn);
		Mem->Phase = EFallbackPhase::FindCover;
		UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s path failed -> FindCover"), *Pawn->GetName());
		break;
	}

	case EFallbackPhase::Holding:
	{
		const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);

		if (!bHasCover)
		{
			Mem->CoverRetryTimer -= DeltaSeconds;
			if (Mem->CoverRetryTimer <= 0.f)
			{
				if (FindDeepCover(OwnerComp, Mem)) break;
				Mem->CoverRetryTimer = CoverRetryInterval;
			}
		}

		USuppressionComponent* SupprComp = Enemy->GetSuppressionComponent();
		if (IsValid(SupprComp) && SupprComp->IsSuppressed())
		{
			Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);
			return;
		}

		Mem->PeekCooldown -= DeltaSeconds;
		if (Mem->PeekCooldown > 0.f) return;

		AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
		if (!IsValid(Target))
		{
			Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);
			return;
		}

		AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
		if (bHasCover && IsValid(Slot) && Slot->Height == ECoverHeight::Crouch)
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->UnCrouch();

		Enemy->SetAimTarget(Target);
		Controller->SetFocus(Target);

		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon))
			Weapon->StartFiring();

		Mem->Phase = EFallbackPhase::PeekFire;
		Mem->PhaseTimer = PeekBurstDuration;
		UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s -> PeekFire (burst=%.2fs)"), *Pawn->GetName(), PeekBurstDuration);
		break;
	}

	case EFallbackPhase::PeekFire:
	{
		Mem->PhaseTimer -= DeltaSeconds;
		if (Mem->PhaseTimer > 0.f) return;

		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon))
			Weapon->StopFiring();

		AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
		const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
		if (bHasCover && IsValid(Slot) && Slot->Height == ECoverHeight::Crouch)
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();

		Enemy->SetAimTarget(nullptr);
		Controller->ClearFocus(EAIFocusPriority::Gameplay);

		Mem->Phase = EFallbackPhase::PeekRecover;
		Mem->PhaseTimer = PeekRecoverDuration;
		UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s PeekFire->PeekRecover"), *Pawn->GetName());
		break;
	}

	case EFallbackPhase::PeekRecover:
	{
		Mem->PhaseTimer -= DeltaSeconds;
		if (Mem->PhaseTimer > 0.f) return;

		Mem->Phase = EFallbackPhase::Holding;
		Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);
		UE_LOG(LogEnemyAI, Log, TEXT("[FALLBACK] %s PeekRecover->Holding (next peek in %.1fs)"), *Pawn->GetName(), Mem->PeekCooldown);
		break;
	}
	}
}

EBTNodeResult::Type UBTTask_EnemyFallback::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FFallbackMemory* Mem = reinterpret_cast<FFallbackMemory*>(NodeMemory);

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (Controller) Controller->StopMovement();

	StopFallbackFire(OwnerComp, Mem, false);
	StopFireAndCleanUp(OwnerComp);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ReleaseClaim(BB, Pawn);

	if (ACharacter* Char = Cast<ACharacter>(Pawn))
		Char->UnCrouch();

	return EBTNodeResult::Aborted;
}

void UBTTask_EnemyFallback::StopFallbackFire(UBehaviorTreeComponent& OwnerComp, FFallbackMemory* Mem, bool bKeepFocus) const
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

void UBTTask_EnemyFallback::StopFireAndCleanUp(UBehaviorTreeComponent& OwnerComp) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
		Weapon->StopFiring();

	Enemy->SetAimTarget(nullptr);

	if (Controller)
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

void UBTTask_EnemyFallback::ReleaseClaim(UBlackboardComponent* BB, APawn* Pawn) const
{
	if (!BB) return;

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
	if (IsValid(Slot) && IsValid(Pawn))
		Slot->Release(Pawn);

	BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
	BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
}

FString UBTTask_EnemyFallback::GetStaticDescription() const
{
	return TEXT("Broken morale: retreat to deep cover, turtle, peek-fire rarely");
}
