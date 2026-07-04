// BTService_EnemyCombat — validates target liveness; writes HasLineOfSight, TargetInRange.

#include "BTService_EnemyCombat.h"
#include "AI/AITargetingStatics.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

UBTService_EnemyCombat::UBTService_EnemyCombat()
{
	NodeName = TEXT("Enemy Combat Service");
	Interval = 0.25f;
	RandomDeviation = 0.05f;
}

void UBTService_EnemyCombat::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Controller || !Pawn) return;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

	if (!IsValid(Target))
	{
		BB->SetValueAsBool(AEnemyAIController::BB_HasLineOfSight, false);
		BB->SetValueAsBool(AEnemyAIController::BB_TargetInRange, false);
		if (Enemy) Enemy->SetHasTargetLOS(false);
		return;
	}

	// LOS check — body-point resolver excludes head so head-only peek never opens fire,
	// except snipers vs a standing target (sniper+standing exception).
	const FVector EyeLocation = Pawn->GetPawnViewLocation();
	FVector VisiblePoint;
	const bool bAllowHead = AITargeting::ShouldIncludeHeadForObserver(Pawn, Target);
	const bool bHasLOS = AITargeting::GetVisibleBodyPoint(Target, EyeLocation, Pawn, VisiblePoint, bAllowHead);
	BB->SetValueAsBool(AEnemyAIController::BB_HasLineOfSight, bHasLOS);

	// Mirror LOS to the character for the anim instance's in-cover aim gate.
	if (Enemy) Enemy->SetHasTargetLOS(bHasLOS);

	// Range check against archetype EngageRangeMax
	const FVector TargetLoc = Target->GetActorLocation();
	bool bInRange = false;
	if (IsValid(Enemy))
	{
		if (const UEnemyArchetypeData* DA = Enemy->GetArchetypeData())
			bInRange = FVector::Dist(Pawn->GetActorLocation(), TargetLoc) <= DA->EngageRangeMax;
	}
	BB->SetValueAsBool(AEnemyAIController::BB_TargetInRange, bInRange);
}
