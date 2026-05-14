// BT task — companion follows player in formation or sprints to reach them.

#include "BTTask_FollowPlayer.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "CompanionCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"

UBTTask_FollowPlayer::UBTTask_FollowPlayer()
{
	NodeName = TEXT("Follow Player");
	bNotifyTick = true;
	bCreateNodeInstance = true; // bIsIdling / LastMoveTarget are per-instance state — must stay true
}

EBTNodeResult::Type UBTTask_FollowPlayer::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	const UCompanionTuningDataAsset* T = AIC ? AIC->GetTuning() : nullptr;
	if (!T) return EBTNodeResult::Failed;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	AActor* Player = Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player)) return EBTNodeResult::Failed;

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	if (!Companion) return EBTNodeResult::Failed;

	CachedController = AIC;
	CachedCompanion = Companion;

	LastMoveTarget = FVector::ZeroVector;
	bIsIdling = false;

	UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] ExecuteTask START — bSprintToTarget=%d"), bSprintToTarget ? 1 : 0);

	// Clear any stale sprint flag from a prior abort (e.g. combat or revive re-entry).
	// Skip for sprint-to-target so the revive branch keeps sprint speed on entry.
	if (!bSprintToTarget)
		Companion->SetSprinting(false);

	return EBTNodeResult::InProgress;
}

void UBTTask_FollowPlayer::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ACompanionAIController* Controller = CachedController.Get();
	ACompanionCharacter* Companion = CachedCompanion.Get();
	if (!Controller || !Companion) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const UCompanionTuningDataAsset* T = Controller->GetTuning();
	if (!T) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AActor* Player = Cast<AActor>(BB->GetValueAsObject(PlayerActorKey.SelectedKeyName));
	if (!IsValid(Player)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const FVector PlayerLocation = Player->GetActorLocation();
	const FVector CompanionLocation = Companion->GetActorLocation();
	const float DistToPlayer = FVector::Dist(CompanionLocation, PlayerLocation);

	UE_LOG(LogCompanion, VeryVerbose, TEXT("[FollowPlayer] Tick: Dist=%.0f bIsIdling=%d Sprint=%d MaxWalkSpeed=%.0f Vel=%.0f"),
		DistToPlayer, bIsIdling ? 1 : 0,
		Companion->IsSprinting() ? 1 : 0,
		Companion->GetCharacterMovement() ? Companion->GetCharacterMovement()->MaxWalkSpeed : -1.0f,
		Companion->GetVelocity().Size2D());

	// Sprint mode: go directly to the player and Succeed on arrival so parent Sequence
	// (e.g. revive) can advance to the next task.
	if (bSprintToTarget)
	{
		if (DistToPlayer <= T->AcceptableRadius)
		{
			UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] SPRINT-TO-TARGET arrived (Dist=%.0f) — Succeed"), DistToPlayer);
			Controller->StopMovement();
			Companion->SetSprinting(false);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}

		Companion->SetSprinting(true);

		// Only re-issue move if player has shifted significantly — avoids restarting the
		// path every tick which can stutter-step the companion.
		if (FVector::Dist(PlayerLocation, LastMoveTarget) > 100.0f)
		{
			LastMoveTarget = PlayerLocation;
			Controller->MoveToLocation(PlayerLocation, T->AcceptableRadius * 0.5f, false, true, false, true);
		}
		return;
	}

	// --- Formation (non-sprint) mode: stay near the player indefinitely ---

	// Close enough to player — stop and idle
	if (DistToPlayer <= T->AcceptableRadius)
	{
		if (!bIsIdling)
		{
			UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] >>> ENTER IDLE branch (Dist=%.0f)"), DistToPlayer);
			Controller->StopMovement();
			Companion->SetSprinting(false);
			bIsIdling = true;
		}
		return;
	}

	// Hysteresis — don't re-engage movement until outside double the radius
	if (bIsIdling && DistToPlayer < T->AcceptableRadius * 1.5f)
	{
		UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] HYSTERESIS return (Dist=%.0f, idling, no SetSprinting)"), DistToPlayer);
		return;
	}

	if (bIsIdling)
		UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] >>> EXIT IDLE branch (Dist=%.0f, resuming formation)"), DistToPlayer);

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
		FormationDir = PlayerLocation - MoveDir * T->FormationOffsetBack + MoveRight * T->FormationOffsetRight;
	}
	else
	{
		// Player stationary — just maintain distance, stay where we are relative to player
		const FVector ToCompanion = (CompanionLocation - PlayerLocation).GetSafeNormal2D();
		FormationDir = PlayerLocation + ToCompanion * T->FormationOffsetBack;
	}

	const bool bWantSprint = DistToPlayer > T->SprintDistanceThreshold;
	UE_LOG(LogCompanion, VeryVerbose, TEXT("[FollowPlayer] FORMATION branch: Dist=%.0f Thresh=%.0f -> SetSprinting(%d)"),
		DistToPlayer, T->SprintDistanceThreshold, bWantSprint ? 1 : 0);

	Companion->SetSprinting(bWantSprint);

	// Only re-issue move if target shifted significantly
	if (FVector::Dist(FormationDir, LastMoveTarget) < 200.0f)
		return;

	LastMoveTarget = FormationDir;

	Controller->MoveToLocation(FormationDir, T->AcceptableRadius * 0.5f, false, true, false, true);
}

void UBTTask_FollowPlayer::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	UE_LOG(LogCompanion, Log, TEXT("[FollowPlayer] OnTaskFinished (result=%d) — clearing sprint"), (int32)TaskResult);

	if (ACompanionCharacter* Companion = CachedCompanion.Get())
		Companion->SetSprinting(false);

	CachedController.Reset();
	CachedCompanion.Reset();

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UBTTask_FollowPlayer::GetStaticDescription() const
{
	return FString::Printf(TEXT("Follow Player%s"), bSprintToTarget ? TEXT(" [SPRINT]") : TEXT(""));
}
