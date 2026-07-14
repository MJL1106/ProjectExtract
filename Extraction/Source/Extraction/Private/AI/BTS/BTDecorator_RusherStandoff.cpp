// BTDecorator_RusherStandoff — tick-observer: circle branch active when the target is inside the
// standoff band (enter at RushStandoffEnterRange, exit past EnterRange + Hysteresis).

#include "BTDecorator_RusherStandoff.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

UBTDecorator_RusherStandoff::UBTDecorator_RusherStandoff()
{
	NodeName = TEXT("Rusher Standoff");
	bNotifyTick = true;
	FlowAbortMode = EBTFlowAbortMode::Both;
}

uint16 UBTDecorator_RusherStandoff::GetInstanceMemorySize() const
{
	return sizeof(FStandoffMemory);
}

AEnemyCharacter* UBTDecorator_RusherStandoff::ResolveEnemy(UBehaviorTreeComponent& OwnerComp, FStandoffMemory* Mem) const
{
	AEnemyCharacter* Enemy = Mem->CachedEnemy.Get();
	if (IsValid(Enemy)) return Enemy;

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	Enemy = Cast<AEnemyCharacter>(Pawn);
	if (IsValid(Enemy)) Mem->CachedEnemy = Enemy;

	return Enemy;
}

void UBTDecorator_RusherStandoff::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	FStandoffMemory* Mem = reinterpret_cast<FStandoffMemory*>(NodeMemory);
	Mem->bCircling = false;
	Mem->bLastCondition = false;
	Mem->CachedEnemy.Reset();
}

void UBTDecorator_RusherStandoff::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	FStandoffMemory* Mem = reinterpret_cast<FStandoffMemory*>(NodeMemory);
	Mem->CachedEnemy.Reset();
}

bool UBTDecorator_RusherStandoff::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	FStandoffMemory* Mem = reinterpret_cast<FStandoffMemory*>(NodeMemory);

	AEnemyCharacter* Enemy = ResolveEnemy(OwnerComp, Mem);
	if (!IsValid(Enemy)) { Mem->bCircling = false; return false; }

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) { Mem->bCircling = false; return false; }

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AActor* Target = BB ? Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget)) : nullptr;
	if (!IsValid(Target)) { Mem->bCircling = false; return false; }

	// A committed charge owns the rusher — exclude the circle branch so the three phases stay
	// mutually exclusive regardless of BT child order (RushCommit mirrors its latch to this flag).
	if (BB->GetValueAsBool(AEnemyAIController::BB_RusherCharging))
	{
		Mem->bCircling = false;
		return false;
	}

	const float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), Target->GetActorLocation());
	const float EnterSq = FMath::Square(DA->RushStandoffEnterRange);
	const float ExitSq = FMath::Square(DA->RushStandoffEnterRange + DA->RushStandoffHysteresis);

	if (Mem->bCircling)
	{
		if (DistSq > ExitSq) Mem->bCircling = false;
	}
	else
	{
		if (DistSq <= EnterSq) Mem->bCircling = true;
	}

	return Mem->bCircling;
}

void UBTDecorator_RusherStandoff::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FStandoffMemory* Mem = reinterpret_cast<FStandoffMemory*>(NodeMemory);
	const bool bCondition = CalculateRawConditionValue(OwnerComp, NodeMemory);

	if (bCondition != Mem->bLastCondition)
	{
		Mem->bLastCondition = bCondition;
		ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
	}
}

FString UBTDecorator_RusherStandoff::GetStaticDescription() const
{
	return TEXT("Circle-strafe when target within RushStandoffEnterRange (hysteresis on exit)");
}
