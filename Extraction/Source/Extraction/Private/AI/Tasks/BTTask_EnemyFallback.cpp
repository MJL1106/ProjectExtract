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
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"

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
	FVector ThreatLoc = Pawn->GetActorLocation();
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (IsValid(Target))
	{
		ThreatLoc = Target->GetActorLocation();
	}
	else
	{
		ThreatLoc = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
	}

	UCoverRegistrySubsystem* Registry = Pawn->GetWorld()->GetSubsystem<UCoverRegistrySubsystem>();
	if (!Registry) return false;

	// Release any existing claim before searching
	ReleaseClaim(BB, Pawn);

	const float SearchRadius = DA->CoverSearchRadius * CoverSearchRadiusMultiplier;
	const FVector PawnLoc = Pawn->GetActorLocation();

	TArray<AAICoverSlot*> Candidates;
	Registry->GetSlotsInRadius(PawnLoc, SearchRadius, Candidates);

	AAICoverSlot* BestSlot = nullptr;
	float BestScore = -1.f;

	for (AAICoverSlot* Slot : Candidates)
	{
		if (!IsValid(Slot) || Slot->IsClaimed()) continue;
		if (Slot == Mem->LastFailedSlot.Get()) continue;
		if (!Slot->IsTargetInFireArc(ThreatLoc)) continue;

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

	Mem->LastFailedSlot.Reset();

	Mem->bSlotClaimed = true;
	BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, BestSlot);

	FVector ArrivalPos = BestSlot->GetLocationAtAlpha(BestSlot->GetAlphaFromLocation(PawnLoc));
	ArrivalPos.Z = PawnLoc.Z;

	if (FVector::Dist(PawnLoc, ArrivalPos) <= 60.f)
	{
		BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);
		if (BestSlot->Height == ECoverHeight::Crouch)
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
		Mem->Phase = EFallbackPhase::Holding;

		Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);
		return true;
	}

	Controller->MoveToLocation(ArrivalPos, 60.f, false, true, false, true);
	Mem->Phase = EFallbackPhase::Moving;
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
		if (!FindDeepCover(OwnerComp, Mem))
		{
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
			Mem->Phase = EFallbackPhase::Holding;
			Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);
			Mem->CoverRetryTimer = CoverRetryInterval;
		}
		break;
	}

	case EFallbackPhase::Moving:
	{
		UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
		if (!PF)
		{
			StopFireAndCleanUp(OwnerComp);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
		if (!IsValid(Slot))
		{
			ReleaseClaim(BB, Pawn);
			Mem->Phase = EFallbackPhase::FindCover;
			break;
		}

		const FVector PawnLoc = Pawn->GetActorLocation();
		FVector ArrivalPos = Slot->GetLocationAtAlpha(Slot->GetAlphaFromLocation(PawnLoc));
		ArrivalPos.Z = PawnLoc.Z;
		const float Dist = FVector::Dist(PawnLoc, ArrivalPos);
		const EPathFollowingStatus::Type Status = PF->GetStatus();
		const bool bArrived = (Dist <= 80.f) || (Status == EPathFollowingStatus::Idle && Dist <= 200.f);

		if (!bArrived && Status == EPathFollowingStatus::Moving) return;

		if (bArrived)
		{
			BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);
	
			if (Slot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
			Mem->Phase = EFallbackPhase::Holding;
			Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);
			return;
		}

		// Path failed — record slot so FindDeepCover skips it, then retry
		Mem->LastFailedSlot = Slot;
		ReleaseClaim(BB, Pawn);
		Mem->Phase = EFallbackPhase::FindCover;
		break;
	}

	case EFallbackPhase::Holding:
	{
		const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);

		// Retry cover search periodically while holding without cover
		if (!bHasCover)
		{
			Mem->CoverRetryTimer -= DeltaSeconds;
			if (Mem->CoverRetryTimer <= 0.f)
			{
				if (FindDeepCover(OwnerComp, Mem)) break;
				Mem->CoverRetryTimer = CoverRetryInterval;
			}
		}

		// Suppression gate: stay crouched, reset peek cooldown
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
		break;
	}

	case EFallbackPhase::PeekFire:
	{
		Mem->PhaseTimer -= DeltaSeconds;
		if (Mem->PhaseTimer > 0.f) return;

		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon))
			Weapon->StopFiring();

		// Re-crouch
		AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
		const bool bHasCover = BB->GetValueAsBool(AEnemyAIController::BB_HasCover);
		if (bHasCover && IsValid(Slot) && Slot->Height == ECoverHeight::Crouch)
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();

		Enemy->SetAimTarget(nullptr);
		Controller->ClearFocus(EAIFocusPriority::Gameplay);

		Mem->Phase = EFallbackPhase::PeekRecover;
		Mem->PhaseTimer = PeekRecoverDuration;
		break;
	}

	case EFallbackPhase::PeekRecover:
	{
		Mem->PhaseTimer -= DeltaSeconds;
		if (Mem->PhaseTimer > 0.f) return;

		Mem->Phase = EFallbackPhase::Holding;
		Mem->PeekCooldown = FMath::RandRange(PeekIntervalMin, PeekIntervalMax);
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

	StopFireAndCleanUp(OwnerComp);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ReleaseClaim(BB, Pawn);

	if (ACharacter* Char = Cast<ACharacter>(Pawn))
		Char->UnCrouch();

	return EBTNodeResult::Aborted;
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
