// BTTask_HeavySuppress — heavy anchors in place, drives sustained burst fire at target or last-known.

#include "BTTask_HeavySuppress.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "WeaponBase.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

UBTTask_HeavySuppress::UBTTask_HeavySuppress()
{
	NodeName = TEXT("Heavy Suppress");
	bNotifyTick = true;
}

uint16 UBTTask_HeavySuppress::GetInstanceMemorySize() const
{
	return sizeof(FHeavySuppressMemory);
}

EBTNodeResult::Type UBTTask_HeavySuppress::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FHeavySuppressMemory* Mem = reinterpret_cast<FHeavySuppressMemory*>(NodeMemory);
	new (Mem) FHeavySuppressMemory();

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

	// Anchor — no movement for the heavy while suppressing
	Controller->StopMovement();
	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	bool bHasAimSource = false;

	if (bHasLOS)
	{
		Enemy->SetAimTarget(Target);
		Controller->SetFocus(Target);
		bHasAimSource = true;
	}
	else
	{
		// Clear aim target so WeaponBase uses the location override, not the actor position
		Enemy->SetAimTarget(nullptr);
		const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		if (!LastKnown.IsNearlyZero())
		{
			Enemy->SetAimLocationOverride(LastKnown);
			Controller->SetFocalPoint(LastKnown);
			Mem->bAimOverrideActive = true;
			bHasAimSource = true;
		}
	}

	// Only start firing if there is a valid aim source — don't forward-hose into the void
	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (bHasAimSource && IsValid(Weapon))
		Weapon->StartFiring();

	Mem->Phase = EHeavySuppressPhase::Fire;
	Mem->PhaseTimer = FMath::RandRange(DA->BurstDurationMin, DA->BurstDurationMax);

	return EBTNodeResult::InProgress;
}

void UBTTask_HeavySuppress::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FHeavySuppressMemory* Mem = reinterpret_cast<FHeavySuppressMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

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

	// Keep aim target / override current with LOS state each tick
	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	bool bHasAimSource = false;

	if (bHasLOS)
	{
		if (Mem->bAimOverrideActive)
		{
			Enemy->ClearAimLocationOverride();
			Mem->bAimOverrideActive = false;
		}
		Enemy->SetAimTarget(Target);
		Controller->SetFocus(Target);
		bHasAimSource = true;
	}
	else
	{
		// Clear aim target so WeaponBase uses the location override, not the actor position
		Enemy->SetAimTarget(nullptr);
		const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		if (!LastKnown.IsNearlyZero())
		{
			Enemy->SetAimLocationOverride(LastKnown);
			Controller->SetFocalPoint(LastKnown);
			Mem->bAimOverrideActive = true;
			bHasAimSource = true;
		}
	}

	Mem->PhaseTimer -= DeltaSeconds;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();

	switch (Mem->Phase)
	{
	case EHeavySuppressPhase::Fire:
		if (Mem->PhaseTimer <= 0.f)
		{
			if (IsValid(Weapon))
				Weapon->StopFiring();
			Mem->Phase = EHeavySuppressPhase::Pause;
			Mem->PhaseTimer = FMath::RandRange(DA->BurstPauseMin, DA->BurstPauseMax);
		}
		break;

	case EHeavySuppressPhase::Pause:
		if (Mem->PhaseTimer <= 0.f)
		{
			// Only resume firing if there's still a valid aim source
			if (bHasAimSource && IsValid(Weapon))
				Weapon->StartFiring();
			Mem->Phase = EHeavySuppressPhase::Fire;
			Mem->PhaseTimer = FMath::RandRange(DA->BurstDurationMin, DA->BurstDurationMax);
		}
		break;
	}
}

EBTNodeResult::Type UBTTask_HeavySuppress::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FHeavySuppressMemory* Mem = reinterpret_cast<FHeavySuppressMemory*>(NodeMemory);
	CleanUp(OwnerComp, Mem);
	return EBTNodeResult::Aborted;
}

void UBTTask_HeavySuppress::CleanUp(UBehaviorTreeComponent& OwnerComp, FHeavySuppressMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
		Weapon->StopFiring();

	if (Mem && Mem->bAimOverrideActive)
	{
		Enemy->ClearAimLocationOverride();
		Mem->bAimOverrideActive = false;
	}

	Enemy->SetAimTarget(nullptr);

	if (Controller)
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

FString UBTTask_HeavySuppress::GetStaticDescription() const
{
	return TEXT("Anchor in place, drive sustained burst fire at target or last-known location");
}
