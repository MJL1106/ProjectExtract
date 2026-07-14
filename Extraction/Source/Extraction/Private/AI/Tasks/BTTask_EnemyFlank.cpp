// BTTask_EnemyFlank — flanker role: ring-sample a flank point, move+fire, release on arrival/abort.

#include "BTTask_EnemyFlank.h"
#include "CoverPoseComponent.h"
#include "EnemyAIController.h"
#include "EnemyDebug.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "EnemySquad.h"
#include "EnemySquadSubsystem.h"
#include "SuppressionComponent.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "BarkSubsystem.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

UBTTask_EnemyFlank::UBTTask_EnemyFlank()
{
	NodeName = TEXT("Enemy Flank");
	bNotifyTick = true;
}

uint16 UBTTask_EnemyFlank::GetInstanceMemorySize() const
{
	return sizeof(FFlankMemory);
}

EBTNodeResult::Type UBTTask_EnemyFlank::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FFlankMemory* Mem = reinterpret_cast<FFlankMemory*>(NodeMemory);
	new (Mem) FFlankMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	// enemy.ForceCover debug: keep enemies in the cover loop — non-cover branches fail outright.
	if (GetForceCoverLevel() > 0) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	UEnemySquadSubsystem* SquadSub = Pawn->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
	UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
	if (!Squad) return EBTNodeResult::Failed;

	// Deliberate flanking is an officer-led maneuver — a leaderless squad pressures independently
	// through the posture Press cadence instead of claiming the Flanker role.
	if (!Squad->HasLivingOfficer()) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	// Squad-persisted cooldown (survives tree restarts)
	const float WorldTime = Pawn->GetWorld()->GetTimeSeconds();
	if ((WorldTime - Squad->GetLastFlankAttemptTime(Enemy)) < DA->FlankAttemptCooldown) return EBTNodeResult::Failed;

	// Health gate
	UHealthComponent* Health = Enemy->GetHealthComponent();
	if (IsValid(Health) && Health->GetHealthPercent() <= DA->FlankAbortHealthFraction) return EBTNodeResult::Failed;

	// Suppression gate
	USuppressionComponent* Suppr = Enemy->GetSuppressionComponent();
	if (IsValid(Suppr) && Suppr->IsSuppressed()) return EBTNodeResult::Failed;

	// Claim Flanker token
	if (!Squad->TryClaimRole(EEnemySquadRole::Flanker, Enemy)) return EBTNodeResult::Failed;
	Mem->bRoleClaimed = true;
	Squad->RecordFlankAttempt(Enemy);

	// Solve a flank point
	FVector FlankPoint;
	if (!SolveFlankPoint(Pawn, Target, DA->EngageRangeMin, FlankPoint))
	{
		ReleaseRoleAndStopFire(OwnerComp, Mem);
		return EBTNodeResult::Failed;
	}

	// Cover-pose hygiene: leaving cover for the flank — a latched pose would montage-slide the run.
	if (UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent()) PoseComp->ResetCoverPose();
	if (GetCoverAnimLogLevel() > 0)
		UE_LOG(LogTemp, Log, TEXT("[COVERSTATE] %s TASK(Flank) start"), *GetNameSafe(Pawn));

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Combat);
	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);

	const EPathFollowingRequestResult::Type MoveResult =
		Controller->MoveToLocation(FlankPoint, ArrivalAcceptance, false, true, false, true);
	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		ReleaseRoleAndStopFire(OwnerComp, Mem);
		return EBTNodeResult::Failed;
	}
	if (MoveResult == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		ReleaseRoleAndStopFire(OwnerComp, Mem);
		return EBTNodeResult::Succeeded;
	}
	Mem->bMoveIssued = true;

	// Start firing while moving
	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon))
	{
		Weapon->StartFiring();
		Mem->bFiring = true;
	}

	// Bark
	UBarkSubsystem* Barks = Pawn->GetWorld()->GetSubsystem<UBarkSubsystem>();
	if (Barks && Squad->TryClaimSquadBark(EBarkType::Flanking))
		Barks->RequestBark(Enemy, DA->BarkSet, EBarkType::Flanking, DA->DisplayName);

	return EBTNodeResult::InProgress;
}

void UBTTask_EnemyFlank::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FFlankMemory* Mem = reinterpret_cast<FFlankMemory*>(NodeMemory);

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB || !Controller || !Pawn)
	{
		ReleaseRoleAndStopFire(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy))
	{
		ReleaseRoleAndStopFire(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Abort: target lost
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		ReleaseRoleAndStopFire(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Abort: suppressed
	USuppressionComponent* Suppr = Enemy->GetSuppressionComponent();
	if (IsValid(Suppr) && Suppr->IsSuppressed())
	{
		ReleaseRoleAndStopFire(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Abort: health low
	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	UHealthComponent* Health = Enemy->GetHealthComponent();
	if (IsValid(DA) && IsValid(Health) && Health->GetHealthPercent() <= DA->FlankAbortHealthFraction)
	{
		ReleaseRoleAndStopFire(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Keep aim on target while moving
	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);

	// Check arrival
	if (!Mem->bMoveIssued) return;

	UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
	if (!PF)
	{
		ReleaseRoleAndStopFire(OwnerComp, Mem);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	const EPathFollowingStatus::Type Status = PF->GetStatus();
	if (Status == EPathFollowingStatus::Moving) return;

	// Arrived or path failed — release role, stop fire, finish
	ReleaseRoleAndStopFire(OwnerComp, Mem);
	return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
}

EBTNodeResult::Type UBTTask_EnemyFlank::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FFlankMemory* Mem = reinterpret_cast<FFlankMemory*>(NodeMemory);

	AAIController* Controller = OwnerComp.GetAIOwner();
	if (Controller) Controller->StopMovement();

	ReleaseRoleAndStopFire(OwnerComp, Mem);
	return EBTNodeResult::Aborted;
}

bool UBTTask_EnemyFlank::SolveFlankPoint(APawn* Pawn, AActor* Target, float RangeMin, FVector& OutPoint) const
{
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Pawn->GetWorld());
	if (!NavSys) return false;

	const FVector TargetLoc = Target->GetActorLocation();
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector TargetFwd = Target->GetActorForwardVector().GetSafeNormal2D();
	const FVector ToPawn = (PawnLoc - TargetLoc).GetSafeNormal2D();
	// Enemy-to-player direction: dot(ToPoint, EnemyToPlayer) > 0 means point is on the far side
	const FVector EnemyToPlayer = (TargetLoc - PawnLoc).GetSafeNormal2D();

	// Two-pass: prefer near-side/perpendicular points; fall back to far-side only if geometry forces it
	static constexpr float FarSideEpsilon = 0.1f;

	const float InnerR = RangeMin;
	const float OuterR = RangeMin + RingOuterPadding;

	float BestNearScore = -FLT_MAX;
	float BestFarScore  = -FLT_MAX;
	FVector BestNearPoint = FVector::ZeroVector;
	FVector BestFarPoint  = FVector::ZeroVector;
	bool bFoundNear = false;
	bool bFoundFar  = false;

	for (int32 i = 0; i < RingSampleCount; ++i)
	{
		const float Angle = (2.f * UE_PI * i) / static_cast<float>(RingSampleCount);
		const FVector Dir2D = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);

		const float Radius = FMath::Lerp(InnerR, OuterR, 0.5f);
		const FVector Candidate = TargetLoc + Dir2D * Radius;

		FNavLocation NavLoc;
		if (!NavSys->ProjectPointToNavigation(Candidate, NavLoc, FVector(300.f, 300.f, 300.f)))
			continue;

		const FVector Point = NavLoc.Location;
		const FVector ToPoint = (Point - TargetLoc).GetSafeNormal2D();

		const float BehindScore      = -FVector::DotProduct(TargetFwd, ToPoint);
		const float DisplacementScore = 1.f - FMath::Abs(FVector::DotProduct(ToPawn, ToPoint));
		const float Score             = DisplacementScore * SideWeight + BehindScore * BehindWeight;

		// Classify: far-side means the flanker would cross through/past the player to reach it
		const float FarSideDot = FVector::DotProduct(ToPoint, EnemyToPlayer);
		if (FarSideDot > FarSideEpsilon)
		{
			if (Score > BestFarScore) { BestFarScore = Score; BestFarPoint = Point; bFoundFar = true; }
		}
		else
		{
			if (Score > BestNearScore) { BestNearScore = Score; BestNearPoint = Point; bFoundNear = true; }
		}
	}

	if (bFoundNear) { OutPoint = BestNearPoint; return true; }
	if (bFoundFar)  { OutPoint = BestFarPoint;  return true; }
	return false;
}

void UBTTask_EnemyFlank::ReleaseRoleAndStopFire(UBehaviorTreeComponent& OwnerComp, FFlankMemory* Mem) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);

	if (Mem->bRoleClaimed && IsValid(Enemy))
	{
		UEnemySquadSubsystem* SquadSub = Enemy->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
		UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
		if (Squad) Squad->ReleaseRole(EEnemySquadRole::Flanker, Enemy);
		Mem->bRoleClaimed = false;
	}

	if (Mem->bFiring && IsValid(Enemy))
	{
		AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
		if (IsValid(Weapon)) Weapon->StopFiring();
		Mem->bFiring = false;
	}

	if (IsValid(Enemy)) Enemy->SetAimTarget(nullptr);
	if (Controller) Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

FString UBTTask_EnemyFlank::GetStaticDescription() const
{
	return TEXT("Claim Flanker role, ring-sample flank point, move+fire, release on arrival/abort");
}
