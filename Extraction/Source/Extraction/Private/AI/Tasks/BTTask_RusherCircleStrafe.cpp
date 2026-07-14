// BTTask_RusherCircleStrafe — orbit the target on the standoff ring, firing on the move.

#include "BTTask_RusherCircleStrafe.h"
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

static constexpr float CircleArrivalAcceptance = 50.f;

UBTTask_RusherCircleStrafe::UBTTask_RusherCircleStrafe()
{
	NodeName = TEXT("Rusher Circle Strafe");
	bNotifyTick = true;
}

uint16 UBTTask_RusherCircleStrafe::GetInstanceMemorySize() const
{
	return sizeof(FCircleMemory);
}

EBTNodeResult::Type UBTTask_RusherCircleStrafe::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FCircleMemory* Mem = reinterpret_cast<FCircleMemory*>(NodeMemory);
	new (Mem) FCircleMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn) return EBTNodeResult::Failed;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return EBTNodeResult::Failed;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target)) return EBTNodeResult::Failed;

	// Cover-pose hygiene: this branch is often entered straight out of the cover approach.
	if (UCoverPoseComponent* PoseComp = Enemy->GetCoverPoseComponent()) PoseComp->ResetCoverPose();

	// Invariant: a circling rusher is not charging — free any rush token still held from a charge
	// that ended without an explicit release (e.g. melee-loop success followed by a phase change).
	UEnemySquadSubsystem* SquadSub = Pawn->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
	if (UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr)
		Squad->ReleaseRushToken(Enemy);

	Enemy->SetMoveSpeedMode(EEnemyMoveSpeedMode::Strafe);
	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);

	Mem->OrbitSign = FMath::RandBool() ? 1.f : -1.f;
	Mem->NextRepickTime = 0.f;

	return EBTNodeResult::InProgress;
}

void UBTTask_RusherCircleStrafe::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FCircleMemory* Mem = reinterpret_cast<FCircleMemory*>(NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!BB || !Controller || !Pawn)
	{
		CleanUp(OwnerComp);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	const UEnemyArchetypeData* DA = IsValid(Enemy) ? Enemy->GetArchetypeData() : nullptr;
	if (!IsValid(DA))
	{
		CleanUp(OwnerComp);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));
	if (!IsValid(Target))
	{
		CleanUp(OwnerComp);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	// Backstop self-exit: standoff decorator should abort first, but never orbit a target that has
	// clearly left the band (falls back to the cover-advance branch).
	const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), Target->GetActorLocation());
	const float ExitDist = (DA->RushStandoffEnterRange + DA->RushStandoffHysteresis) * 1.25f;
	if (DistSq > FMath::Square(ExitDist))
	{
		CleanUp(OwnerComp);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	Enemy->SetAimTarget(Target);
	Controller->SetFocus(Target);

	// Fire while orbiting — same reload-safe gate as RusherAdvance.
	AWeaponBase* Weapon = Enemy->GetCurrentWeapon();
	if (IsValid(Weapon) && Weapon->CanFire() && !Weapon->IsFiring())
		Weapon->StartFiring();

	// Pure time-gated repick: also rate-limits the nav-project + trace cost when picks keep failing
	// (a leg-completion fast-path would re-run PickOrbitPoint every tick while boxed in).
	const float Now = Pawn->GetWorld()->GetTimeSeconds();
	if (Now < Mem->NextRepickTime) return;

	FVector OrbitPoint;
	if (PickOrbitPoint(Enemy, Target, Mem, OrbitPoint))
		Controller->MoveToLocation(OrbitPoint, CircleArrivalAcceptance, false, true, false, true);

	// Failed pick = hold and fire this cadence window, then try again.
	Mem->NextRepickTime = Now + FMath::FRandRange(DA->StrafeIntervalMin, DA->StrafeIntervalMax);
}

bool UBTTask_RusherCircleStrafe::PickOrbitPoint(AEnemyCharacter* Enemy, const AActor* Target, FCircleMemory* Mem, FVector& OutPoint) const
{
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Enemy->GetWorld());
	if (!NavSys) return false;

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	const FVector TargetLoc = Target->GetActorLocation();
	const FVector Bearing = (Enemy->GetActorLocation() - TargetLoc).GetSafeNormal2D();
	const float EyeOffsetZ = Enemy->GetPawnViewLocation().Z - Enemy->GetActorLocation().Z;

	// Try the preferred side first at full then half step, then the reverse side (and keep it —
	// orbits naturally bounce off walls). Attempt count is bounded by StrafeLosRetryCount.
	const float BaseStep = DA->RushCircleStepDeg;
	const float TrySigns[4] = { Mem->OrbitSign, Mem->OrbitSign, -Mem->OrbitSign, -Mem->OrbitSign };
	const float TrySteps[4] = { BaseStep, BaseStep * 0.5f, BaseStep, BaseStep * 0.5f };
	const int32 Attempts = FMath::Min(4, FMath::Max(1, DA->StrafeLosRetryCount));

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(RusherCircleLoS), false, Enemy);
	TraceParams.AddIgnoredActor(Target);

	for (int32 i = 0; i < Attempts; ++i)
	{
		const FVector Dir = Bearing.RotateAngleAxis(TrySigns[i] * TrySteps[i], FVector::UpVector);
		const FVector Candidate = TargetLoc + Dir * DA->RushStandoffRadius;

		FNavLocation NavLoc;
		const FVector Extent(DA->StrafeNavProjectExtent, DA->StrafeNavProjectExtent, DA->StrafeNavProjectExtent);
		if (!NavSys->ProjectPointToNavigation(Candidate, NavLoc, Extent)) continue;

		// Keep only picks that can still see the target (no orbiting into a blind wall).
		const FVector EyeAtCandidate = NavLoc.Location + FVector(0.f, 0.f, EyeOffsetZ);
		if (Enemy->GetWorld()->LineTraceTestByChannel(EyeAtCandidate, TargetLoc, ECC_Visibility, TraceParams))
			continue;

		Mem->OrbitSign = TrySigns[i];
		OutPoint = NavLoc.Location;
		return true;
	}
	return false;
}

EBTNodeResult::Type UBTTask_RusherCircleStrafe::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CleanUp(OwnerComp);
	return EBTNodeResult::Aborted;
}

void UBTTask_RusherCircleStrafe::CleanUp(UBehaviorTreeComponent& OwnerComp) const
{
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	if (!IsValid(Enemy)) return;

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

FString UBTTask_RusherCircleStrafe::GetStaticDescription() const
{
	return TEXT("Orbit the target on the standoff ring, firing; direction flips on nav/LoS failure");
}
