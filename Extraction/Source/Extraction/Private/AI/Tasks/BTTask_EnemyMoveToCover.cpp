// BTTask_EnemyMoveToCover — claims a cover slot and moves the enemy to it.

#include "BTTask_EnemyMoveToCover.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Navigation/PathFollowingComponent.h"
#include "CoverSlotTypes.h"

UBTTask_EnemyMoveToCover::UBTTask_EnemyMoveToCover()
{
	NodeName = TEXT("Enemy Move To Cover");
	bNotifyTick = true;
}

uint16 UBTTask_EnemyMoveToCover::GetInstanceMemorySize() const
{
	return sizeof(FMoveToCoverMemory);
}

EBTNodeResult::Type UBTTask_EnemyMoveToCover::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FMoveToCoverMemory* Mem = reinterpret_cast<FMoveToCoverMemory*>(NodeMemory);
	new (Mem) FMoveToCoverMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		ReleaseClaim(BB, Pawn);
		return EBTNodeResult::Failed;
	}

	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	const UEnemyArchetypeData* DA = Enemy ? Enemy->GetArchetypeData() : nullptr;
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	// Set combat speed for the move
	if (AEnemyCharacter* EnemyMutable = Cast<AEnemyCharacter>(Pawn))
		EnemyMutable->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	// Reuse an already-claimed slot if we still own it and it's still valid
	{
		AAICoverSlot* ExistingSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
		if (IsValid(ExistingSlot) && ExistingSlot->IsClaimedBy(Pawn))
		{
			BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);
			if (ExistingSlot->Height == ECoverHeight::Crouch)
				if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
			return EBTNodeResult::Succeeded;
		}
	}

	// Release any stale claim before searching for a new slot
	ReleaseClaim(BB, Pawn);

	UCoverRegistrySubsystem* Registry = Pawn->GetWorld()->GetSubsystem<UCoverRegistrySubsystem>();
	if (!Registry) return EBTNodeResult::Failed;

	AAICoverSlot* Slot = Registry->FindBestCoverFor(Pawn->GetActorLocation(), Target, DA->CoverSearchRadius, nullptr, Pawn);
	if (!Slot || !Slot->TryClaim(Pawn))
	{
		BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
		BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
		return EBTNodeResult::Failed;
	}

	Mem->bSlotClaimed = true;
	BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, Slot);

	const FVector PawnLoc = Pawn->GetActorLocation();
	FVector ArrivalPos = Slot->GetLocationAtAlpha(Slot->GetAlphaFromLocation(PawnLoc));
	ArrivalPos.Z = PawnLoc.Z;

	if (FVector::Dist(PawnLoc, ArrivalPos) <= 60.f)
	{
		BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);
		if (Slot->Height == ECoverHeight::Crouch)
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
		return EBTNodeResult::Succeeded;
	}

	Controller->MoveToLocation(ArrivalPos, 60.f, false, true, false, true);
	Mem->bMoveIssued = true;
	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyMoveToCover::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FMoveToCoverMemory* Mem = reinterpret_cast<FMoveToCoverMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	if (!Mem->bMoveIssued) return;

	UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
	if (!PF) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
	if (!IsValid(Slot))
	{
		ReleaseClaim(BB, Pawn);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	const FVector PawnLoc = Pawn->GetActorLocation();
	FVector ArrivalPos = Slot->GetLocationAtAlpha(Slot->GetAlphaFromLocation(PawnLoc));
	ArrivalPos.Z = PawnLoc.Z;
	const float Dist = FVector::Dist(PawnLoc, ArrivalPos);
	const EPathFollowingStatus::Type Status = PF->GetStatus();

	const bool bArrived = (Dist <= 80.f) || (Status == EPathFollowingStatus::Idle && Dist <= 200.f);
	if (!bArrived && Status == EPathFollowingStatus::Moving) return;

	if (bArrived)
	{
		BB->SetValueAsBool(AEnemyAIController::BB_HasCover, true);
		if (Slot->Height == ECoverHeight::Crouch)
			if (ACharacter* Char = Cast<ACharacter>(Pawn)) Char->Crouch();
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	// Path failed
	ReleaseClaim(BB, Pawn);
	return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
}

EBTNodeResult::Type UBTTask_EnemyMoveToCover::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (Controller) Controller->StopMovement();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ReleaseClaim(BB, Pawn);
	return EBTNodeResult::Aborted;
}

void UBTTask_EnemyMoveToCover::ReleaseClaim(UBlackboardComponent* BB, APawn* Pawn) const
{
	if (!BB) return;

	AAICoverSlot* Slot = Cast<AAICoverSlot>(BB->GetValueAsObject(AEnemyAIController::BB_CoverSlot));
	if (IsValid(Slot) && IsValid(Pawn))
		Slot->Release(Pawn);

	BB->SetValueAsObject(AEnemyAIController::BB_CoverSlot, nullptr);
	BB->SetValueAsBool(AEnemyAIController::BB_HasCover, false);
}

FString UBTTask_EnemyMoveToCover::GetStaticDescription() const
{
	return TEXT("Find and move to best cover slot from registry");
}
