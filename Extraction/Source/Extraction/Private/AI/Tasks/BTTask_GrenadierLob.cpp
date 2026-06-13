// BTTask_GrenadierLob — accumulates LOS-blocked time; lobs a grenade at last-known when threshold met.
// Returns Failed fast when not triggering so the subtree falls through to normal fire/cover behaviour.

#include "BTTask_GrenadierLob.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemyGrenadierComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

UBTTask_GrenadierLob::UBTTask_GrenadierLob()
{
	NodeName = TEXT("Grenadier Lob");
	bNotifyTick = true;
}

uint16 UBTTask_GrenadierLob::GetInstanceMemorySize() const
{
	return sizeof(FGrenadierLobMemory);
}

EBTNodeResult::Type UBTTask_GrenadierLob::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FGrenadierLobMemory* Mem = reinterpret_cast<FGrenadierLobMemory*>(NodeMemory);
	new (Mem) FGrenadierLobMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Pawn) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	UEnemyGrenadierComponent* GrenComp = Enemy->GetGrenadierComponent();
	if (!IsValid(GrenComp)) return EBTNodeResult::Failed;

	// Only run this task when LOS is already blocked
	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	if (bHasLOS) return EBTNodeResult::Failed;

	return EBTNodeResult::InProgress;
}

void UBTTask_GrenadierLob::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FGrenadierLobMemory* Mem = reinterpret_cast<FGrenadierLobMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	UEnemyGrenadierComponent* GrenComp = Enemy->GetGrenadierComponent();
	if (!IsValid(GrenComp)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);

	if (bHasLOS)
	{
		// LOS restored — reset accumulator and fall through (no throw)
		Mem->LOSBlockedAccum = 0.f;
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	if (Mem->bWaitingForThrow)
	{
		// Telegraph in progress inside the component — wait for it to clear
		if (!GrenComp->IsTelegraphing())
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	Mem->LOSBlockedAccum += DeltaSeconds;

	if (Mem->LOSBlockedAccum < DA->GrenadeLobTriggerLOSBlockedTime) return;

	// Supply exhausted, cooldown running, or component unavailable — fall through to normal behaviour
	if (!GrenComp->CanThrow()) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
	if (LastKnown.IsNearlyZero()) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	if (!GrenComp->TryThrowAt(LastKnown))
	{
		// Throw failed (out of range / unsolvable arc) — fall through
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Throw initiated — wait for telegraph to finish
	Mem->LOSBlockedAccum = 0.f;
	Mem->bWaitingForThrow = true;
}

EBTNodeResult::Type UBTTask_GrenadierLob::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (IsValid(Enemy))
	{
		UEnemyGrenadierComponent* GrenComp = Enemy->GetGrenadierComponent();
		if (IsValid(GrenComp))
			GrenComp->CancelThrow();
	}
	return EBTNodeResult::Aborted;
}

FString UBTTask_GrenadierLob::GetStaticDescription() const
{
	return TEXT("Accumulate LOS-blocked time → lob grenade at last-known when threshold met");
}
