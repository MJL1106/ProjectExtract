// BTDecorator_ShotgunCommit — tick-observer: rush branch active when target is inside commit range AND health is above abort fraction.
// CalculateRawConditionValue is the live source of truth (latch-aware; hysteresis via bRushing).
// TickNode fires ConditionalFlowAbort only when the condition actually changes.
// bRushing resets on InitializeMemory + OnCeaseRelevant — every new engagement starts in hold.

#include "BTDecorator_ShotgunCommit.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "HealthComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

UBTDecorator_ShotgunCommit::UBTDecorator_ShotgunCommit()
{
	NodeName = TEXT("Shotgun Commit");
	bNotifyTick = true;
	FlowAbortMode = EBTFlowAbortMode::Both;
}

uint16 UBTDecorator_ShotgunCommit::GetInstanceMemorySize() const
{
	return sizeof(FShotgunCommitMemory);
}

AEnemyCharacter* UBTDecorator_ShotgunCommit::ResolveEnemy(UBehaviorTreeComponent& OwnerComp, FShotgunCommitMemory* Mem) const
{
	AEnemyCharacter* Enemy = Mem->CachedEnemy.Get();
	if (IsValid(Enemy)) return Enemy;

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	Enemy = Cast<AEnemyCharacter>(Pawn);
	if (IsValid(Enemy)) Mem->CachedEnemy = Enemy;

	return Enemy;
}

void UBTDecorator_ShotgunCommit::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	FShotgunCommitMemory* Mem = reinterpret_cast<FShotgunCommitMemory*>(NodeMemory);
	Mem->bRushing = false;
	Mem->bLastCondition = false;
	Mem->bValidationWarningEmitted = false;
	Mem->CachedEnemy.Reset();

	// One-shot DA misconfiguration warning.
	AEnemyCharacter* Enemy = ResolveEnemy(OwnerComp, Mem);
	if (!IsValid(Enemy)) return;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return;

	if (!Mem->bValidationWarningEmitted && DA->ShotgunCommitRange >= DA->EngageRangeMax)
	{
		UE_LOG(LogEnemyAI, Warning,
			TEXT("BTDecorator_ShotgunCommit [%s]: ShotgunCommitRange (%.0f) >= EngageRangeMax (%.0f) — "
			     "the hold/cover phase will be skipped; enemy commits to a rush on first contact. Tune the DA."),
			*GetNameSafe(DA), DA->ShotgunCommitRange, DA->EngageRangeMax);
		Mem->bValidationWarningEmitted = true;
	}
}

void UBTDecorator_ShotgunCommit::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	FShotgunCommitMemory* Mem = reinterpret_cast<FShotgunCommitMemory*>(NodeMemory);
	Mem->CachedEnemy.Reset();
}

bool UBTDecorator_ShotgunCommit::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	// Live, latch-aware evaluation — not a cached read.
	// bRushing drives the hysteresis band; after InitializeMemory (runs on every subtree re-push) it is false,
	// so the first Selector evaluation of a new engagement uses the tight commit range and correctly returns false at long range.
	FShotgunCommitMemory* Mem = reinterpret_cast<FShotgunCommitMemory*>(NodeMemory);

	AEnemyCharacter* Enemy = ResolveEnemy(OwnerComp, Mem);
	if (!IsValid(Enemy))
	{
		Mem->bRushing = false;
		return false;
	}

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	UHealthComponent* Health = Enemy->GetHealthComponent();
	if (!IsValid(DA) || !IsValid(Health))
	{
		Mem->bRushing = false;
		return false;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AActor* Target = BB ? Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget)) : nullptr;
	if (!IsValid(Target))
	{
		Mem->bRushing = false;
		return false;
	}

	if (Health->GetHealthPercent() <= DA->ShotgunRushAbortHealthFraction)
	{
		Mem->bRushing = false;
		return false;
	}

	const float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), Target->GetActorLocation());
	const float CommitRangeSq = DA->ShotgunCommitRange * DA->ShotgunCommitRange;
	const float ExitRangeSq = (DA->ShotgunCommitRange + DA->ShotgunCommitHysteresis)
		* (DA->ShotgunCommitRange + DA->ShotgunCommitHysteresis);

	if (Mem->bRushing)
	{
		// Stay rushing until target escapes the hysteresis band.
		if (DistSq > ExitRangeSq) Mem->bRushing = false;
	}
	else
	{
		// Commit only when inside the tight commit range.
		if (DistSq <= CommitRangeSq) Mem->bRushing = true;
	}

	return Mem->bRushing;
}

void UBTDecorator_ShotgunCommit::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FShotgunCommitMemory* Mem = reinterpret_cast<FShotgunCommitMemory*>(NodeMemory);
	const bool bCondition = CalculateRawConditionValue(OwnerComp, NodeMemory);

	if (bCondition != Mem->bLastCondition)
	{
		Mem->bLastCondition = bCondition;
		ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
	}
}

FString UBTDecorator_ShotgunCommit::GetStaticDescription() const
{
	return TEXT("Rush when target within ShotgunCommitRange AND health above abort fraction (hysteresis on exit; resets each engagement; tick-polled, aborts on change)");
}
