// BT task — companion commanded explore execution.

#include "AI/Tasks/BTTask_CompanionExplore.h"
#include "AIController.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "Companion/CompanionCharacter.h"
#include "Components/HealthComponent.h"
#include "Enemy/EnemyCharacter.h"
#include "World/Lootable.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompanionExplore, Log, All);

// Any live enemy within the radius = the area is hot; the loot chain must not hold the command
// slot open (the command selector outranks the combat branch). Mirrors BTTask_CompanionBreach.
static bool ExploreAnyLiveEnemyWithin(UWorld* World, const FVector& Center, float Radius)
{
	if (!World) return false;
	const float RadiusSq = FMath::Square(Radius);
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		const AEnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy)) continue;
		const UHealthComponent* Health = Enemy->GetHealthComponent();
		if (!IsValid(Health) || Health->IsDead()) continue;
		if (FVector::DistSquared(Enemy->GetActorLocation(), Center) <= RadiusSq) return true;
	}
	return false;
}

// Nearest still-lootable container within the radius (mirrors BTTask_CompanionLoot's sweep filter).
static AActor* ExploreFindNearestLootable(UWorld* World, const FVector& Center, float Radius)
{
	if (!World) return nullptr;

	TArray<AActor*> Lootables;
	UGameplayStatics::GetAllActorsWithInterface(World, ULootable::StaticClass(), Lootables);

	AActor* Best = nullptr;
	float BestDistSq = FMath::Square(Radius);
	for (AActor* Candidate : Lootables)
	{
		if (!IsValid(Candidate) || !ILootable::Execute_CanLoot(Candidate)) continue;
		const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), Center);
		if (DistSq > BestDistSq) continue;
		BestDistSq = DistSq;
		Best = Candidate;
	}
	return Best;
}

UBTTask_CompanionExplore::UBTTask_CompanionExplore()
{
	NodeName = TEXT("Companion Explore");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_CompanionExplore::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ExploreLocation = FVector::ZeroVector;
	MoveElapsed = 0.f;

	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!IsValid(AIC) || !IsValid(BB))
	{
		ClearIfStillExplore(OwnerComp);
		return EBTNodeResult::Failed;
	}

	ExploreLocation = BB->GetValueAsVector(ACompanionAIController::BB_CommandTargetLocation);

	// Full path only — a partial path "succeeds" at the nearest reachable point, which would stamp
	// the engagement grant around a spot the companion never reached.
	FAIMoveRequest MoveReq(ExploreLocation);
	MoveReq.SetAcceptanceRadius(ArriveAcceptRadius);
	MoveReq.SetAllowPartialPath(false);
	MoveReq.SetProjectGoalLocation(true);
	MoveReq.SetUsePathfinding(true);

	if (AIC->MoveTo(MoveReq) == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogCompanionExplore, Warning, TEXT("ExecuteTask: move to %s failed"), *ExploreLocation.ToCompactString());
		ClearIfStillExplore(OwnerComp);
		return EBTNodeResult::Failed;
	}

	UE_LOG(LogCompanionExplore, Log, TEXT("ExecuteTask: exploring toward %s"), *ExploreLocation.ToCompactString());
	return EBTNodeResult::InProgress;
}

void UBTTask_CompanionExplore::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	APawn* Pawn = IsValid(AIC) ? AIC->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		ClearIfStillExplore(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Combat breaks off the walk: while a command is active the combat branch cannot run (the
	// command selector outranks it) — same contract as the breach/loot tasks.
	if (const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
	{
		if (IsValid(Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget))))
		{
			UE_LOG(LogCompanionExplore, Log, TEXT("TickTask: combat target acquired — breaking off explore"));
			ClearIfStillExplore(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}
	}

	// Latest explore ping wins mid-walk: the command enum stays Explore so no observer-abort fires —
	// watch the location key ourselves and re-route.
	if (const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
	{
		const FVector BBLocation = BB->GetValueAsVector(ACompanionAIController::BB_CommandTargetLocation);
		if (!BBLocation.Equals(ExploreLocation, 1.f))
		{
			UE_LOG(LogCompanionExplore, Log, TEXT("TickTask: re-routing to fresh explore ping %s"), *BBLocation.ToCompactString());
			ExploreLocation = BBLocation;
			MoveElapsed = 0.f;
			AIC->StopMovement();
			FAIMoveRequest ReRouteReq(ExploreLocation);
			ReRouteReq.SetAcceptanceRadius(ArriveAcceptRadius);
			ReRouteReq.SetAllowPartialPath(false);
			ReRouteReq.SetProjectGoalLocation(true);
			ReRouteReq.SetUsePathfinding(true);
			if (AIC->MoveTo(ReRouteReq) == EPathFollowingRequestResult::Failed)
			{
				ClearIfStillExplore(OwnerComp);
				FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			}
			return;
		}
	}

	MoveElapsed += DeltaSeconds;
	if (AIC->GetMoveStatus() != EPathFollowingStatus::Idle && MoveElapsed < MoveTimeout) return;

	if (AIC->GetMoveStatus() != EPathFollowingStatus::Idle) AIC->StopMovement();
	FinishExplore(OwnerComp, AIC, Pawn);
}

void UBTTask_CompanionExplore::FinishExplore(UBehaviorTreeComponent& OwnerComp, ACompanionAIController* AIC, APawn* Pawn)
{
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn);
	const UCompanionTuningDataAsset* Tuning = AIC->GetTuning();
	const float LootRadius = Tuning ? Tuning->ExploreLootRadius : 1200.f;
	const float GrantDuration = Tuning ? Tuning->ExploreEngagementDuration : 8.f;

	// A timeout finish can land far from the ping — never stamp the grant or chain loot around a
	// spot the companion didn't actually reach.
	const bool bArrived = FVector::Dist2D(Pawn->GetActorLocation(), ExploreLocation) <= ArriveAcceptRadius * 2.f;

	// The player ordered the companion into this spot, so it clears the area: unaware enemies
	// nearby become engageable (grant) and a quiet area chains a loot sweep. Unbroken Stealth
	// moves quietly instead — no grant, no auto-loot.
	bool bChainedLoot = false;
	if (bArrived && IsValid(Companion) && !Companion->IsStealthActive() && LootRadius > 0.f)
	{
		Companion->SetPostBreachEngagement(ExploreLocation, LootRadius, GrantDuration);

		if (!ExploreAnyLiveEnemyWithin(Companion->GetWorld(), ExploreLocation, LootRadius))
		{
			if (AActor* Container = ExploreFindNearestLootable(Companion->GetWorld(), ExploreLocation, LootRadius))
			{
				UE_LOG(LogCompanionExplore, Log, TEXT("FinishExplore: area quiet — chaining loot sweep to %s"),
					*GetNameSafe(Container));
				AIC->IssueCommand(ECompanionCommand::Loot, ETakedownMethod::Knife,
					Container, Container->GetActorLocation());
				bChainedLoot = true;
			}
		}
	}

	if (!bChainedLoot)
		AIC->ClearActiveCommand();

	FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
}

EBTNodeResult::Type UBTTask_CompanionExplore::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ClearIfStillExplore(OwnerComp);
	return EBTNodeResult::Aborted;
}

void UBTTask_CompanionExplore::ClearIfStillExplore(UBehaviorTreeComponent& OwnerComp)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(AIC)) return;

	AIC->StopMovement();

	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	const ECompanionCommand Active = IsValid(BB)
		? static_cast<ECompanionCommand>(BB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand))
		: ECompanionCommand::None;
	if (!IsValid(BB) || Active == ECompanionCommand::Explore)
		AIC->ClearActiveCommand();
}

FString UBTTask_CompanionExplore::GetStaticDescription() const
{
	return FString::Printf(TEXT("Explore: walk to the pinged spot (accept %.0f cm), grant + loot chain on arrival"), ArriveAcceptRadius);
}
