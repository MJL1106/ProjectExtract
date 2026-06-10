// BTTask_OfficerCommand — officer holds behind allied centroid and fires when LOS is available.

#include "BTTask_OfficerCommand.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"

UBTTask_OfficerCommand::UBTTask_OfficerCommand()
{
	NodeName = TEXT("Officer Command");
	bNotifyTick = true;
}

uint16 UBTTask_OfficerCommand::GetInstanceMemorySize() const
{
	return sizeof(FOfficerCommandMemory);
}

EBTNodeResult::Type UBTTask_OfficerCommand::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FOfficerCommandMemory* Mem = reinterpret_cast<FOfficerCommandMemory*>(NodeMemory);
	new (Mem) FOfficerCommandMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
	Controller->SetFocus(Target);

	// Immediately compute and move to hold position
	FVector HoldPos;
	if (ComputeHoldPosition(Pawn, Target, HoldPos))
		Controller->MoveToLocation(HoldPos, 80.f, false, true, false, true);

	Mem->RepositionTimer = RepositionInterval;

	return EBTNodeResult::InProgress;
}

void UBTTask_OfficerCommand::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FOfficerCommandMemory* Mem = reinterpret_cast<FOfficerCommandMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Opportunistic fire when LOS is available
	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();

	if (bHasLOS)
	{
		Enemy->SetAimTarget(Target);
		if (IsValid(Weapon) && !Mem->bFiring)
		{
			Weapon->StartFiring();
			Mem->bFiring = true;
		}
	}
	else
	{
		if (IsValid(Weapon) && Mem->bFiring)
		{
			Weapon->StopFiring();
			Mem->bFiring = false;
		}
		Enemy->SetAimTarget(nullptr);
	}

	// Re-evaluate hold position on interval
	Mem->RepositionTimer -= DeltaSeconds;
	if (Mem->RepositionTimer <= 0.f)
	{
		FVector HoldPos;
		if (ComputeHoldPosition(Pawn, Target, HoldPos))
			Controller->MoveToLocation(HoldPos, 80.f, false, true, false, true);

		Mem->RepositionTimer = RepositionInterval;
	}
}

EBTNodeResult::Type UBTTask_OfficerCommand::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FOfficerCommandMemory* Mem = reinterpret_cast<FOfficerCommandMemory*>(NodeMemory);
	CleanUp(OwnerComp, Mem);
	return EBTNodeResult::Aborted;
}

void UBTTask_OfficerCommand::CleanUp(UBehaviorTreeComponent& OwnerComp, FOfficerCommandMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon) && Mem && Mem->bFiring)
		Weapon->StopFiring();

	Enemy->SetAimTarget(nullptr);

	if (Controller)
	{
		Controller->StopMovement();
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

bool UBTTask_OfficerCommand::ComputeHoldPosition(APawn* Pawn, AActor* Target, FVector& OutPos) const
{
	if (!IsValid(Pawn) || !IsValid(Target)) return false;

	// Gather allied team-1 pawns within scan radius
	TArray<FVector> AllyLocations;
	AllyLocations.Reserve(8);

	for (TActorIterator<AEnemyCharacter> It(Pawn->GetWorld()); It; ++It)
	{
		AEnemyCharacter* Ally = *It;
		if (!IsValid(Ally) || Ally == Pawn) continue;
		if (FVector::Dist(Pawn->GetActorLocation(), Ally->GetActorLocation()) > AllyScanRadius) continue;
		UHealthComponent* AllyHealth = Ally->GetHealthComponent();
		if (IsValid(AllyHealth) && AllyHealth->IsDead()) continue;
		AllyLocations.Add(Ally->GetActorLocation());
	}

	// No living allies in range — hold current position rather than retreating indefinitely
	if (AllyLocations.Num() == 0) return false;

	FVector Centroid = FVector::ZeroVector;
	{
		FVector Sum = FVector::ZeroVector;
		for (const FVector& Loc : AllyLocations)
			Sum += Loc;
		Centroid = Sum / static_cast<float>(AllyLocations.Num());
	}

	const FVector AwayDir = (Centroid - Target->GetActorLocation()).GetSafeNormal2D();
	const FVector Candidate = Centroid + AwayDir * HoldOffset;

	// Project onto navmesh
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Pawn->GetWorld());
	if (!NavSys) return false;

	FNavLocation NavLoc;
	const bool bProjected = NavSys->ProjectPointToNavigation(Candidate, NavLoc, FVector(200.f, 200.f, 200.f));
	if (bProjected)
		OutPos = NavLoc.Location;
	else
		OutPos = Candidate;

	return true;
}

FString UBTTask_OfficerCommand::GetStaticDescription() const
{
	return TEXT("Hold behind allied centroid, fire opportunistically when LOS available");
}
