// BT task — companion commanded loot sweep.

#include "AI/Tasks/BTTask_CompanionLoot.h"
#include "AI/CompanionAIController.h"
#include "Companion/CompanionCharacter.h"
#include "World/Lootable.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompanionLoot, Log, All);

// Same-room gate (mirrors ExploreLootableHasLoS in BTTask_CompanionExplore): the sweep radius alone
// reaches through walls into neighbouring rooms — the companion chased containers behind closed
// doors, auto-opening every door on the way. A candidate only counts when the companion can actually
// see it from where it stands. Two probe points (bounds centre + top centre) and every other lootable
// ignored as a blocker: a shelf of containers must not occlude its own neighbours, or the sweep dies
// after the first container while the wall/closed-door block stays intact.
//
// A blocked probe is not automatically a rejection. A loot volume carries no mesh — it is draped
// over an existing drawer/table/shelf and that world geometry IS the visual, so the probe point sits
// INSIDE the furniture, which cannot be ignored (it is not the candidate). A hit within
// OcclusionTolerance of the volume's SURFACE (zero for anything inside it) is therefore the prop the
// volume wraps and counts as seen; a hit further out is a wall or a doorframe — a different room —
// and still rejects. Surface distance, NOT distance from the probe point: a centre metric inherits
// the box diagonal as an omnidirectional floor, so a volume flattened onto a table top would accept
// the floor and the ceiling as readily as the table.
static bool LootViewerHasLoS(UWorld* World, const APawn* Viewer, const AActor* Candidate,
	const TArray<AActor*>& AllLootables, float OcclusionTolerance)
{
	// Collision-only bounds — already the default, stated explicitly so the choice is visible.
	const FBox Bounds = Candidate->GetComponentsBoundingBox(false);
	if (!Bounds.IsValid)
	{
		// No colliding components — the box would ForceInit and the probe would trace to the world
		// origin. Reject loudly; a Blueprint subclass with a NoCollision volume must not fail silently.
		UE_LOG(LogCompanionLoot, Warning, TEXT("%s has no colliding bounds — cannot LoS-probe it"),
			*GetNameSafe(Candidate));
		return false;
	}

	FVector EyeLoc; FRotator EyeRot;
	Viewer->GetActorEyesViewPoint(EyeLoc, EyeRot);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionLootSweepLoS), false);
	Params.AddIgnoredActor(Viewer);
	Params.AddIgnoredActors(AllLootables);

	const FVector Center = Bounds.GetCenter();
	const FVector Probes[] = { Center, FVector(Center.X, Center.Y, Bounds.Max.Z) };
	const float ToleranceSq = FMath::Square(OcclusionTolerance);

	for (const FVector& Target : Probes)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, EyeLoc, Target, ECC_Visibility, Params))
			return true;
		if (Bounds.ComputeSquaredDistanceToPoint(Hit.ImpactPoint) <= ToleranceSq)
			return true;
	}
	return false;
}

// Distance from pawn to the nearest point on the container's collision bounds (not its origin) —
// robust to wide cabinets whose pivot sits at one end. Mirrors BTTask_CompanionBreach's helper.
static float DistanceFromPawnToActor(const APawn* Pawn, const AActor* Target)
{
	if (!IsValid(Pawn) || !IsValid(Target)) return TNumericLimits<float>::Max();
	const FBox Bounds = Target->GetComponentsBoundingBox(false);
	if (!Bounds.IsValid) return FVector::Dist(Pawn->GetActorLocation(), Target->GetActorLocation());
	return FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(Pawn->GetActorLocation()));
}

UBTTask_CompanionLoot::UBTTask_CompanionLoot()
{
	NodeName = TEXT("Companion Loot Sweep");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_CompanionLoot::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	bMoveRequested = false;
	bLootTriggered = false;
	LootWaitElapsed = 0.f;
	LootedCount = 0;
	CurrentTarget.Reset();
	CommandedTarget.Reset();
	SkippedThisSweep.Reset();

	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!IsValid(AIC) || !IsValid(BB))
	{
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	AActor* Container = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CommandTargetActor));
	if (!IsValid(Container) || !Container->GetClass()->ImplementsInterface(ULootable::StaticClass())
		|| !ILootable::Execute_CanLoot(Container))
	{
		UE_LOG(LogCompanionLoot, Warning, TEXT("ExecuteTask: command target %s is not lootable"), *GetNameSafe(Container));
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	CurrentTarget = Container;
	CommandedTarget = Container;
	SweepAnchor = Container->GetActorLocation();

	if (!StartMoveToCurrentTarget(OwnerComp))
	{
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}
	return EBTNodeResult::InProgress;
}

void UBTTask_CompanionLoot::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	APawn* Pawn = IsValid(AIC) ? AIC->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Combat breaks off the sweep: while a command is active the combat branch cannot run (the
	// command selector outranks it), so a live target must fail the loot command out — same
	// contract as the breach task's mid-approach break-off. The player re-pings afterwards.
	if (const UBlackboardComponent* CombatBB = OwnerComp.GetBlackboardComponent())
	{
		if (IsValid(Cast<AActor>(CombatBB->GetValueAsObject(ACompanionAIController::BB_CombatTarget))))
		{
			UE_LOG(LogCompanionLoot, Log, TEXT("TickTask: combat target acquired — breaking off loot sweep"));
			FailAndClear(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}
	}

	// Latest loot ping wins mid-sweep: a fresh confirmed target re-anchors the sweep (the command
	// enum stays Loot, so no observer-abort fires — we watch the target key ourselves).
	if (const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
	{
		AActor* BBTarget = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CommandTargetActor));
		if (IsValid(BBTarget) && BBTarget != CommandedTarget.Get()
			&& BBTarget->GetClass()->ImplementsInterface(ULootable::StaticClass()))
		{
			UE_LOG(LogCompanionLoot, Log, TEXT("TickTask: re-anchoring sweep on new ping %s"), *GetNameSafe(BBTarget));
			CommandedTarget = BBTarget;
			CurrentTarget = BBTarget;
			SweepAnchor = BBTarget->GetActorLocation();
			SkippedThisSweep.Reset();
			bLootTriggered = false;
			LootWaitElapsed = 0.f;
			AIC->StopMovement();
			if (!StartMoveToCurrentTarget(OwnerComp))
				AdvanceSweep(OwnerComp);
			return;
		}
	}

	// Post-loot pause at the container, then chain to the next one.
	if (bLootTriggered)
	{
		LootWaitElapsed += DeltaSeconds;
		if (LootWaitElapsed >= InterLootPause)
			AdvanceSweep(OwnerComp);
		return;
	}

	// Target vanished or got looted by someone else mid-approach — skip ahead.
	AActor* Container = CurrentTarget.Get();
	if (!IsValid(Container) || !ILootable::Execute_CanLoot(Container))
	{
		AdvanceSweep(OwnerComp);
		return;
	}

	const float Dist = DistanceFromPawnToActor(Pawn, Container);
	const bool bPathDone = (AIC->GetMoveStatus() == EPathFollowingStatus::Idle);
	const bool bShouldLoot = (Dist <= InteractionRange) || (bPathDone && Dist <= ArrivalLootRange);

	if (bShouldLoot)
	{
		LootCurrentTarget(OwnerComp);
		return;
	}

	// Path finished but still out of range — container unreachable; blacklist it for this sweep
	// (or FindNextContainer would re-select it forever) and continue with the next one.
	if (bMoveRequested && bPathDone)
	{
		UE_LOG(LogCompanionLoot, Warning, TEXT("TickTask: %s unreachable (%.0f > %.0f) — skipping"),
			*GetNameSafe(Container), Dist, ArrivalLootRange);
		SkippedThisSweep.Add(Container);
		AdvanceSweep(OwnerComp);
	}
}

EBTNodeResult::Type UBTTask_CompanionLoot::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FailAndClear(OwnerComp);
	return EBTNodeResult::Aborted;
}

FString UBTTask_CompanionLoot::GetStaticDescription() const
{
	return FString::Printf(TEXT("Loot pinged container, sweep others within %.0f cm"), SweepRadius);
}

bool UBTTask_CompanionLoot::StartMoveToCurrentTarget(UBehaviorTreeComponent& OwnerComp)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	AActor* Container = CurrentTarget.Get();
	if (!IsValid(AIC) || !IsValid(Container)) return false;

	bMoveRequested = false;

	// Already close enough — TickTask loots on the next tick.
	if (const APawn* Pawn = AIC->GetPawn())
		if (DistanceFromPawnToActor(Pawn, Container) <= InteractionRange)
			return true;

	const EPathFollowingRequestResult::Type MoveResult =
		AIC->MoveToActor(Container, MoveAcceptRadius, true, true, false, nullptr, true);
	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogCompanionLoot, Warning, TEXT("StartMove: MoveToActor failed for %s"), *GetNameSafe(Container));
		return false;
	}

	bMoveRequested = true;
	return true;
}

void UBTTask_CompanionLoot::LootCurrentTarget(UBehaviorTreeComponent& OwnerComp)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	AActor* Container = CurrentTarget.Get();
	if (!IsValid(AIC) || !IsValid(Container)) return;

	AIC->StopMovement();

	APawn* Pawn = AIC->GetPawn();
	if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn))
		Companion->PlayLootMontage();

	ILootable::Execute_Loot(Container, Pawn);
	++LootedCount;
	bLootTriggered = true;
	LootWaitElapsed = 0.f;

	UE_LOG(LogCompanionLoot, Log, TEXT("Looted %s (%d so far), pausing %.1fs"),
		*GetNameSafe(Container), LootedCount, InterLootPause);
}

AActor* UBTTask_CompanionLoot::FindNextContainer(UBehaviorTreeComponent& OwnerComp) const
{
	const ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	const APawn* Pawn = IsValid(AIC) ? AIC->GetPawn() : nullptr;
	UWorld* World = OwnerComp.GetWorld();
	if (!IsValid(Pawn) || !World) return nullptr;

	TArray<AActor*> Lootables;
	UGameplayStatics::GetAllActorsWithInterface(World, ULootable::StaticClass(), Lootables);

	AActor* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	const float SweepRadiusSq = FMath::Square(SweepRadius);

	for (AActor* Candidate : Lootables)
	{
		if (!IsValid(Candidate) || !ILootable::Execute_CanLoot(Candidate)) continue;
		if (SkippedThisSweep.Contains(Candidate)) continue;
		if (FVector::DistSquared(Candidate->GetActorLocation(), SweepAnchor) > SweepRadiusSq) continue;
		// Same-room gate — a container it can't see is a container in another room; never chain
		// through walls/closed doors. The commanded (pinged) first target never routes through here,
		// so an explicitly pinged far container still works.
		if (!LootViewerHasLoS(World, Pawn, Candidate, Lootables, LootOcclusionTolerance)) continue;

		const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), Pawn->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}
	return Best;
}

void UBTTask_CompanionLoot::AdvanceSweep(UBehaviorTreeComponent& OwnerComp)
{
	bLootTriggered = false;
	LootWaitElapsed = 0.f;

	if (MaxContainersPerSweep <= 0 || LootedCount < MaxContainersPerSweep)
	{
		// Keep trying candidates until one accepts a move — unpathable ones get blacklisted.
		while (AActor* Next = FindNextContainer(OwnerComp))
		{
			CurrentTarget = Next;
			if (StartMoveToCurrentTarget(OwnerComp)) return;

			UE_LOG(LogCompanionLoot, Warning, TEXT("AdvanceSweep: no path to %s — skipping"), *GetNameSafe(Next));
			SkippedThisSweep.Add(Next);
		}
	}

	UE_LOG(LogCompanionLoot, Log, TEXT("Sweep complete — %d container(s) looted"), LootedCount);
	ClearCommandIfStillLoot(OwnerComp);
	CurrentTarget.Reset();
	FinishLatentTask(OwnerComp, LootedCount > 0 ? EBTNodeResult::Succeeded : EBTNodeResult::Failed);
}

void UBTTask_CompanionLoot::ClearCommandIfStillLoot(UBehaviorTreeComponent& OwnerComp)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!IsValid(AIC) || !IsValid(BB)) return;

	const ECompanionCommand Active =
		static_cast<ECompanionCommand>(BB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand));
	if (Active == ECompanionCommand::Loot)
		AIC->ClearActiveCommand();
}

void UBTTask_CompanionLoot::FailAndClear(UBehaviorTreeComponent& OwnerComp)
{
	if (ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner()))
		AIC->StopMovement();

	// Guarded clear: an abort caused by a fresh replacement command (breach/takedown) must not
	// wipe the command it was replaced by.
	ClearCommandIfStillLoot(OwnerComp);

	CurrentTarget.Reset();
	CommandedTarget.Reset();
	SkippedThisSweep.Reset();
	bMoveRequested = false;
	bLootTriggered = false;
	LootWaitElapsed = 0.f;
}
