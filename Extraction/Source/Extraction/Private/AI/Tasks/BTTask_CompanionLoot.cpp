// BT task — companion commanded loot sweep.

#include "AI/Tasks/BTTask_CompanionLoot.h"
#include "AI/CompanionAIController.h"
#include "Companion/CompanionCharacter.h"
#include "World/Lootable.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
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

// Horizontal (2D) distance from a point to the nearest XY point on a collision AABB footprint.
// Robust to wide cabinets whose pivot sits at one end, immune to vertical offset. The caller
// fetches the bounds once and passes them to both this and VerticalDistFromBounds, avoiding a
// redundant GetComponentsBoundingBox call per tick.
static float HorizontalDistFromBounds(const FVector& PawnLoc, const FBox& Bounds, const FVector& FallbackLoc)
{
	if (!Bounds.IsValid) return FVector::Dist2D(PawnLoc, FallbackLoc);
	const float ClosestX = FMath::Clamp(PawnLoc.X, Bounds.Min.X, Bounds.Max.X);
	const float ClosestY = FMath::Clamp(PawnLoc.Y, Bounds.Min.Y, Bounds.Max.Y);
	return FVector::Dist2D(PawnLoc, FVector(ClosestX, ClosestY, PawnLoc.Z));
}

// Vertical distance from a Z coordinate to the nearest Z face of a collision AABB. Returns 0
// when the coordinate is within the box's vertical range. See HorizontalDistFromBounds for the
// single-fetch rationale.
static float VerticalDistFromBounds(float PawnZ, const FBox& Bounds, float FallbackZ)
{
	if (!Bounds.IsValid) return FMath::Abs(PawnZ - FallbackZ);
	if (PawnZ < Bounds.Min.Z) return Bounds.Min.Z - PawnZ;
	if (PawnZ > Bounds.Max.Z) return PawnZ - Bounds.Max.Z;
	return 0.f;
}

// Project a container's collision base onto the navmesh. The query originates at the container's
// Bounds.Min.Z (its bottom face) with a tight vertical half-extent (roughly one capsule height),
// so the search box stays on the container's own storey and cannot reach through a floor slab.
// Both an above-reject and a below-reject guard against inter-storey projection in stacked
// buildings. Shared by both BTTask_CompanionLoot and BTTask_CompanionExplore (public static).
bool UBTTask_CompanionLoot::ProjectContainerToNav(UWorld* World, const AActor* Container,
	float HorizExtent, float VertExtent, float AboveRejectTolerance, FVector& OutNavPoint)
{
	if (!World || !IsValid(Container)) return false;
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys) return false;

	const FBox Bounds = Container->GetComponentsBoundingBox(false);
	const FVector Origin = Container->GetActorLocation();
	const float BaseZ = Bounds.IsValid ? Bounds.Min.Z : Origin.Z;

	const FVector QueryOrigin(Origin.X, Origin.Y, BaseZ);
	const FVector QueryExtent(HorizExtent, HorizExtent, VertExtent);

	FNavLocation Projected;
	if (!NavSys->ProjectPointToNavigation(QueryOrigin, Projected, QueryExtent))
		return false;

	// Above-reject: landed on a higher storey.
	if (Projected.Location.Z > Origin.Z + AboveRejectTolerance)
		return false;

	// Below-reject: landed on a lower storey. The container's own floor sits at most one capsule
	// height below the collision base; anything further is a different storey.
	if (Projected.Location.Z < BaseZ - VertExtent)
		return false;

	OutNavPoint = Projected.Location;
	return true;
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

	// Horizontal + vertical proximity. Bounds fetched once for both gates (avoids a redundant
	// GetComponentsBoundingBox call per tick).
	const FBox ContainerBounds = Container->GetComponentsBoundingBox(false);
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector ContainerLoc = Container->GetActorLocation();
	const float HorizDist = HorizontalDistFromBounds(PawnLoc, ContainerBounds, ContainerLoc);
	const float VertDist = VerticalDistFromBounds(PawnLoc.Z, ContainerBounds, ContainerLoc.Z);

	// Asymmetric vertical gate: VerticalLootTolerance covers the legitimate draped-furniture case
	// (pawn on the floor, volume on furniture above on the same storey). BelowContainerRejectHeight
	// catches the through-floor case (pawn directly underneath on a lower storey).
	const float PawnBelowBase = ContainerBounds.IsValid
		? FMath::Max(0.f, ContainerBounds.Min.Z - PawnLoc.Z)
		: FMath::Max(0.f, ContainerLoc.Z - PawnLoc.Z);
	const bool bVertOk = (VertDist <= VerticalLootTolerance) && (PawnBelowBase <= BelowContainerRejectHeight);
	const bool bPathDone = (AIC->GetMoveStatus() == EPathFollowingStatus::Idle);
	const bool bShouldLoot = bVertOk
		&& ((HorizDist <= InteractionRange) || (bPathDone && HorizDist <= ArrivalLootRange));

	if (bShouldLoot)
	{
		LootCurrentTarget(OwnerComp);
		return;
	}

	// Path finished but still out of range — container unreachable; blacklist it for this sweep
	// (or FindNextContainer would re-select it forever) and continue with the next one.
	if (bMoveRequested && bPathDone)
	{
		UE_LOG(LogCompanionLoot, Warning, TEXT("TickTask: %s unreachable (horiz=%.0f vert=%.0f, range=%.0f/%.0f) — skipping"),
			*GetNameSafe(Container), HorizDist, VertDist, ArrivalLootRange, VerticalLootTolerance);
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

bool UBTTask_CompanionLoot::StartMoveToCurrentTarget(UBehaviorTreeComponent& OwnerComp, const FVector* PrecomputedNavGoal)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	AActor* Container = CurrentTarget.Get();
	if (!IsValid(AIC) || !IsValid(Container)) return false;

	bMoveRequested = false;

	// Already close enough (horizontal + vertical) — TickTask loots on the next tick.
	if (const APawn* Pawn = AIC->GetPawn())
	{
		const FBox Bounds = Container->GetComponentsBoundingBox(false);
		const FVector PLoc = Pawn->GetActorLocation();
		const FVector CLoc = Container->GetActorLocation();
		const float Below = Bounds.IsValid
			? FMath::Max(0.f, Bounds.Min.Z - PLoc.Z) : FMath::Max(0.f, CLoc.Z - PLoc.Z);
		if (HorizontalDistFromBounds(PLoc, Bounds, CLoc) <= InteractionRange
			&& VerticalDistFromBounds(PLoc.Z, Bounds, CLoc.Z) <= VerticalLootTolerance
			&& Below <= BelowContainerRejectHeight)
			return true;
	}

	// Use the pre-computed goal when available (FindNextContainer already projected), otherwise
	// project now. The projection queries from the container's collision base with a tight vertical
	// extent so it stays on the container's own storey.
	FVector NavGoal;
	if (PrecomputedNavGoal)
	{
		NavGoal = *PrecomputedNavGoal;
	}
	else if (!ProjectContainerToNav(AIC->GetWorld(), Container,
		NavProjectHorizontalExtent, NavProjectVerticalExtent, NavProjectAboveRejectTolerance, NavGoal))
	{
		UE_LOG(LogCompanionLoot, Warning, TEXT("StartMove: %s cannot project to navmesh (h=%.0f v=%.0f)"),
			*GetNameSafe(Container), NavProjectHorizontalExtent, NavProjectVerticalExtent);
		return false;
	}

	const EPathFollowingRequestResult::Type MoveResult =
		AIC->MoveToLocation(NavGoal, MoveAcceptRadius, /*bStopOnOverlap*/ true,
			/*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true,
			/*bCanStrafe*/ false);
	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogCompanionLoot, Warning, TEXT("StartMove: MoveToLocation failed for %s (nav goal %s)"),
			*GetNameSafe(Container), *NavGoal.ToString());
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

AActor* UBTTask_CompanionLoot::FindNextContainer(UBehaviorTreeComponent& OwnerComp, FVector& OutNavGoal) const
{
	const ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	const APawn* Pawn = IsValid(AIC) ? AIC->GetPawn() : nullptr;
	UWorld* World = OwnerComp.GetWorld();
	if (!IsValid(Pawn) || !World) return nullptr;

	TArray<AActor*> Lootables;
	UGameplayStatics::GetAllActorsWithInterface(World, ULootable::StaticClass(), Lootables);

	AActor* Best = nullptr;
	FVector BestNavGoal = FVector::ZeroVector;
	float BestDistSq = TNumericLimits<float>::Max();
	const float SweepRadiusSq = FMath::Square(SweepRadius);

	for (AActor* Candidate : Lootables)
	{
		// Cheap gates first.
		if (!IsValid(Candidate) || !ILootable::Execute_CanLoot(Candidate)) continue;
		if (SkippedThisSweep.Contains(Candidate)) continue;
		if (FVector::DistSquared(Candidate->GetActorLocation(), SweepAnchor) > SweepRadiusSq) continue;

		// Distance from pawn — skip if farther than current best (avoids the expensive LoS + nav
		// projection below for candidates that could never win the nearest-first selection).
		const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), Pawn->GetActorLocation());
		if (DistSq >= BestDistSq) continue;

		// Same-room gate — a container it can't see is a container in another room; never chain
		// through walls/closed doors. Cheaper than nav projection (two cached-scene line traces
		// vs a Recast poly query). The commanded (pinged) first target never routes through here,
		// so an explicitly pinged far container still works.
		if (!LootViewerHasLoS(World, Pawn, Candidate, Lootables, LootOcclusionTolerance)) continue;

		// Nav projection — the most expensive gate. Stores the result so the caller can skip
		// re-projecting in StartMoveToCurrentTarget.
		FVector CandidateNavGoal;
		if (!ProjectContainerToNav(World, Candidate,
			NavProjectHorizontalExtent, NavProjectVerticalExtent, NavProjectAboveRejectTolerance,
			CandidateNavGoal))
			continue;

		BestDistSq = DistSq;
		Best = Candidate;
		BestNavGoal = CandidateNavGoal;
	}

	if (Best) OutNavGoal = BestNavGoal;
	return Best;
}

void UBTTask_CompanionLoot::AdvanceSweep(UBehaviorTreeComponent& OwnerComp)
{
	bLootTriggered = false;
	LootWaitElapsed = 0.f;

	if (MaxContainersPerSweep <= 0 || LootedCount < MaxContainersPerSweep)
	{
		// Keep trying candidates until one accepts a move — unpathable ones get blacklisted.
		// Capped per tick: each FindNextContainer iterates all lootables with LoS and nav queries,
		// so an unbounded retry loop can burst hundreds of Recast queries in one frame.
		constexpr int32 MaxRetriesPerTick = 3;
		int32 Retries = 0;
		FVector NavGoal;
		while (AActor* Next = FindNextContainer(OwnerComp, NavGoal))
		{
			CurrentTarget = Next;
			if (StartMoveToCurrentTarget(OwnerComp, &NavGoal)) return;

			UE_LOG(LogCompanionLoot, Warning, TEXT("AdvanceSweep: no path to %s — skipping"), *GetNameSafe(Next));
			SkippedThisSweep.Add(Next);

			if (++Retries >= MaxRetriesPerTick)
			{
				// Defer remaining retries to the next tick. TickTask sees CurrentTarget invalid +
				// bMoveRequested false and re-enters AdvanceSweep via the "target vanished" path.
				CurrentTarget.Reset();
				bMoveRequested = false;
				return;
			}
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
