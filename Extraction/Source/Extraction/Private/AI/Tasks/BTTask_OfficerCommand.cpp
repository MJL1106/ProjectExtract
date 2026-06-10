// BTTask_OfficerCommand — officer holds behind allied centroid and fires when LOS is available.

#include "BTTask_OfficerCommand.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemyMoraleComponent.h"
#include "EnemySquad.h"
#include "EnemySquadSubsystem.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "BarkSubsystem.h"
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

	// Resolve squad for hold position and command duties
	UEnemySquadSubsystem* SquadSub = Pawn->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
	UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;

	FVector HoldPos;
	if (ComputeHoldPosition(Pawn, Target, Squad, HoldPos))
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

	// Tick cooldown timers
	Mem->FocusCooldownTimer = FMath::Max(0.f, Mem->FocusCooldownTimer - DeltaSeconds);
	Mem->RallyCooldownTimer = FMath::Max(0.f, Mem->RallyCooldownTimer - DeltaSeconds);

	// Re-evaluate hold position on interval
	Mem->RepositionTimer -= DeltaSeconds;
	if (Mem->RepositionTimer <= 0.f)
	{
		UEnemySquadSubsystem* SquadSub = Pawn->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
		UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;

		FVector HoldPos;
		if (ComputeHoldPosition(Pawn, Target, Squad, HoldPos))
			Controller->MoveToLocation(HoldPos, 80.f, false, true, false, true);

		Mem->RepositionTimer = RepositionInterval;

		const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();

		if (IsValid(DA) && Squad)
		{
			// Focus-fire: re-call on cooldown (officer always overrides)
			if (Mem->FocusCooldownTimer <= 0.f && IsValid(Target))
			{
				Squad->SetFocusTarget(Target, Enemy, true);
				Mem->FocusCooldownTimer = DA->FocusCallCooldown;

				UBarkSubsystem* Barks = Pawn->GetWorld()->GetSubsystem<UBarkSubsystem>();
				if (Barks && Squad->TryClaimSquadBark(EBarkType::FocusTarget))
					Barks->RequestBark(Enemy, DA->BarkSet, EBarkType::FocusTarget, DA->DisplayName);
			}

			// Rally: if any living (non-dead) member is Broken and rally cooldown elapsed
			if (Mem->RallyCooldownTimer <= 0.f)
			{
				bool bNeedsRally = false;
				const TArray<TWeakObjectPtr<AEnemyCharacter>>& Members = Squad->GetMembers();
				for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
				{
					AEnemyCharacter* Ally = M.Get();
					if (!IsValid(Ally) || Ally == Enemy) continue;
					UHealthComponent* AllyHP = Ally->GetHealthComponent();
					if (IsValid(AllyHP) && AllyHP->IsDead()) continue;
					UEnemyMoraleComponent* Morale = Ally->GetMoraleComponent();
					if (!IsValid(Morale)) continue;
					if (Morale->GetMoraleState() == EMoraleState::Broken)
					{
						bNeedsRally = true;
						break;
					}
				}

				if (bNeedsRally)
				{
					Squad->Rally(Enemy);
					Mem->RallyCooldownTimer = DA->RallyCooldown;
				}
			}
		}
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

bool UBTTask_OfficerCommand::ComputeHoldPosition(APawn* Pawn, AActor* Target, UEnemySquad* Squad, FVector& OutPos) const
{
	if (!IsValid(Pawn) || !IsValid(Target)) return false;

	TArray<FVector> AllyLocations;
	AllyLocations.Reserve(8);
	const FVector PawnLoc = Pawn->GetActorLocation();

	// Primary path: use squad members (cheap — no world scan)
	if (Squad)
	{
		const TArray<TWeakObjectPtr<AEnemyCharacter>>& Members = Squad->GetMembers();
		for (const TWeakObjectPtr<AEnemyCharacter>& M : Members)
		{
			AEnemyCharacter* Ally = M.Get();
			if (!IsValid(Ally) || Ally == Pawn) continue;
			UHealthComponent* AllyHP = Ally->GetHealthComponent();
			if (IsValid(AllyHP) && AllyHP->IsDead()) continue;
			if (FVector::Dist(PawnLoc, Ally->GetActorLocation()) > AllyScanRadius) continue;
			AllyLocations.Add(Ally->GetActorLocation());
		}
	}
	else
	{
		// Squadless officer fallback: world scan
		for (TActorIterator<AEnemyCharacter> It(Pawn->GetWorld()); It; ++It)
		{
			AEnemyCharacter* Ally = *It;
			if (!IsValid(Ally) || Ally == Pawn) continue;
			if (FVector::Dist(PawnLoc, Ally->GetActorLocation()) > AllyScanRadius) continue;
			UHealthComponent* AllyHP = Ally->GetHealthComponent();
			if (IsValid(AllyHP) && AllyHP->IsDead()) continue;
			AllyLocations.Add(Ally->GetActorLocation());
		}
	}

	if (AllyLocations.Num() == 0) return false;

	FVector Sum = FVector::ZeroVector;
	for (const FVector& Loc : AllyLocations)
		Sum += Loc;
	const FVector Centroid = Sum / static_cast<float>(AllyLocations.Num());

	const FVector AwayDir = (Centroid - Target->GetActorLocation()).GetSafeNormal2D();
	const FVector Candidate = Centroid + AwayDir * HoldOffset;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Pawn->GetWorld());
	if (!NavSys) return false;

	FNavLocation NavLoc;
	if (NavSys->ProjectPointToNavigation(Candidate, NavLoc, FVector(200.f, 200.f, 200.f)))
		OutPos = NavLoc.Location;
	else
		OutPos = Candidate;

	return true;
}

FString UBTTask_OfficerCommand::GetStaticDescription() const
{
	return TEXT("Hold behind allied centroid, fire opportunistically when LOS available");
}
