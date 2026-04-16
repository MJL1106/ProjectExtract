// BT task — companion follows player in formation or sprints to reach them.

#include "BTTask_FollowPlayer.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Navigation/PathFollowingComponent.h"

UBTTask_FollowPlayer::UBTTask_FollowPlayer()
{
	NodeName = TEXT("Follow Player");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_FollowPlayer::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AActor* Player = Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player)) return EBTNodeResult::Failed;

	return EBTNodeResult::InProgress;
}

void UBTTask_FollowPlayer::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!Controller) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AActor* Player = Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	ACharacter* Companion = Cast<ACharacter>(Controller->GetPawn());
	if (!Companion) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Calculate formation position relative to player
	const FVector PlayerLocation = Player->GetActorLocation();
	const FVector PlayerForward = Player->GetActorForwardVector();
	const FVector PlayerRight = Player->GetActorRightVector();

	const FVector FormationPos = PlayerLocation
		- PlayerForward * FormationOffsetBack
		+ PlayerRight * FormationOffsetRight;

	const float DistToFormation = FVector::Dist(Companion->GetActorLocation(), FormationPos);

	// Arrived at formation point
	if (DistToFormation <= AcceptableRadius)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	// Set movement speed
	if (UCharacterMovementComponent* MoveComp = Companion->GetCharacterMovement())
	{
		if (bSprintToTarget || DistToFormation > SprintDistanceThreshold)
			MoveComp->MaxWalkSpeed = SprintSpeed;
		else
			MoveComp->MaxWalkSpeed = WalkSpeed;
	}

	Controller->MoveToLocation(FormationPos, AcceptableRadius, false, true, false, true);
}

FString UBTTask_FollowPlayer::GetStaticDescription() const
{
	return FString::Printf(TEXT("Follow player (offset: %.0f back, %.0f right)%s"),
		FormationOffsetBack, FormationOffsetRight,
		bSprintToTarget ? TEXT(" [SPRINT]") : TEXT(""));
}
