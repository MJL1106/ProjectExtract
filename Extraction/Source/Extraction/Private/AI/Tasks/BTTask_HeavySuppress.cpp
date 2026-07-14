// BTTask_HeavySuppress — heavy pushes toward the threat firing on the move, anchors inside
// EngageRangeMin, and drives sustained burst fire at target or last-known.

#include "BTTask_HeavySuppress.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "WeaponBase.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

/** Repath cadence while advancing (mirrors the rusher's 0.5s rhythm). */
static constexpr float AdvanceRepathInterval = 0.5f;
/** Extra distance past EngageRangeMin before an anchored heavy resumes the push (no band-edge stutter). */
static constexpr float AdvanceResumeHysteresis = 200.f;
/** Threat-point drift that forces an immediate repath regardless of cadence. */
static constexpr float AdvanceGoalMovedRepathDist = 150.f;

void UBTTask_HeavySuppress::UpdateAdvance(AAIController* Controller, const APawn* Pawn,
	const UEnemyArchetypeData* DA, FHeavySuppressMemory* Mem,
	bool bHasAimSource, const FVector& ThreatLoc, float DeltaSeconds)
{
	if (!Controller || !IsValid(Pawn) || !IsValid(DA) || !Mem) return;

	// Nothing to act on — hold position rather than wander.
	if (!bHasAimSource)
	{
		if (Mem->bAdvancing)
		{
			Controller->StopMovement();
			Mem->bAdvancing = false;
		}
		return;
	}

	const float Dist = FVector::Dist2D(Pawn->GetActorLocation(), ThreatLoc);

	if (Mem->bAdvancing)
	{
		// Close enough — anchor and hose from here.
		if (Dist <= DA->EngageRangeMin)
		{
			Controller->StopMovement();
			Mem->bAdvancing = false;
			return;
		}

		Mem->RepathTimer -= DeltaSeconds;
		const bool bGoalMoved = FVector::Dist2D(ThreatLoc, Mem->LastMoveGoal) > AdvanceGoalMovedRepathDist;
		if (Mem->RepathTimer > 0.f && !bGoalMoved) return;

		Controller->MoveToLocation(ThreatLoc, DA->EngageRangeMin, false, true, true, true);
		Mem->LastMoveGoal = ThreatLoc;
		Mem->RepathTimer = AdvanceRepathInterval;
		return;
	}

	// Anchored — resume the push only once the gap clearly exceeds the band edge.
	if (Dist > DA->EngageRangeMin + AdvanceResumeHysteresis)
	{
		Controller->MoveToLocation(ThreatLoc, DA->EngageRangeMin, false, true, true, true);
		Mem->LastMoveGoal = ThreatLoc;
		Mem->RepathTimer = AdvanceRepathInterval;
		Mem->bAdvancing = true;
	}
}

UBTTask_HeavySuppress::UBTTask_HeavySuppress()
{
	NodeName = TEXT("Heavy Suppress");
	bNotifyTick = true;
}

uint16 UBTTask_HeavySuppress::GetInstanceMemorySize() const
{
	return sizeof(FHeavySuppressMemory);
}

EBTNodeResult::Type UBTTask_HeavySuppress::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FHeavySuppressMemory* Mem = reinterpret_cast<FHeavySuppressMemory*>(NodeMemory);
	new (Mem) FHeavySuppressMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	// Clear any stale move from a prior branch; UpdateAdvance below owns movement from here.
	Controller->StopMovement();
	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	bool bHasAimSource = false;

	if (bHasLOS)
	{
		Enemy->SetAimTarget(Target);
		Controller->SetFocus(Target);
		bHasAimSource = true;
	}
	else
	{
		// Clear aim target so WeaponBase uses the location override, not the actor position
		Enemy->SetAimTarget(nullptr);
		const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		if (!LastKnown.IsNearlyZero())
		{
			Enemy->SetAimLocationOverride(LastKnown);
			Controller->SetFocalPoint(LastKnown);
			Mem->bAimOverrideActive = true;
			bHasAimSource = true;
		}
	}

	// Push toward the threat when outside EngageRangeMin; anchor once inside.
	const FVector EntryThreatLoc = bHasLOS
		? Target->GetActorLocation()
		: BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
	UpdateAdvance(Controller, Pawn, DA, Mem, bHasAimSource, EntryThreatLoc, 0.f);

	// Only start firing if there is a valid aim source — don't forward-hose into the void
	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (bHasAimSource && IsValid(Weapon))
		Weapon->StartFiring();

	Mem->Phase = EHeavySuppressPhase::Fire;
	Mem->PhaseTimer = FMath::RandRange(DA->BurstDurationMin, DA->BurstDurationMax);

	return EBTNodeResult::InProgress;
}

void UBTTask_HeavySuppress::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FHeavySuppressMemory* Mem = reinterpret_cast<FHeavySuppressMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Keep aim target / override current with LOS state each tick
	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	bool bHasAimSource = false;

	if (bHasLOS)
	{
		if (Mem->bAimOverrideActive)
		{
			Enemy->ClearAimLocationOverride();
			Mem->bAimOverrideActive = false;
		}
		Enemy->SetAimTarget(Target);
		Controller->SetFocus(Target);
		bHasAimSource = true;
	}
	else
	{
		// Clear aim target so WeaponBase uses the location override, not the actor position
		Enemy->SetAimTarget(nullptr);
		const FVector LastKnown = BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
		if (!LastKnown.IsNearlyZero())
		{
			Enemy->SetAimLocationOverride(LastKnown);
			Controller->SetFocalPoint(LastKnown);
			Mem->bAimOverrideActive = true;
			bHasAimSource = true;
		}
	}

	// Advance/anchor decision each tick — the burst state machine below runs either way,
	// so the push is a walking hose, not a silent reposition.
	const FVector TickThreatLoc = bHasLOS
		? Target->GetActorLocation()
		: BB->GetValueAsVector(AEnemyAIController::BB_LastKnownLocation);
	UpdateAdvance(Controller, Pawn, DA, Mem, bHasAimSource, TickThreatLoc, DeltaSeconds);

	Mem->PhaseTimer -= DeltaSeconds;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();

	switch (Mem->Phase)
	{
	case EHeavySuppressPhase::Fire:
		if (Mem->PhaseTimer <= 0.f)
		{
			if (IsValid(Weapon))
				Weapon->StopFiring();
			Mem->Phase = EHeavySuppressPhase::Pause;
			Mem->PhaseTimer = FMath::RandRange(DA->BurstPauseMin, DA->BurstPauseMax);
		}
		break;

	case EHeavySuppressPhase::Pause:
		if (Mem->PhaseTimer <= 0.f)
		{
			// Only resume firing if there's still a valid aim source
			if (bHasAimSource && IsValid(Weapon))
				Weapon->StartFiring();
			Mem->Phase = EHeavySuppressPhase::Fire;
			Mem->PhaseTimer = FMath::RandRange(DA->BurstDurationMin, DA->BurstDurationMax);
		}
		break;
	}
}

EBTNodeResult::Type UBTTask_HeavySuppress::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FHeavySuppressMemory* Mem = reinterpret_cast<FHeavySuppressMemory*>(NodeMemory);
	CleanUp(OwnerComp, Mem);
	return EBTNodeResult::Aborted;
}

void UBTTask_HeavySuppress::CleanUp(UBehaviorTreeComponent& OwnerComp, FHeavySuppressMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;

	// An in-flight advance would otherwise keep walking into the next branch.
	if (Controller && Mem && Mem->bAdvancing)
	{
		Controller->StopMovement();
		Mem->bAdvancing = false;
	}

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
		Weapon->StopFiring();

	if (Mem && Mem->bAimOverrideActive)
	{
		Enemy->ClearAimLocationOverride();
		Mem->bAimOverrideActive = false;
	}

	Enemy->SetAimTarget(nullptr);

	if (Controller)
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

FString UBTTask_HeavySuppress::GetStaticDescription() const
{
	return TEXT("Push to EngageRangeMin firing on the move, then anchor and drive sustained burst fire at target or last-known location");
}
