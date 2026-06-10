// BTTask_SniperNest — picks a perch, telegraphs aim, fires a single shot, relocates when found.

#include "BTTask_SniperNest.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "WeaponBase.h"
#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "EnemySniperTelegraphComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"

UBTTask_SniperNest::UBTTask_SniperNest()
{
	NodeName = TEXT("Sniper Nest");
	bNotifyTick = true;
}

uint16 UBTTask_SniperNest::GetInstanceMemorySize() const
{
	return sizeof(FSniperNestMemory);
}

EBTNodeResult::Type UBTTask_SniperNest::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FSniperNestMemory* Mem = reinterpret_cast<FSniperNestMemory*>(NodeMemory);
	new (Mem) FSniperNestMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
	Mem->Phase = ESniperNestPhase::Evaluate;

	return EBTNodeResult::InProgress;
}

void UBTTask_SniperNest::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FSniperNestMemory* Mem = reinterpret_cast<FSniperNestMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);

	// Fetch sniper component
	UEnemySniperTelegraphComponent* SniperComp = Enemy->GetSniperTelegraphComponent();

	switch (Mem->Phase)
	{
	case ESniperNestPhase::Evaluate:
	{
		// Determine if we need a new perch
		const bool bNeedsReloc = NeedsRelocation(OwnerComp, NodeMemory);

		if (!bNeedsReloc && bHasLOS)
		{
			// Already in a good spot — start telegraphing if weapon can fire
			AWeaponBase* EvalWeapon = Enemy->GetCurrentWeapon();
			if (IsValid(SniperComp) && SniperComp->CanBeginTelegraph() && IsValid(EvalWeapon) && EvalWeapon->CanFire())
			{
				Enemy->SetAimTarget(Target);
				Controller->SetFocus(Target);
				SniperComp->BeginTelegraph(Target);
				Mem->Phase = ESniperNestPhase::Telegraph;
			}
			return;
		}

		// Pick a new perch
		UCoverRegistrySubsystem* Registry = Pawn->GetWorld()->GetSubsystem<UCoverRegistrySubsystem>();
		if (!Registry)
		{
			CleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		// Release any current claim before searching
		ReleaseSlot(OwnerComp);

		TArray<AAICoverSlot*> Candidates;
		Registry->GetSlotsInRadius(Pawn->GetActorLocation(), DA->SniperPerchSearchRadius, Candidates);

		// Prefer far, stand-height slots
		AAICoverSlot* BestSlot = nullptr;
		float BestScore = -1.f;
		const FVector PawnLoc = Pawn->GetActorLocation();

		for (AAICoverSlot* Slot : Candidates)
		{
			if (!IsValid(Slot) || Slot->IsClaimed()) continue;
			if (Slot->Height != ECoverHeight::Stand) continue;
			if (!Slot->IsTargetInFireArc(Target->GetActorLocation())) continue;

			const float Dist = FVector::Dist(PawnLoc, Slot->GetActorLocation());
			if (Dist > BestScore)
			{
				BestScore = Dist;
				BestSlot = Slot;
			}
		}

		if (!BestSlot || !BestSlot->TryClaim(Pawn))
		{
			CleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		Mem->bSlotClaimed = true;
		BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, BestSlot);

		if (IsValid(SniperComp))
			SniperComp->ResetRelocateCounter();

		Controller->MoveToLocation(BestSlot->GetStandPosition(), 60.f, false, true, false, true);
		Mem->Phase = ESniperNestPhase::MovingToPerch;
		break;
	}

	case ESniperNestPhase::MovingToPerch:
	{
		UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
		if (!PF)
		{
			CleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
		if (!IsValid(Slot))
		{
			CleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		const float Dist = FVector::Dist(Pawn->GetActorLocation(), Slot->GetStandPosition());
		const EPathFollowingStatus::Type Status = PF->GetStatus();
		const bool bArrived = (Dist <= 80.f) || (Status == EPathFollowingStatus::Idle && Dist <= 200.f);

		if (!bArrived && Status == EPathFollowingStatus::Moving) return;

		if (!bArrived)
		{
			// Path failed — release and fail
			CleanUp(OwnerComp, Mem);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}

		// Arrived — try to begin telegraph if weapon can fire
		AWeaponBase* ArrivalWeapon = Enemy->GetCurrentWeapon();
		if (IsValid(SniperComp) && SniperComp->CanBeginTelegraph() && bHasLOS
			&& IsValid(ArrivalWeapon) && ArrivalWeapon->CanFire())
		{
			Enemy->SetAimTarget(Target);
			Controller->SetFocus(Target);
			SniperComp->BeginTelegraph(Target);
			Mem->Phase = ESniperNestPhase::Telegraph;
		}
		else
		{
			// No LOS from perch — re-evaluate next tick
			Mem->Phase = ESniperNestPhase::Evaluate;
		}
		break;
	}

	case ESniperNestPhase::Telegraph:
	{
		if (!bHasLOS)
		{
			if (IsValid(SniperComp))
				SniperComp->CancelTelegraph();
			Mem->Phase = ESniperNestPhase::Evaluate;
			return;
		}

		if (!IsValid(SniperComp) || !SniperComp->IsReadyToFire()) return;

		Mem->Phase = ESniperNestPhase::ReadyToFire;
		break;
	}

	case ESniperNestPhase::ReadyToFire:
	{
		Controller->StopMovement();
		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon))
		{
			Weapon->StartFiring();
			Weapon->StopFiring();
		}

		if (IsValid(SniperComp))
			SniperComp->NotifyShotFired();

		Enemy->SetAimTarget(nullptr);
		Controller->ClearFocus(EAIFocusPriority::Gameplay);

		// Succeeded — BT will loop us back next cycle
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
	}
}

EBTNodeResult::Type UBTTask_SniperNest::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FSniperNestMemory* Mem = reinterpret_cast<FSniperNestMemory*>(NodeMemory);
	CleanUp(OwnerComp, Mem);
	return EBTNodeResult::Aborted;
}

bool UBTTask_SniperNest::NeedsRelocation(UBehaviorTreeComponent& OwnerComp, const uint8* NodeMemory) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return true;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return true;

	UEnemySniperTelegraphComponent* SniperComp = Enemy->GetSniperTelegraphComponent();
	if (!IsValid(SniperComp)) return true;

	const bool bHasLOS = OwnerComp.GetBlackboardComponent()
		? OwnerComp.GetBlackboardComponent()->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight)
		: false;

	if (!bHasLOS) return true;
	if (SniperComp->GetShotsSinceRelocate() >= DA->SniperRelocateAfterShots) return true;
	if (DA->bSniperRelocateWhenDamaged && Enemy->WasDamagedRecently(2.f)) return true;

	return false;
}

void UBTTask_SniperNest::CleanUp(UBehaviorTreeComponent& OwnerComp, FSniperNestMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);

	if (IsValid(Enemy))
	{
		UEnemySniperTelegraphComponent* SniperComp = Enemy->GetSniperTelegraphComponent();
		if (IsValid(SniperComp) && SniperComp->IsTelegraphing())
			SniperComp->CancelTelegraph();

		Enemy->SetAimTarget(nullptr);
	}

	if (Controller)
	{
		Controller->StopMovement();
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}

	ReleaseSlot(OwnerComp);
}

void UBTTask_SniperNest::ReleaseSlot(UBehaviorTreeComponent& OwnerComp) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
	if (IsValid(Slot) && IsValid(Pawn))
		Slot->Release(Pawn);

	BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
	BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
}

FString UBTTask_SniperNest::GetStaticDescription() const
{
	return TEXT("Pick perch → telegraph aim → fire single shot → relocate when found");
}
