// BTTask_ShieldAdvance — shield carrier walks toward target with periodic bursts; fails on shield break.

#include "BTTask_ShieldAdvance.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "WeaponBase.h"
#include "EnemyShieldComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"

UBTTask_ShieldAdvance::UBTTask_ShieldAdvance()
{
	NodeName = TEXT("Shield Advance");
	bNotifyTick = true;
}

uint16 UBTTask_ShieldAdvance::GetInstanceMemorySize() const
{
	return sizeof(FShieldAdvanceMemory);
}

EBTNodeResult::Type UBTTask_ShieldAdvance::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FShieldAdvanceMemory* Mem = reinterpret_cast<FShieldAdvanceMemory*>(NodeMemory);
	new (Mem) FShieldAdvanceMemory();

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

	UEnemyShieldComponent* ShieldComp = Enemy->GetShieldComponent();
	if (!IsValid(ShieldComp) || ShieldComp->IsShieldBroken()) return EBTNodeResult::Failed;

	// Cache and override walk speed for the advance
	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
		{
			Mem->OriginalMaxWalkSpeed = CMC->MaxWalkSpeed;
			CMC->MaxWalkSpeed = DA->ShieldAdvanceSpeed;
		}
	}

	Enemy->SetAimTarget(Target);
	Enemy->SetExtraSpreadDegrees(DA->ShieldSidearmSpreadDeg);
	Controller->SetFocus(Target);
	Controller->MoveToActor(Target, 80.f, false, true, false, nullptr, true);

	Mem->BurstCycleTimer = BurstCycleInterval;

	return EBTNodeResult::InProgress;
}

void UBTTask_ShieldAdvance::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FShieldAdvanceMemory* Mem = reinterpret_cast<FShieldAdvanceMemory*>(NodeMemory);

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

	UEnemyShieldComponent* ShieldComp = Enemy->GetShieldComponent();
	if (!IsValid(ShieldComp) || ShieldComp->IsShieldBroken())
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();

	if (Mem->bBursting)
	{
		Mem->BurstActiveTimer -= DeltaSeconds;
		if (Mem->BurstActiveTimer <= 0.f)
		{
			if (IsValid(Weapon))
				Weapon->StopFiring();
			Mem->bBursting = false;
			// Resume advance
			Controller->MoveToActor(Target, 80.f, false, true, false, nullptr, true);
		}
	}
	else
	{
		Mem->BurstCycleTimer -= DeltaSeconds;
		if (Mem->BurstCycleTimer <= 0.f)
		{
			// Pause advance, fire a burst
			Controller->StopMovement();
			if (IsValid(Weapon))
				Weapon->StartFiring();
			Mem->bBursting = true;
			Mem->BurstActiveTimer = BurstDuration;
			Mem->BurstCycleTimer = BurstCycleInterval;
		}
	}
}

EBTNodeResult::Type UBTTask_ShieldAdvance::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FShieldAdvanceMemory* Mem = reinterpret_cast<FShieldAdvanceMemory*>(NodeMemory);
	CleanUp(OwnerComp, Mem);
	return EBTNodeResult::Aborted;
}

void UBTTask_ShieldAdvance::CleanUp(UBehaviorTreeComponent& OwnerComp, FShieldAdvanceMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
		Weapon->StopFiring();

	Enemy->SetAimTarget(nullptr);
	Enemy->SetExtraSpreadDegrees(0.f);

	// Restore original walk speed
	if (Mem && Mem->OriginalMaxWalkSpeed > 0.f)
	{
		if (ACharacter* Char = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
				CMC->MaxWalkSpeed = Mem->OriginalMaxWalkSpeed;
		}
	}

	if (Controller)
	{
		Controller->StopMovement();
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

FString UBTTask_ShieldAdvance::GetStaticDescription() const
{
	return TEXT("Advance toward target at ShieldAdvanceSpeed with periodic burst pauses; fail on shield break");
}
