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

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

	if (!IsValid(Target))
	{
		BB->SetValueAsBool(AEnemyAIController::BB_HasLineOfSight, false);
		BB->SetValueAsBool(AEnemyAIController::BB_TargetInRange, false);
		return;
	}

	// LOS check — body-point resolver excludes head so head-only peek never opens fire.
	const FVector EyeLocation = Pawn->GetPawnViewLocation();
	FVector VisiblePoint;
	const bool bHasLOS = AITargeting::GetVisibleBodyPoint(Target, EyeLocation, Pawn, VisiblePoint);
	BB->SetValueAsBool(AEnemyAIController::BB_HasLineOfSight, bHasLOS);

	// Range check against archetype EngageRangeMax
	const FVector TargetLoc = Target->GetActorLocation();
	bool bInRange = false;
	if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn))
	{
		if (const UEnemyArchetypeData* DA = Enemy->GetArchetypeData())
			bInRange = FVector::Dist(Pawn->GetActorLocation(), TargetLoc) <= DA->EngageRangeMax;
	}
	BB->SetValueAsBool(AEnemyAIController::BB_TargetInRange, bInRange);
}
