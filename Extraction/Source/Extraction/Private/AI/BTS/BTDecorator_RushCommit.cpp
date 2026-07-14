// BTDecorator_RushCommit — tick-observer: charge branch active when a rush token is available/held,
// target is inside charge range, and a vulnerability trigger (or point-blank/base-chance) fires.
// CalculateRawConditionValue is the live source of truth (latch-aware; hysteresis via bCharging).
// TickNode fires ConditionalFlowAbort only when the condition actually changes.

#include "BTDecorator_RushCommit.h"
#include "EnemyCharacter.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyAwarenessComponent.h"
#include "EnemySquad.h"
#include "EnemySquadSubsystem.h"
#include "HealthComponent.h"
#include "WeaponBase.h"
#include "WeaponComponent.h"
#include "CompanionCharacter.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

UBTDecorator_RushCommit::UBTDecorator_RushCommit()
{
	NodeName = TEXT("Rush Commit");
	bNotifyTick = true;
	FlowAbortMode = EBTFlowAbortMode::Both;
}

uint16 UBTDecorator_RushCommit::GetInstanceMemorySize() const
{
	return sizeof(FRushCommitMemory);
}

AEnemyCharacter* UBTDecorator_RushCommit::ResolveEnemy(UBehaviorTreeComponent& OwnerComp, FRushCommitMemory* Mem) const
{
	AEnemyCharacter* Enemy = Mem->CachedEnemy.Get();
	if (IsValid(Enemy)) return Enemy;

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	Enemy = Cast<AEnemyCharacter>(Pawn);
	if (IsValid(Enemy)) Mem->CachedEnemy = Enemy;

	return Enemy;
}

void UBTDecorator_RushCommit::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	FRushCommitMemory* Mem = reinterpret_cast<FRushCommitMemory*>(NodeMemory);
	Mem->bCharging = false;
	Mem->bLastCondition = false;
	Mem->NextTriggerEvalTime = 0.f;
	Mem->LastLosTime = OwnerComp.GetWorld() ? OwnerComp.GetWorld()->GetTimeSeconds() : 0.f;
	Mem->CachedEnemy.Reset();

	if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
		BB->SetValueAsBool(AEnemyAIController::BB_RusherCharging, false);
}

void UBTDecorator_RushCommit::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const
{
	FRushCommitMemory* Mem = reinterpret_cast<FRushCommitMemory*>(NodeMemory);
	Mem->CachedEnemy.Reset();
}

bool UBTDecorator_RushCommit::IsTargetReloading(const AActor* Target)
{
	if (const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Target))
		return Companion->IsReloading();

	if (const UWeaponComponent* WeaponComp = Target->FindComponentByClass<UWeaponComponent>())
	{
		const AWeaponBase* Weapon = WeaponComp->GetCurrentWeapon();
		return IsValid(Weapon) && Weapon->IsReloading();
	}
	return false;
}

bool UBTDecorator_RushCommit::EvaluateTriggers(const AEnemyCharacter* Enemy, const AActor* Target, bool bHasLOS, float LosLostSeconds) const
{
	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();

	// Target seen reloading (LoS-gated — no wallhack reads).
	if (DA->bRushOnTargetReload && bHasLOS && IsTargetReloading(Target))
		return true;

	// Target low health.
	if (DA->RushTargetLowHealthFraction > 0.f)
	{
		const UHealthComponent* TargetHealth = Target->FindComponentByClass<UHealthComponent>();
		if (IsValid(TargetHealth) && TargetHealth->GetHealthPercent() <= DA->RushTargetLowHealthFraction)
			return true;
	}

	// Target hiding — LoS lost long enough.
	if (DA->RushLosLostTriggerTime > 0.f && LosLostSeconds >= DA->RushLosLostTriggerTime)
		return true;

	// Organic pressure — small base chance per evaluation.
	return DA->RushBaseChance > 0.f && FMath::FRand() < DA->RushBaseChance;
}

bool UBTDecorator_RushCommit::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	FRushCommitMemory* Mem = reinterpret_cast<FRushCommitMemory*>(NodeMemory);
	const bool bResult = ComputeCondition(OwnerComp, Mem);

	// Mirror the charge latch to the BB so BTDecorator_RusherStandoff can exclude the circle branch
	// while a charge is committed — makes the three rusher branches order-independent in the tree
	// (NeoStack-authored nodes land at x=0 and BT priority is x-position).
	if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
		BB->SetValueAsBool(AEnemyAIController::BB_RusherCharging, bResult);

	return bResult;
}

bool UBTDecorator_RushCommit::ComputeCondition(UBehaviorTreeComponent& OwnerComp, FRushCommitMemory* Mem) const
{
	AEnemyCharacter* Enemy = ResolveEnemy(OwnerComp, Mem);
	if (!IsValid(Enemy)) { Mem->bCharging = false; return false; }

	const UEnemyArchetypeData* DA = Enemy->GetArchetypeData();
	if (!IsValid(DA)) { Mem->bCharging = false; return false; }

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	AActor* Target = BB ? Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget)) : nullptr;
	if (!IsValid(Target)) { Mem->bCharging = false; return false; }

	// Health abort (0 = fearless, never aborts).
	if (DA->RushAbortHealthFraction > 0.f)
	{
		const UHealthComponent* Health = Enemy->GetHealthComponent();
		if (IsValid(Health) && Health->GetHealthPercent() <= DA->RushAbortHealthFraction)
		{
			Mem->bCharging = false;
			return false;
		}
	}

	const UWorld* World = OwnerComp.GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// LoS bookkeeping runs every call so the lost-duration stays honest across phases.
	const AEnemyAIController* EnemyController = Cast<AEnemyAIController>(OwnerComp.GetAIOwner());
	const UEnemyAwarenessComponent* Awareness = EnemyController ? EnemyController->GetAwarenessComponent() : nullptr;
	const bool bHasLOS = Awareness && Awareness->HasLOSToTarget();
	if (bHasLOS) Mem->LastLosTime = Now;

	const float DistSq = FVector::DistSquared(Enemy->GetActorLocation(), Target->GetActorLocation());

	// Point-blank override — self-defence beats the token cap and triggers.
	if (DA->RushPointBlankRange > 0.f && DistSq <= FMath::Square(DA->RushPointBlankRange))
	{
		Mem->bCharging = true;
		return true;
	}

	if (Mem->bCharging)
	{
		// Committed — hold until the target escapes the hysteresis band.
		if (DistSq > FMath::Square(DA->RushChargeMaxRange + DA->RushChargeHysteresis))
			Mem->bCharging = false;
		return Mem->bCharging;
	}

	// Not committed: launch gate — range, then token availability (claim happens in the task).
	if (DistSq > FMath::Square(DA->RushChargeMaxRange)) return false;

	UEnemySquadSubsystem* SquadSub = Enemy->GetWorld()->GetSubsystem<UEnemySquadSubsystem>();
	UEnemySquad* Squad = SquadSub ? SquadSub->GetSquadFor(Enemy) : nullptr;
	if (Squad && !Squad->IsRushTokenAvailable(Enemy, DA->MaxSimultaneousRushers)) return false;

	// Trigger evaluation on the DA interval (keeps component scans + chance rolls rate-limited).
	if (Now < Mem->NextTriggerEvalTime) return false;
	Mem->NextTriggerEvalTime = Now + DA->RushEvalInterval;

	if (EvaluateTriggers(Enemy, Target, bHasLOS, Now - Mem->LastLosTime))
		Mem->bCharging = true;

	return Mem->bCharging;
}

void UBTDecorator_RushCommit::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FRushCommitMemory* Mem = reinterpret_cast<FRushCommitMemory*>(NodeMemory);
	const bool bCondition = CalculateRawConditionValue(OwnerComp, NodeMemory);

	if (bCondition != Mem->bLastCondition)
	{
		Mem->bLastCondition = bCondition;
		ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
	}
}

FString UBTDecorator_RushCommit::GetStaticDescription() const
{
	return TEXT("Charge when a rush token is available, target within RushChargeMaxRange, and a trigger fires (reload/low HP/LoS-lost/point-blank/base chance); hysteresis on exit");
}
