// BT task — companion follows player in formation or sprints to reach them.

#include "BTTask_FollowPlayer.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "CompanionCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
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

	LastMoveTarget = FVector::ZeroVector;
	bIsIdling = false;

	return EBTNodeResult::InProgress;
}

void UBTTask_FollowPlayer::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!Controller) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AActor* Player = Cast<AActor>(OwnerComp.GetBlackboardComponent()->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Controller->GetPawn());
	if (!Companion) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const FVector PlayerLocation = Player->GetActorLocation();
	const FVector CompanionLocation = Companion->GetActorLocation();
	const float DistToPlayer = FVector::Dist(CompanionLocation, PlayerLocation);

	// Sprint mode: go directly to the player and Succeed on arrival so parent Sequence
	// (e.g. revive) can advance to the next task.
	if (bSprintToTarget)
	{
		if (DistToPlayer <= AcceptableRadius)
		{
			Controller->StopMovement();
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}

		Companion->SetSprinting(true);

		// Only re-issue move if player has shifted significantly — avoids restarting the
		// path every tick which can stutter-step the companion.
		if (FVector::Dist(PlayerLocation, LastMoveTarget) > 100.0f)
		{
			LastMoveTarget = PlayerLocation;
			Controller->MoveToLocation(PlayerLocation, AcceptableRadius * 0.5f, false, true, false, true);
		}
		return;
	}

	// --- Formation (non-sprint) mode: stay near the player indefinitely ---

	// Close enough to player — stop and idle
	if (DistToPlayer <= AcceptableRadius)
	{
		if (!bIsIdling)
		{
			Controller->StopMovement();
			bIsIdling = true;
		}
		return;
	}

	// Hysteresis — don't re-engage movement until outside double the radius
	if (bIsIdling && DistToPlayer < AcceptableRadius * 1.5f)
		return;

	bIsIdling = false;

	// Calculate formation offset using player's movement direction when moving,
	// or the vector from player to companion when stationary (keeps current side)
	const ACharacter* PlayerChar = Cast<ACharacter>(Player);
	FVector FormationDir;

	if (PlayerChar && PlayerChar->GetVelocity().SizeSquared() > 100.0f * 100.0f)
	{
		// Player is moving — formation behind their movement direction
		const FVector MoveDir = PlayerChar->GetVelocity().GetSafeNormal2D();
		const FVector MoveRight = FVector::CrossProduct(FVector::UpVector, MoveDir);
		FormationDir = PlayerLocation - MoveDir * FormationOffsetBack + MoveRight * FormationOffsetRight;
	}
	else
	{
		// Player stationary — just maintain distance, stay where we are relative to player
		const FVector ToCompanion = (CompanionLocation - PlayerLocation).GetSafeNormal2D();
		FormationDir = PlayerLocation + ToCompanion * FormationOffsetBack;
	}

	// Only re-issue move if target shifted significantly
	if (FVector::Dist(FormationDir, LastMoveTarget) < 200.0f)
		return;

	LastMoveTarget = FormationDir;

	Companion->SetSprinting(bSprintToTarget || DistToPlayer > SprintDistanceThreshold);

	Controller->MoveToLocation(FormationDir, AcceptableRadius * 0.5f, false, true, false, true);
}

FString UBTTask_FollowPlayer::GetStaticDescription() const
{
	return FString::Printf(TEXT("Follow player (offset: %.0f back, %.0f right)%s"),
		FormationOffsetBack, FormationOffsetRight,
		bSprintToTarget ? TEXT(" [SPRINT]") : TEXT(""));
}
