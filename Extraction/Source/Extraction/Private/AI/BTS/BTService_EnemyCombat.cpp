// BTService_EnemyCombat — validates target liveness; writes HasLineOfSight, TargetInRange.

#include "BTService_EnemyCombat.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "AI/AITargetingStatics.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "EnemyAwarenessComponent.h"
#include "EnemyCharacter.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

UBTService_EnemyCombat::UBTService_EnemyCombat()
{
	NodeName = TEXT("Enemy Combat Service");
	Interval = 0.25f;
	RandomDeviation = 0.05f;
}

void UBTService_EnemyCombat::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_AI_EnemyCombatService);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;

	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Controller || !Pawn) return;

	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Pawn);
	AActor* Target = Cast<AActor>(BB->GetValueAsObject(AEnemyAIController::BB_CombatTarget));

	// Liveness, not just validity: a DBNO player stays a live UObject for the whole bleedout window,
	// so IsValid alone left LOS/InRange true and the fire task kept shooting the body until the
	// awareness tick got round to clearing the key. Dead or downed reads the same here — no target.
	if (!IsValid(Target) || !UEnemyAwarenessComponent::IsActorAlive(Target))
	{
		BB->SetValueAsBool(AEnemyAIController::BB_HasLineOfSight, false);
		BB->SetValueAsBool(AEnemyAIController::BB_TargetInRange, false);
		if (Enemy) Enemy->SetHasTargetLOS(false);
		return;
	}

	// Debug auto-engage: force LOS true so the fire task shoots without a real trace.
	const bool bDebugForceEngage = Enemy && Enemy->bDebugAutoEngagePlayer;

	// LOS check — body-point resolver excludes head so head-only peek never opens fire,
	// except snipers vs a standing target (sniper+standing exception).
	bool bHasLOS = false;
	if (bDebugForceEngage)
	{
		bHasLOS = true;
	}
	else
	{
		const FVector EyeLocation = Pawn->GetPawnViewLocation();
		FVector VisiblePoint;
		const bool bAllowHead = AITargeting::ShouldIncludeHeadForObserver(Pawn, Target);
		bHasLOS = AITargeting::GetVisibleBodyPoint(Target, EyeLocation, Pawn, VisiblePoint, bAllowHead);
	}
	BB->SetValueAsBool(AEnemyAIController::BB_HasLineOfSight, bHasLOS);

	// Mirror LOS to the character for the anim instance's in-cover aim gate.
	if (Enemy) Enemy->SetHasTargetLOS(bHasLOS);

	// Range check against archetype EngageRangeMax
	const FVector TargetLoc = Target->GetActorLocation();
	bool bInRange = false;
	if (IsValid(Enemy))
	{
		if (const UEnemyArchetypeData* DA = Enemy->GetArchetypeData())
			bInRange = FVector::Dist(Pawn->GetActorLocation(), TargetLoc) <= DA->EngageRangeMax;
	}
	BB->SetValueAsBool(AEnemyAIController::BB_TargetInRange, bInRange);
}
