// BT task — companion sprints to downed player and revives them after a hold timer.

#include "BTTask_RevivePlayer.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "ExtractionCharacter.h"
#include "CompanionCharacter.h"
#include "HealthComponent.h"

UBTTask_RevivePlayer::UBTTask_RevivePlayer()
{
	NodeName = TEXT("Revive Player");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_RevivePlayer::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AExtractionCharacter* Player = Cast<AExtractionCharacter>(
		OwnerComp.GetBlackboardComponent()->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player) || !Player->GetIsDBNO()) return EBTNodeResult::Failed;

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC) return EBTNodeResult::Failed;
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	if (!Companion) return EBTNodeResult::Failed;

	// Stop any combat activity
	Companion->StopWeaponFire();

	ReviveElapsed = 0.0f;
	bIsHoldingRevive = false;

	UE_LOG(LogTemp, Log, TEXT("Companion starting revive sequence for %s"), *GetNameSafe(Player));

	return EBTNodeResult::InProgress;
}

void UBTTask_RevivePlayer::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	if (!Controller) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Controller->GetPawn());
	if (!Companion) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AExtractionCharacter* Player = Cast<AExtractionCharacter>(
		OwnerComp.GetBlackboardComponent()->GetValueAsObject(PlayerActorKey.SelectedKeyName));

	if (!IsValid(Player))
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Player already revived or died — done
	if (!Player->GetIsDBNO())
	{
		UE_LOG(LogTemp, Log, TEXT("Companion revive cancelled — player no longer DBNO"));
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	const float DistToPlayer = FVector::Dist(Companion->GetActorLocation(), Player->GetActorLocation());

	// Not close enough yet — keep moving (FollowPlayer task in revive sequence handles this,
	// but if we somehow get here while far, fail so the sequence re-runs the follow task)
	if (DistToPlayer > Companion->ReviveProximityRadius)
	{
		bIsHoldingRevive = false;
		ReviveElapsed = 0.0f;
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// In range — stop moving and hold for revive
	if (!bIsHoldingRevive)
	{
		Controller->StopMovement();
		bIsHoldingRevive = true;

		// Face the player
		const FRotator LookAt = (Player->GetActorLocation() - Companion->GetActorLocation()).Rotation();
		Companion->SetActorRotation(FRotator(0.0f, LookAt.Yaw, 0.0f));
	}

	ReviveElapsed += DeltaSeconds;

	if (ReviveElapsed >= Companion->ReviveDuration)
	{
		if (Companion->HasAuthority() && IsValid(Player))
		{
			Player->ExitDBNO();
		}

		UE_LOG(LogTemp, Log, TEXT("Companion revived %s"), *GetNameSafe(Player));
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}

FString UBTTask_RevivePlayer::GetStaticDescription() const
{
	return TEXT("Revive downed player (highest priority)");
}
