// BTTask_RusherAdvance — charge phase: claim rush token, close on a spread bearing, fire on the
// move, melee on contact.

#include "BTTask_RusherAdvance.h"
#include "BarkSubsystem.h"
#include "CoverPoseComponent.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemySquad.h"
#include "EnemySquadSubsystem.h"
#include "WeaponBase.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"

static constexpr float RusherRePathInterval = 0.5f;
// Inside this multiple of MeleeApproachDistance the spread bearing collapses to a direct chase.
static constexpr float RusherDirectChaseFactor = 2.5f;
static constexpr float RusherApproachNavExtent = 300.f;

UBTTask_RusherAdvance::UBTTask_RusherAdvance()
{
	NodeName = TEXT("Rusher Advance");
	bNotifyTick = true;
}

uint16 UBTTask_RusherAdvance::GetInstanceMemorySize() const
{
	return sizeof(FRusherMemory);
}

EBTNodeResult::Type UBTTask_RusherAdvance::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FRusherMemory* Mem = reinterpret_cast<FRusherMemory*>(NodeMemory);
	new (Mem) FRusherMemory();

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

	// Claim a rush token — the squad cap on simultaneous chargers. Idempotent re-claim when this
	// charger re-executes out of its melee loop. No squad = uncapped (solo rusher degrades cleanly).
	// Point-blank targets bypass a failed claim: the commit decorator opens this branch regardless
	// of tokens at that range (self-defence), so failing here would dead-end into orbiting a target
	// standing in this rusher's face.
	const float TargetDistSq = FVector::DistSquared(Pawn->GetActorLocation(), Target->GetActorLocation());
	const bool bPointBlank = DA->RushPointBlankRange > 0.f && TargetDistSq <= FMath::Square(DA->RushPointBlankRange);
	UEnemySquadSubsystem* SquadSub = Pawn->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
	UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
	if (Squad)
	{
		if (!Squad->TryClaimRushToken(Enemy, DA->MaxSimultaneousRushers) && !bPointBlank)
			return EBTNodeResult::Failed;
		Mem->SlotIndex = Squad->GetRushSlotIndex(Enemy);
	}
	if (Mem->SlotIndex == INDEX_NONE)
		Mem->SlotIndex = static_cast<int32>(Enemy->GetUniqueID() % 2);

	// Cover-pose hygiene: the charge can start straight out of the cover-approach branch.
	if (UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent()) PoseComp->ResetCoverPose();

	// War-cry as the charge commits — content-gated (only the Rusher's set carries Rush lines).
	if (IsValid(DA->BarkSet))
		if (UBarkSubsystem* Barks = Pawn->GetWorld()->GetSubsystem<UBarkSubsystem>())
			Barks->RequestBark(Enemy, DA->BarkSet, EBarkType::Rush);

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);
	IssueApproachMove(Controller, Enemy, Target, Mem);
	Mem->RePathTimer = RusherRePathInterval;

	return EBTNodeResult::InProgress;
}

void UBTTask_RusherAdvance::IssueApproachMove(AAIController* Controller, AEnemyCharacter* Enemy, AActor* Target, const FRusherMemory* Mem) const
{
	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	const FVector TargetLoc = Target->GetActorLocation();
	const FVector PawnLoc = Enemy->GetActorLocation();
	const float Dist = FVector::Dist2D(PawnLoc, TargetLoc);
	const float DirectChaseDist = DA->MeleeApproachDistance * RusherDirectChaseFactor;

	// Final stretch (or spread disabled) — straight to the target.
	if (Dist <= DirectChaseDist || DA->RushApproachSpreadDeg <= 0.f)
	{
		Controller->MoveToActor(Target, DA->MeleeApproachDistance, false, true, false, nullptr, true);
		return;
	}

	// Spread bearing: rotate this charger's current bearing off the direct line by its slot side.
	// Repathing every 0.5s walks the bearing around the target, curving simultaneous chargers apart
	// until the direct-chase stretch collapses them onto the melee point.
	const float SpreadSign = (Mem->SlotIndex % 2 == 0) ? -1.f : 1.f;
	const FVector Bearing = (PawnLoc - TargetLoc).GetSafeNormal2D();
	const FVector Dir = Bearing.RotateAngleAxis(SpreadSign * DA->RushApproachSpreadDeg, FVector::UpVector);
	const FVector Candidate = TargetLoc + Dir * FMath::Max(Dist * 0.6f, DirectChaseDist);

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Enemy->GetWorld());
	FNavLocation NavLoc;
	const FVector Extent(RusherApproachNavExtent, RusherApproachNavExtent, RusherApproachNavExtent);
	if (NavSys && NavSys->ProjectPointToNavigation(Candidate, NavLoc, Extent))
	{
		Controller->MoveToLocation(NavLoc.Location, DA->MeleeApproachDistance, false, true, false, true);
		return;
	}

	// No nav on the spread side — fall back to the direct chase.
	Controller->MoveToActor(Target, DA->MeleeApproachDistance, false, true, false, nullptr, true);
}

void UBTTask_RusherAdvance::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FRusherMemory* Mem = reinterpret_cast<FRusherMemory*>(NodeMemory);

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
		CleanUp(OwnerComp, /*bReleaseToken=*/true);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), Target->GetActorLocation());
	const float MeleeRangeSq = DA->MeleeRange * DA->MeleeRange;

	if (DistSq <= MeleeRangeSq && DA->bCanMelee)
	{
		// Stop firing while in melee range; attempt strike each tick (PerformMelee enforces cooldown)
		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon))
			Weapon->StopFiring();

		if (Enemy->PerformMelee(Target))
		{
			// Keep the token: the selector re-enters this task immediately for the melee loop, and a
			// waiting squadmate must not steal the slot between strikes.
			CleanUp(OwnerComp, /*bReleaseToken=*/false);
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		// Cooldown not elapsed — stay InProgress, keep attempting next tick
		return;
	}

	// Outside melee range (or melee disabled) — fire while closing, gated on LOS.
	// Gate on CanFire()+!IsFiring() (mirrors BTTask_EnemyCombatFire) so we never call StartFiring
	// during a reload: doing so orphans bWantsToFire=true, and a no-melee rusher (shotgun) has no
	// StopFiring path to clear it, leaving the weapon permanently silent after the first reload.
	{
		const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
		const float LosGrace = IsValid(DA) ? DA->FireLosLostGrace : 0.35f;
		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (bHasLOS)
		{
			Mem->LosLostTimer = 0.f;
			if (IsValid(Weapon) && Weapon->CanFire() && !Weapon->IsFiring())
				Weapon->StartFiring();
		}
		else
		{
			Mem->LosLostTimer += DeltaSeconds;
			if (Mem->LosLostTimer > LosGrace && IsValid(Weapon) && Weapon->IsFiring())
				Weapon->StopFiring();
		}
	}

	Mem->RePathTimer -= DeltaSeconds;
	if (Mem->RePathTimer <= 0.f)
	{
		IssueApproachMove(Controller, Enemy, Target, Mem);
		Mem->RePathTimer = RusherRePathInterval;
	}
}

EBTNodeResult::Type UBTTask_RusherAdvance::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CleanUp(OwnerComp, /*bReleaseToken=*/true);
	return EBTNodeResult::Aborted;
}

void UBTTask_RusherAdvance::CleanUp(UBehaviorTreeComponent& OwnerComp, bool bReleaseToken) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	if (bReleaseToken)
	{
		UEnemySquadSubsystem* SquadSub = Enemy->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
		UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
		if (Squad) Squad->ReleaseRushToken(Enemy);
	}

	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
		Weapon->StopFiring();

	Enemy->SetAimTarget(nullptr);
	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);

	if (Controller)
	{
		Controller->StopMovement();
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

FString UBTTask_RusherAdvance::GetStaticDescription() const
{
	return TEXT("Claim rush token, close on spread bearing, fire on the move, melee on contact");
}
