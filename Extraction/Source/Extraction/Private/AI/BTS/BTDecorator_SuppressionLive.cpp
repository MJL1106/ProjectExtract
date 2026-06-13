// BTDecorator_SuppressionLive — tick-observer: flanker branch active only while suppression is live.

#include "BTDecorator_SuppressionLive.h"
#include "EnemyCharacter.h"
#include "EnemySquad.h"
#include "EnemySquadSubsystem.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"

UBTDecorator_SuppressionLive::UBTDecorator_SuppressionLive()
{
	NodeName = TEXT("Suppression Live");
	bNotifyTick = true;
	FlowAbortMode = EBTFlowAbortMode::Both;
}

uint16 UBTDecorator_SuppressionLive::GetInstanceMemorySize() const
{
	return sizeof(FSuppressionLiveMemory);
}

UEnemySquad* UBTDecorator_SuppressionLive::ResolveSquad(UBehaviorTreeComponent& OwnerComp, FSuppressionLiveMemory* Mem) const
{
	UEnemySquad* Squad = Mem->CachedSquad.Get();
	if (IsValid(Squad)) return Squad;

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return nullptr;

	UWorld* World = Enemy->GetWorld();
	if (!World) return nullptr;

	UEnemySquadSubsystem* SquadSub = World->GetSubsystem<UEnemySquadSubsystem>();
	Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
	if (Squad) Mem->CachedSquad = Squad;

	return Squad;
}

bool UBTDecorator_SuppressionLive::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	FSuppressionLiveMemory* Mem = reinterpret_cast<FSuppressionLiveMemory*>(NodeMemory);
	UEnemySquad* Squad = ResolveSquad(OwnerComp, Mem);
	if (!Squad) return false;

	return Squad->IsBoundingActive() && Squad->IsSuppressionLive();
}

void UBTDecorator_SuppressionLive::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FSuppressionLiveMemory* Mem = reinterpret_cast<FSuppressionLiveMemory*>(NodeMemory);
	const bool bCondition = CalculateRawConditionValue(OwnerComp, NodeMemory);

	if (bCondition != Mem->bLastCondition)
	{
		Mem->bLastCondition = bCondition;
		ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
	}
}

FString UBTDecorator_SuppressionLive::GetStaticDescription() const
{
	return TEXT("Squad bounding active AND suppression live (tick-polled, aborts on change)");
}
