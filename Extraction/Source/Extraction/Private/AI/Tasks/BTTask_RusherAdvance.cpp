// BTTask_RusherAdvance — sprints at target, fires on the move, melees on contact.

#include "BTTask_RusherAdvance.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "WeaponBase.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

static constexpr float RusherRePathInterval = 0.5f;

UBTTask_RusherAdvance::UBTTask_RusherAdvance()
{
	NodeName = TEXT("Rusher Advance");
	bNotifyTick = true;
}

uint16 UBTTask_RusherAdvance::GetInstanceMemorySize() const
{
	return sizeof(FRusherMemory);
}

EBTNodeResult::Type UBTTask_RusherAdvance::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FRusherMemory* Mem = reinterpret_cast<FRusherMemory*>(NodeMemory);
	new (Mem) FRusherMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);
	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	const float AcceptRadius = IsValid(DA) ? DA->MeleeApproachDistance : 120.f;
	Controller->MoveToActor(Target, AcceptRadius, false, true, false, nullptr, true);
	Mem->RePathTimer = RusherRePathInterval;

	return EBTNodeResult::InProgress;
}

void UBTTask_RusherAdvance::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FRusherMemory* Mem = reinterpret_cast<FRusherMemory*>(NodeMemory);

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
		CleanUp(OwnerComp);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), Target->GetActorLocation());
	const float MeleeRangeSq = DA->MeleeRange * DA->MeleeRange;

	if (DistSq <= MeleeRangeSq && DA->bCanMelee)
	{
		// Stop firing while in melee range; attempt strike each tick (PerformMelee enforces cooldown)
		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon))
			Weapon->StopFiring();

		if (Enemy->PerformMelee(Target))
		{
			CleanUp(OwnerComp);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		// Cooldown not elapsed — stay InProgress, keep attempting next tick
		return;
	}

	// Outside melee range (or melee disabled) — fire while closing.
	// Gate on CanFire()+!IsFiring() (mirrors BTTask_EnemyCombatFire) so we never call StartFiring
	// during a reload: doing so orphans bWantsToFire=true, and a no-melee rusher (shotgun) has no
	// StopFiring path to clear it, leaving the weapon permanently silent after the first reload.
	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon) && Weapon->CanFire() && !Weapon->IsFiring())
		Weapon->StartFiring();

	Mem->RePathTimer -= DeltaSeconds;
	if (Mem->RePathTimer <= 0.f)
	{
		Controller->MoveToActor(Target, DA->MeleeApproachDistance, false, true, false, nullptr, true);
		Mem->RePathTimer = RusherRePathInterval;
	}
}

EBTNodeResult::Type UBTTask_RusherAdvance::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CleanUp(OwnerComp);
	return EBTNodeResult::Aborted;
}

void UBTTask_RusherAdvance::CleanUp(UBehaviorTreeComponent& OwnerComp) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
		Weapon->StopFiring();

	Enemy->SetAimTarget(nullptr);
	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	if (Controller)
	{
		Controller->StopMovement();
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

FString UBTTask_RusherAdvance::GetStaticDescription() const
{
	return TEXT("Sprint at target, fire on the move, melee on contact");
}
