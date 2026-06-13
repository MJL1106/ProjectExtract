// BTTask_EnemyMelee — instant melee strike if target is within MeleeRange; returns Failed otherwise.

#include "BTTask_EnemyMelee.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

UBTTask_EnemyMelee::UBTTask_EnemyMelee()
{
	NodeName = TEXT("Enemy Melee");
	bNotifyTick = false;
}

EBTNodeResult::Type UBTTask_EnemyMelee::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
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

	const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), Target->GetActorLocation());
	const float MeleeRangeSq = DA->MeleeRange * DA->MeleeRange;

	if (DistSq > MeleeRangeSq) return EBTNodeResult::Failed;

	return Enemy->PerformMelee(Target) ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}

FString UBTTask_EnemyMelee::GetStaticDescription() const
{
	return TEXT("Instant melee strike if target within MeleeRange; else Failed");
}
