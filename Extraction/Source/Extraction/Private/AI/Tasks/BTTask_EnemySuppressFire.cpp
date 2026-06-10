// BTTask_EnemySuppressFire — suppressor role: sustained area fire at target or last-known.

#include "BTTask_EnemySuppressFire.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemySquad.h"
#include "EnemySquadSubsystem.h"
#include "WeaponBase.h"
#include "BarkSubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

UBTTask_EnemySuppressFire::UBTTask_EnemySuppressFire()
{
	NodeName = TEXT("Enemy Suppress Fire");
	bNotifyTick = true;
}

uint16 UBTTask_EnemySuppressFire::GetInstanceMemorySize() const
{
	return sizeof(FSuppressFireMemory);
}

EBTNodeResult::Type UBTTask_EnemySuppressFire::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FSuppressFireMemory* Mem = reinterpret_cast<FSuppressFireMemory*>(NodeMemory);
	new (Mem) FSuppressFireMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	// Verify ManeuverRole == Suppressor
	const uint8 RoleVal = BB->GetValueAsEnum(AEnemyAIController::BB_ManeuverRole);
	if (static_cast<EEnemyManeuverRole>(RoleVal) != EEnemyManeuverRole::Suppressor) return EBTNodeResult::Failed;

	// Anchor in place
	Controller->StopMovement();
	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	// Aim setup — LOS-dependent
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

	// Start firing if we have something to shoot at
	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (bHasAimSource && IsValid(Weapon) && !Weapon->IsReloading())
	{
		Weapon->StartFiring();
		Mem->bFiring = true;
	}

	// Notify squad that suppressor is engaged (keeps IsSuppressionLive true through reload-holds)
	UWorld* World = Pawn->GetWorld();
	if (World)
	{
		UEnemySquadSubsystem* SquadSub = World->GetSubsystem<UEnemySquadSubsystem>();
		UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
		if (Squad) Squad->SetSuppressorEngaged(Enemy, true);
	}

	Mem->Phase = ESuppressFirePhase::Fire;
	Mem->PhaseTimer = FMath::RandRange(BurstDurationMin, BurstDurationMax);

	// Bark — Suppressing
	if (World)
	{
		UEnemySquadSubsystem* SquadSub = World->GetSubsystem<UEnemySquadSubsystem>();
		UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
		if (Squad)
		{
			const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
			UBarkSubsystem* Barks = World->GetSubsystem<UBarkSubsystem>();
			if (IsValid(DA) && Barks && Squad->TryClaimSquadBark(EBarkType::Suppressing))
				Barks->RequestBark(Enemy, DA->BarkSet, EBarkType::Suppressing, DA->DisplayName);
		}
	}

	return EBTNodeResult::InProgress;
}

void UBTTask_EnemySuppressFire::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FSuppressFireMemory* Mem = reinterpret_cast<FSuppressFireMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Pawn)
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy))
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Fail if role revoked
	const uint8 RoleVal = BB->GetValueAsEnum(AEnemyAIController::BB_ManeuverRole);
	if (static_cast<EEnemyManeuverRole>(RoleVal) != EEnemyManeuverRole::Suppressor)
	{
		CleanUp(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Fail if target lost — tear down maneuver if still holding role (e.g. target vanished during reload hold)
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		CleanUp(OwnerComp, Mem);
		if (static_cast<EEnemyManeuverRole>(RoleVal) == EEnemyManeuverRole::Suppressor)
		{
			UWorld* World = Enemy->GetWorld();
			if (World)
			{
				UEnemySquadSubsystem* SquadSub = World->GetSubsystem<UEnemySquadSubsystem>();
				UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
				if (Squad) Squad->NotifyManeuverMemberBlocked(Enemy);
			}
		}
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Keep aim current with LOS state
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

	// Ammo-aware: if reloading, hold fire and wait (auto-reload refills)
	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon) && Weapon->IsReloading())
	{
		if (Mem->bFiring)
		{
			Weapon->StopFiring();
			Mem->bFiring = false;
		}
		return;
	}

	Mem->PhaseTimer -= DeltaSeconds;

	switch (Mem->Phase)
	{
	case ESuppressFirePhase::Fire:
		if (!Mem->bFiring && bHasAimSource && IsValid(Weapon))
		{
			Weapon->StartFiring();
			Mem->bFiring = true;
		}
		if (Mem->PhaseTimer <= 0.f)
		{
			if (IsValid(Weapon) && Mem->bFiring)
			{
				Weapon->StopFiring();
				Mem->bFiring = false;
			}
			Mem->Phase = ESuppressFirePhase::Pause;
			Mem->PhaseTimer = FMath::RandRange(BurstPauseMin, BurstPauseMax);
		}
		break;

	case ESuppressFirePhase::Pause:
		if (Mem->PhaseTimer <= 0.f)
		{
			if (bHasAimSource && IsValid(Weapon))
			{
				Weapon->StartFiring();
				Mem->bFiring = true;
			}
			Mem->Phase = ESuppressFirePhase::Fire;
			Mem->PhaseTimer = FMath::RandRange(BurstDurationMin, BurstDurationMax);
		}
		break;
	}
}

EBTNodeResult::Type UBTTask_EnemySuppressFire::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FSuppressFireMemory* Mem = reinterpret_cast<FSuppressFireMemory*>(NodeMemory);
	CleanUp(OwnerComp, Mem);

	// External abort (target lost, awareness flip) while still holding role = zombie state.
	// Tear down the maneuver so bBoundingActive doesn't stick.
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (IsValid(Enemy))
	{
		UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
		if (BB)
		{
			const uint8 RoleVal = BB->GetValueAsEnum(AEnemyAIController::BB_ManeuverRole);
			if (static_cast<EEnemyManeuverRole>(RoleVal) == EEnemyManeuverRole::Suppressor)
			{
				UWorld* World = Enemy->GetWorld();
				if (World)
				{
					UEnemySquadSubsystem* SquadSub = World->GetSubsystem<UEnemySquadSubsystem>();
					UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
					if (Squad) Squad->NotifyManeuverMemberBlocked(Enemy);
				}
			}
		}
	}

	return EBTNodeResult::Aborted;
}

void UBTTask_EnemySuppressFire::CleanUp(UBehaviorTreeComponent& OwnerComp, FSuppressFireMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon) && Mem && Mem->bFiring)
	{
		Weapon->StopFiring();
		Mem->bFiring = false;
	}

	if (Mem && Mem->bAimOverrideActive)
	{
		Enemy->ClearAimLocationOverride();
		Mem->bAimOverrideActive = false;
	}

	Enemy->SetAimTarget(nullptr);

	if (Controller)
		Controller->ClearFocus(EAIFocusPriority::Gameplay);

	// Clear suppressor engaged flag on squad
	UWorld* World = Enemy->GetWorld();
	if (World)
	{
		UEnemySquadSubsystem* SquadSub = World->GetSubsystem<UEnemySquadSubsystem>();
		UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
		if (Squad) Squad->SetSuppressorEngaged(Enemy, false);
	}
}

FString UBTTask_EnemySuppressFire::GetStaticDescription() const
{
	return FString::Printf(TEXT("Sustained suppress fire (burst %.1f-%.1fs, pause %.1f-%.1fs)"),
		BurstDurationMin, BurstDurationMax, BurstPauseMin, BurstPauseMax);
}
