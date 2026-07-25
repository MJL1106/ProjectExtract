// BTTask_EnemyBoundingAdvance — bounding flanker: short hop toward target, move+fire, notify squad on arrival.

#include "BTTask_EnemyBoundingAdvance.h"
#include "CoverPoseComponent.h"
#include "EnemyAIController.h"
#include "EnemyDebug.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemyMoraleComponent.h"
#include "EnemySquad.h"
#include "EnemySquadSubsystem.h"
#include "SuppressionComponent.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

UBTTask_EnemyBoundingAdvance::UBTTask_EnemyBoundingAdvance()
{
	NodeName = TEXT("Enemy Bounding Advance");
	bNotifyTick = true;
}

uint16 UBTTask_EnemyBoundingAdvance::GetInstanceMemorySize() const
{
	return sizeof(FBoundingAdvanceMemory);
}

EBTNodeResult::Type UBTTask_EnemyBoundingAdvance::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBoundingAdvanceMemory* Mem = reinterpret_cast<FBoundingAdvanceMemory*>(NodeMemory);
	new (Mem) FBoundingAdvanceMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	// enemy.ForceCover debug: keep enemies in the cover loop — non-cover branches fail outright.
	if (GetForceCoverLevel() > 0) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	// Defense in depth: verify ManeuverRole == Flanker
	const uint8 RoleVal = BB->GetValueAsEnum(AEnemyAIController::BB_ManeuverRole);
	if (static_cast<EEnemyManeuverRole>(RoleVal) != EEnemyManeuverRole::Flanker) return EBTNodeResult::Failed;

	// Survival pre-empts
	USuppressionComponent* Suppr = Enemy->GetSuppressionComponent();
	if (IsValid(Suppr) && Suppr->IsSuppressed())
	{
		NotifyBlocked(Enemy, Mem);
		return EBTNodeResult::Failed;
	}

	UHealthComponent* Health = Enemy->GetHealthComponent();
	if (IsValid(Health) && Health->GetHealthPercent() < SurvivalHealthFraction)
	{
		NotifyBlocked(Enemy, Mem);
		return EBTNodeResult::Failed;
	}

	// Solve bound point
	FVector BoundPoint;
	if (!SolveBoundPoint(Pawn, Target, BoundPoint))
	{
		NotifyBlocked(Enemy, Mem);
		return EBTNodeResult::Failed;
	}
	Mem->BoundPoint = BoundPoint;

	// Cover-pose hygiene: leaving cover for the bound — a latched pose would montage-slide the run.
	if (UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent()) PoseComp->ResetCoverPose();
	if (GetCoverAnimLogLevel() > 0)
		UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s TASK(BoundingAdvance) start"), *GetNameSafe(Pawn));

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);

	const EPathFollowingRequestResult::Type MoveResult =
		Controller->MoveToLocation(BoundPoint, ArrivalAcceptance, false, true, false, true);

	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		NotifyBlocked(Enemy, Mem);
		return EBTNodeResult::Failed;
	}
	if (MoveResult == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		NotifyBlocked(Enemy, Mem);
		return EBTNodeResult::Failed;
	}
	Mem->bMoveIssued = true;

	// Fire while moving -- only if we have line of sight (Pattern B LOS gate)
	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	if (bHasLOS)
	{
		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon))
		{
			Weapon->StartFiring();
			Mem->bFiring = true;
		}
		Mem->LosLostTimer = 0.f;
	}

	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyBoundingAdvance::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBoundingAdvanceMemory* Mem = reinterpret_cast<FBoundingAdvanceMemory*>(NodeMemory);

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB || !Controller || !Pawn)
	{
		StopFireAndAim(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy))
	{
		StopFireAndAim(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Target lost
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		Controller->StopMovement();
		StopFireAndAim(OwnerComp, Mem);
		NotifyBlocked(Enemy, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Survival pre-empt: suppressed
	USuppressionComponent* Suppr = Enemy->GetSuppressionComponent();
	if (IsValid(Suppr) && Suppr->IsSuppressed())
	{
		Controller->StopMovement();
		StopFireAndAim(OwnerComp, Mem);
		NotifyBlocked(Enemy, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Survival pre-empt: low HP
	UHealthComponent* Health = Enemy->GetHealthComponent();
	if (IsValid(Health) && Health->GetHealthPercent() < SurvivalHealthFraction)
	{
		Controller->StopMovement();
		StopFireAndAim(OwnerComp, Mem);
		NotifyBlocked(Enemy, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Morale pre-empt: rattled flanker aborts maneuver.
	const UEnemyMoraleComponent* MoraleComp = Enemy->GetMoraleComponent();
	if (IsValid(MoraleComp) && MoraleComp->GetMoraleState() != EMoraleState::Confident)
	{
		Controller->StopMovement();
		StopFireAndAim(OwnerComp, Mem);
		NotifyBlocked(Enemy, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Keep aim on target
	Enemy->SetAimTarget(Target);

	// LOS fire gate: stop firing when LOS lost past grace, re-start when regained
	const bool bHasLOS = BB->GetValueAsBool(AEnemyAIController::BB_HasLineOfSight);
	{
		const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
		const float LosGrace = IsValid(DA) ? DA->FireLosLostGrace : 0.35f;
		if (bHasLOS)
		{
			Mem->LosLostTimer = 0.f;
			if (!Mem->bFiring)
			{
				AWeaponBase* W = Enemy->GetCurrentWeapon();
				if (IsValid(W) && W->CanFire() && !W->IsFiring())
				{
					W->StartFiring();
					Mem->bFiring = true;
				}
			}
		}
		else
		{
			Mem->LosLostTimer += DeltaSeconds;
			if (Mem->LosLostTimer > LosGrace && Mem->bFiring)
			{
				AWeaponBase* W = Enemy->GetCurrentWeapon();
				if (IsValid(W)) W->StopFiring();
				Mem->bFiring = false;
			}
		}
	}

	// Check arrival
	if (!Mem->bMoveIssued) return;

	UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
	if (!PF)
	{
		StopFireAndAim(OwnerComp, Mem);
		NotifyBlocked(Enemy, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	const EPathFollowingStatus::Type Status = PF->GetStatus();
	if (Status == EPathFollowingStatus::Moving) return;

	// Move ended — verify we actually reached the bound point (partial paths shouldn't trigger swap)
	const float DistToBound = FVector::Dist2D(Pawn->GetActorLocation(), Mem->BoundPoint);
	if (DistToBound <= ArrivalAcceptance + ArrivalSlack)
	{
		StopFireAndAim(OwnerComp, Mem);
		NotifyArrived(Enemy, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}

	// Partial path — didn't reach the point
	Controller->StopMovement();
	StopFireAndAim(OwnerComp, Mem);
	NotifyBlocked(Enemy, Mem);
	return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
}

EBTNodeResult::Type UBTTask_EnemyBoundingAdvance::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBoundingAdvanceMemory* Mem = reinterpret_cast<FBoundingAdvanceMemory*>(NodeMemory);

	AAIController* Controller = OwnerComp.GetAIOwner();
	if (Controller) Controller->StopMovement();

	StopFireAndAim(OwnerComp, Mem);

	// External abort while still holding Flanker role = zombie state.
	// Tear down the maneuver so bBoundingActive doesn't stick.
	// Exception: if suppression is holding (reload pause), the decorator will re-activate
	// the branch when the mag refills — don't tear down the maneuver.
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (IsValid(Enemy))
	{
		UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
		if (BB)
		{
			const uint8 RoleVal = BB->GetValueAsEnum(AEnemyAIController::BB_ManeuverRole);
			if (static_cast<EEnemyManeuverRole>(RoleVal) == EEnemyManeuverRole::Flanker)
			{
				UEnemySquad* Squad = ResolveSquad(Enemy, Mem);
				if (!Squad || !Squad->IsSuppressionHolding())
					NotifyBlocked(Enemy, Mem);
			}
		}
	}

	return EBTNodeResult::Aborted;
}

bool UBTTask_EnemyBoundingAdvance::SolveBoundPoint(APawn* Pawn, AActor* Target, FVector& OutPoint) const
{
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Pawn->GetWorld());
	if (!NavSys) return false;

	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector TargetLoc = Target->GetActorLocation();

	FVector ToTarget = (TargetLoc - PawnLoc).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero()) return false;

	const FVector Lateral = FVector::CrossProduct(FVector::UpVector, ToTarget).GetSafeNormal2D();
	const float LateralSign = FMath::RandBool() ? 1.f : -1.f;
	const FVector AdvanceDir = (ToTarget + Lateral * LateralSign * LateralBias).GetSafeNormal2D();

	const float DistToTarget = FVector::Dist2D(PawnLoc, TargetLoc);
	const float ClampedHop = FMath::Min(HopDistance, FMath::Max(0.f, DistToTarget - MinBoundStandoff));
	if (ClampedHop < ArrivalAcceptance) return false;

	const FVector Candidate = PawnLoc + AdvanceDir * ClampedHop;

	FNavLocation NavLoc;
	if (!NavSys->ProjectPointToNavigation(Candidate, NavLoc, FVector(300.f, 300.f, 300.f)))
		return false;

	OutPoint = NavLoc.Location;
	return true;
}

void UBTTask_EnemyBoundingAdvance::StopFireAndAim(UBehaviorTreeComponent& OwnerComp, FBoundingAdvanceMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

	if (Mem && Mem->bFiring)
	{
		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon)) Weapon->StopFiring();
		Mem->bFiring = false;
	}

	Enemy->SetAimTarget(nullptr);

	if (Controller)
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

UEnemySquad* UBTTask_EnemyBoundingAdvance::ResolveSquad(AEnemyCharacter* Enemy, FBoundingAdvanceMemory* Mem) const
{
	UEnemySquad* Squad = Mem->CachedSquad.Get();
	if (IsValid(Squad)) return Squad;

	UWorld* World = Enemy->GetWorld();
	if (!World) return nullptr;

	UEnemySquadSubsystem* SquadSub = World->GetSubsystem<UEnemySquadSubsystem>();
	Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
	if (Squad) Mem->CachedSquad = Squad;

	return Squad;
}

void UBTTask_EnemyBoundingAdvance::NotifyArrived(AEnemyCharacter* Enemy, FBoundingAdvanceMemory* Mem) const
{
	UEnemySquad* Squad = ResolveSquad(Enemy, Mem);
	if (Squad) Squad->NotifyFlankerArrived(Enemy);
}

void UBTTask_EnemyBoundingAdvance::NotifyBlocked(AEnemyCharacter* Enemy, FBoundingAdvanceMemory* Mem) const
{
	UEnemySquad* Squad = ResolveSquad(Enemy, Mem);
	if (Squad) Squad->NotifyManeuverMemberBlocked(Enemy);
}

FString UBTTask_EnemyBoundingAdvance::GetStaticDescription() const
{
	return FString::Printf(TEXT("Bounding advance (hop %.0fcm, arrival %.0fcm)"), HopDistance, ArrivalAcceptance);
}
