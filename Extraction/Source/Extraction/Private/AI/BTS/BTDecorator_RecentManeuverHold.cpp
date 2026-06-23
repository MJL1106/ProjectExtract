// BTDecorator_RecentManeuverHold — condition: TRUE when NOT in a maneuver hold window
// (i.e. allows the branch). FALSE during the window (skips Flank/MoveToCover so the
// grunt drops straight to Enemy Combat Fire and fights in place).

#include "BTDecorator_RecentManeuverHold.h"
#include "EnemyAIController.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

UBTDecorator_RecentManeuverHold::UBTDecorator_RecentManeuverHold()
{
	NodeName = TEXT("Not In Maneuver Hold");
	FlowAbortMode = EBTFlowAbortMode::None;
}

bool UBTDecorator_RecentManeuverHold::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!IsValid(BB)) return true;

	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!IsValid(Controller)) return true;

	APawn* Pawn = Controller->GetPawn();
	if (!IsValid(Pawn)) return true;

	UWorld* World = Pawn->GetWorld();
	if (!World) return true;

	const float HoldUntil = BB->GetValueAsFloat(AEnemyAIController::BB_ManeuverHoldUntil);
	return World->GetTimeSeconds() >= HoldUntil;
}

FString UBTDecorator_RecentManeuverHold::GetStaticDescription() const
{
	return TEXT("Passes when ManeuverHoldUntil has expired (not in post-maneuver engage-in-place window)");
}
