// BTTask_EnemyMoveToCover — claims a cover slot and moves the enemy to it.

#include "BTTask_EnemyMoveToCover.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "AICoverSlot.h"
#include "CoverRegistrySubsystem.h"
#include "EnemySquad.h"
#include "EnemySquadSubsystem.h"
#include "HealthComponent.h"
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

	// Point-blank override: skip cover entirely so the selector falls through to open-ground fire
	if (DA->PointBlankFireRange > 0.f)
	{
		const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), Target->GetActorLocation());
		if (DistSq <= FMath::Square(DA->PointBlankFireRange))
		{
			ReleaseClaim(BB, Pawn);
			return EBTNodeResult::Failed;
		}
	}

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

	AAICoverSlot* Slot = ScoreSlotsWithSpacing(Pawn, Enemy, Target, DA, Registry);
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

	const EPathFollowingRequestResult::Type MoveResult = Controller->MoveToLocation(ArrivalPos, 60.f, false, true, false, true);
	UE_LOG(LogEnemyAI, Verbose, TEXT("[COVER] %s MoveTo (%.0f,%.0f,%.0f) dist=%.0f result=%d (0=Failed 1=AlreadyAtGoal 2=RequestSuccessful)"),
		*Pawn->GetName(), ArrivalPos.X, ArrivalPos.Y, ArrivalPos.Z, FVector::Dist(PawnLoc, ArrivalPos), (int32)MoveResult);
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

AAICoverSlot* UBTTask_EnemyMoveToCover::ScoreSlotsWithSpacing(APawn* Pawn, const AEnemyCharacter* Enemy,
	AActor* Target, const UEnemyArchetypeData* DA, UCoverRegistrySubsystem* Registry) const
{
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector TargetLoc = Target->GetActorLocation();

	TArray<AAICoverSlot*> Candidates;
	Candidates.Reserve(16);
	Registry->GetSlotsInRadius(PawnLoc, DA->CoverSearchRadius, Candidates);

	// Gather living squadmate claimed-slot positions (or pawn positions as fallback) for spacing
	TArray<FVector> AllyPositions;
	UEnemySquadSubsystem* SquadSub = Pawn->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
	UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;

	if (Squad)
	{
		const TArray<TWeakObjectPtr<AEnemyCharacter>>& Members = Squad->GetMembers();
		AllyPositions.Reserve(Members.Num());
		for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
		{
			AEnemyCharacter* Ally = M.Get();
			if (!IsValid(Ally) || Ally == Pawn) continue;
			UHealthComponent* AllyHP = Ally->GetHealthComponent();
			if (IsValid(AllyHP) && AllyHP->IsDead()) continue;

			// Prefer claimed cover slot position over pawn position
			FVector Pos = Ally->GetActorLocation();
			AEnemyAIController* AllyAIC = Cast<AEnemyAIController>(Ally->GetController());
			if (AllyAIC)
			{
				UBlackboardComponent* AllyBB = AllyAIC->GetBlackboardComponent();
				AAICoverSlot* AllySlot = AllyBB ? Cast<AAICoverSlot>(AllyBB->GetValueAsObject(AEnemyAIController::BB_CoverSlot)) : nullptr;
				if (IsValid(AllySlot)) Pos = AllySlot->GetActorLocation();
			}
			AllyPositions.Add(Pos);
		}
	}

	// LOS trace setup (matches FindBestCoverFor)
	UWorld* World = Pawn->GetWorld();
	FCollisionQueryParams LoSParams(SCENE_QUERY_STAT(EnemyCoverLoS), false);
	LoSParams.AddIgnoredActor(Target);

	const float MinSpacing = DA->MinAllySpacing;
	AAICoverSlot* BestSlot = nullptr;
	float BestScore = -1.f;

	for (AAICoverSlot* Slot : Candidates)
	{
		if (!IsValid(Slot) || Slot->IsClaimed()) continue;

		// Post-vacate cooldown (anti snap-back)
		if (Slot->IsOnPostVacateCooldownFor(Pawn, 0.f)) continue;

		// Hide-only stand cover reject
		if (Slot->Height == ECoverHeight::Stand && !Slot->bIsPeekableCornerStart && !Slot->bIsPeekableCornerEnd)
			continue;

		// Target must be within fire arc
		if (!Slot->IsTargetInFireArc(TargetLoc)) continue;

		// LOS check — at least one position must see the target
		{
			constexpr float EyeHeight = 150.f;
			constexpr float ApexProbeDist = 100.f;
			const FVector SlotEye = Slot->GetActorLocation() + FVector(0.f, 0.f, EyeHeight);
			FHitResult Hit;
			bool bHasLOS = !World->LineTraceSingleByChannel(Hit, SlotEye, TargetLoc, ECC_Visibility, LoSParams)
				|| Hit.GetActor() == Target;

			if (!bHasLOS && Slot->Height == ECoverHeight::Stand)
			{
				const FVector LineDir = Slot->GetLineDirection();
				if (Slot->bIsPeekableCornerStart)
				{
					const FVector Apex = Slot->GetLeftEdge() + (-LineDir) * ApexProbeDist + FVector(0.f, 0.f, EyeHeight);
					bHasLOS = !World->LineTraceSingleByChannel(Hit, Apex, TargetLoc, ECC_Visibility, LoSParams)
						|| Hit.GetActor() == Target;
				}
				if (!bHasLOS && Slot->bIsPeekableCornerEnd)
				{
					const FVector Apex = Slot->GetRightEdge() + LineDir * ApexProbeDist + FVector(0.f, 0.f, EyeHeight);
					bHasLOS = !World->LineTraceSingleByChannel(Hit, Apex, TargetLoc, ECC_Visibility, LoSParams)
						|| Hit.GetActor() == Target;
				}
			}

			if (!bHasLOS) continue;
		}

		float Score = UCoverRegistrySubsystem::ScoreSlotFor(Slot, PawnLoc, Target, DA->CoverSearchRadius);
		if (Score < 0.f) continue;

		// Penalize slots too close to living squadmates' claimed positions
		if (MinSpacing > 0.f)
		{
			const FVector SlotLoc = Slot->GetActorLocation();
			for (const FVector& AllyPos : AllyPositions)
			{
				if (FVector::Dist(SlotLoc, AllyPos) < MinSpacing)
				{
					Score *= 0.2f;
					break;
				}
			}
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestSlot = Slot;
		}
	}

	return BestSlot;
}

FString UBTTask_EnemyMoveToCover::GetStaticDescription() const
{
	return TEXT("Find and move to best cover slot from registry");
}
