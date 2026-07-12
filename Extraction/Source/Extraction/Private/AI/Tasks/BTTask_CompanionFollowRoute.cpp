// BTTask_CompanionFollowRoute — one-shot ordered waypoint traversal for the companion.

#include "BTTask_CompanionFollowRoute.h"
#include "CompanionAIController.h"
#include "CompanionCharacter.h"
#include "Animation/CompanionAnimInstance.h"
#include "Companion/CompanionRoute.h"
#include "Character/ExtractionPlayer.h"
#include "HealthComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCompanionRoute, Log, All);
DEFINE_LOG_CATEGORY(LogCompanionRoute);

UBTTask_CompanionFollowRoute::UBTTask_CompanionFollowRoute()
{
	NodeName = TEXT("Companion Follow Route");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
}

uint16 UBTTask_CompanionFollowRoute::GetInstanceMemorySize() const
{
	return sizeof(FRouteMemory);
}

// ---------------------------------------------------------------------------
// ComputeRouteStartIndex — find the forward waypoint nearest to the pawn's
// projected position on the route polyline. Returns the endpoint index of
// the closest segment so the companion walks forward, never backtracks.
// ---------------------------------------------------------------------------

static int32 ComputeRouteStartIndex(const ACompanionRoute& Route, const FVector& PawnLocation)
{
	const int32 NumPoints = Route.NumPoints();
	if (NumPoints <= 1) return 0;

	float BestDistSq = TNumericLimits<float>::Max();
	int32 BestSegment = 0;

	for (int32 i = 0; i < NumPoints - 1; ++i)
	{
		const FVector A = Route.GetWorldPoint(i);
		const FVector B = Route.GetWorldPoint(i + 1);
		const FVector Closest = FMath::ClosestPointOnSegment(PawnLocation, A, B);
		const float DistSq = FVector::DistSquared(PawnLocation, Closest);

		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestSegment = i;
		}
	}

	// Forward endpoint of the best segment, clamped to valid range
	return FMath::Clamp(BestSegment + 1, 0, NumPoints - 1);
}

// ---------------------------------------------------------------------------
// ExecuteTask
// ---------------------------------------------------------------------------

EBTNodeResult::Type UBTTask_CompanionFollowRoute::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// Construct node memory FIRST: OnTaskFinished runs on every finish (incl. a synchronous
	// Failed from the guards below) and reads this memory, so it must always be freshly
	// constructed. Members (TWeakObjectPtr + PODs) own no heap — no manual dtor needed,
	// matching the project's BT-task convention.
	FRouteMemory* Mem = reinterpret_cast<FRouteMemory*>(NodeMemory);
	new (Mem) FRouteMemory();

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	ACompanionAIController* Controller = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (!BB || !IsValid(Controller)) return EBTNodeResult::Failed;

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Controller->GetPawn());
	if (!IsValid(Companion)) return EBTNodeResult::Failed;

	ACompanionRoute* Route = Cast<ACompanionRoute>(
		BB->GetValueAsObject(ACompanionAIController::BB_ActiveRoute));
	if (!IsValid(Route) || Route->NumPoints() == 0)
	{
		UE_LOG(LogCompanionRoute, Warning,
			TEXT("ExecuteTask — no valid route in BB_ActiveRoute"));
		return EBTNodeResult::Failed;
	}

	Mem->CachedRoute = Route;
	Mem->CachedCharacter = Companion;
	Mem->CachedController = Controller;

	// Fresh route — the walk is starting, so the hold flag must be down.
	Companion->SetRouteHoldingAtFinal(false);

	// Stale-cover teardown: a cover task aborted around route start (e.g. the post-breach combat
	// window) can leave the pose component latched bInCover with no owner. The anim rig correctly
	// zeroes the aim gate AND the grip layer for a tucked companion, so a stale latch walks the
	// whole route with the gun off the hands and no aim offset. The route owns the body now.
	if (const USkeletalMeshComponent* Mesh = Companion->GetMesh())
		if (UCompanionAnimInstance* Anim = Cast<UCompanionAnimInstance>(Mesh->GetAnimInstance()))
			if (Anim->IsInCover())
			{
				UE_LOG(LogCompanionRoute, Warning, TEXT("%s: route start with stale cover pose — tearing down"), *Companion->GetName());
				Anim->ExitCoverPose();
			}

	const int32 StartIndex = ComputeRouteStartIndex(*Route, Companion->GetActorLocation());
	Mem->CurrentIndex = StartIndex;
	Mem->Phase = (StartIndex == 0) ? ERoutePhase::MovingToStart : ERoutePhase::Moving;

	// Fix 11: cancel sprint, then snapshot locomotion state for restore
	Companion->SetSprinting(false);

	if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
	{
		Mem->OriginalMaxWalkSpeed = CMC->MaxWalkSpeed;
		Mem->OriginalMaxWalkSpeedCrouched = CMC->MaxWalkSpeedCrouched;
	}

	const FCompanionRouteWaypoint& StartWP = Route->GetWaypoint(StartIndex);
	ApplyStance(*Mem, StartWP.Stance, StartWP.SpeedOverride);

	// Route-wide player speed lock — released in OnTaskFinished regardless of exit path.
	if (Route->PlayerSpeedLock > 0.f)
	{
		if (AExtractionPlayer* LockedPlayer = Cast<AExtractionPlayer>(Controller->GetPlayerCharacter()))
		{
			LockedPlayer->SetRouteSpeedLock(Route->PlayerSpeedLock);
			Mem->CachedSpeedLockedPlayer = LockedPlayer;
		}
	}

	if (!IssueMoveToWaypoint(*Controller, *Mem, StartIndex))
		return EBTNodeResult::Failed;

	UE_LOG(LogCompanionRoute, Log,
		TEXT("%s: starting route %s (%d pts, start WP %d)"),
		*Companion->GetName(), *Route->GetName(), Route->NumPoints(), StartIndex);

	return EBTNodeResult::InProgress;
}

// ---------------------------------------------------------------------------
// TickTask
// ---------------------------------------------------------------------------

void UBTTask_CompanionFollowRoute::TickTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FRouteMemory* Mem = reinterpret_cast<FRouteMemory*>(NodeMemory);

	ACompanionRoute* Route = Mem->CachedRoute.Get();
	ACompanionAIController* Controller = Mem->CachedController.Get();
	ACompanionCharacter* Companion = Mem->CachedCharacter.Get();

	// Route or refs became stale mid-run
	if (!IsValid(Route) || !IsValid(Controller) || !IsValid(Companion))
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Fix 9: bail if companion is dead/downed
	UHealthComponent* HC = Companion->GetHealthComponent();
	if (IsValid(HC) && HC->IsDead())
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	const FCompanionRouteWaypoint& WP = Route->GetWaypoint(Mem->CurrentIndex);

	switch (Mem->Phase)
	{
	// ----- MovingToStart / Moving: poll path-following -----
	case ERoutePhase::MovingToStart:
	case ERoutePhase::Moving:
	{
		UpdateAimFocus(*Mem, Mem->CurrentIndex);

		UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
		if (!PF)
			return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

		const FVector PawnLoc = Companion->GetActorLocation();
		const FVector GoalLoc = Route->GetWorldPoint(Mem->CurrentIndex);
		const float DistSq = FVector::DistSquared(PawnLoc, GoalLoc);
		const float AcceptSq = FMath::Square(Route->WaypointAcceptanceRadius);
		const EPathFollowingStatus::Type Status = PF->GetStatus();

		const bool bArrived = (Status == EPathFollowingStatus::Idle) || (DistSq <= AcceptSq);
		if (!bArrived) break;

		// Arrived at waypoint
		Route->NotifyWaypointReached(Mem->CurrentIndex);
		Mem->bAnyReached = true;
		Mem->MoveFailCount = 0;

		// Check wait-for-player (fix 12: squared distance)
		if (WP.bWaitForPlayer)
		{
			const APawn* Player = Controller->GetPlayerCharacter();
			const float PlayerDistSq = IsValid(Player)
				? FVector::DistSquared(PawnLoc, Player->GetActorLocation()) : 0.f;
			const float WaitRadiusSq = FMath::Square(WP.WaitForPlayerRadius);

			if (PlayerDistSq > WaitRadiusSq)
			{
				Mem->Phase = ERoutePhase::WaitingForPlayer;
				Mem->WaitElapsed = 0.f;
				Mem->WaitTimeout = Route->MaxWaitForPlayerSeconds;
				Mem->WaitRadiusSq = WaitRadiusSq;
				break;
			}
		}

		// Check dwell
		if (WP.DwellSeconds > 0.f)
		{
			Mem->Phase = ERoutePhase::Dwelling;
			Mem->DwellElapsed = 0.f;
			Mem->DwellTarget = WP.DwellSeconds;
			break;
		}

		// Advance immediately
		const EBTNodeResult::Type Result = AdvanceToNext(OwnerComp, *Mem);
		if (Result != EBTNodeResult::InProgress)
			return FinishLatentTask(OwnerComp, Result);
		break;
	}

	// ----- WaitingForPlayer -----
	case ERoutePhase::WaitingForPlayer:
	{
		UpdateAimFocus(*Mem, Mem->CurrentIndex);
		Mem->WaitElapsed += DeltaSeconds;

		const APawn* Player = Controller->GetPlayerCharacter();

		// Null-player escape: if the player ref is gone (seamless travel / respawn /
		// momentarily unpossessed), proceed rather than hanging forever
		if (!IsValid(Player))
		{
			UE_LOG(LogCompanionRoute, Log,
				TEXT("%s: wait-for-player — player ref null at WP %d, proceeding"),
				*Companion->GetName(), Mem->CurrentIndex);
		}
		else
		{
			const float PlayerDistSq = FVector::DistSquared(
				Companion->GetActorLocation(), Player->GetActorLocation());
			const bool bPlayerClose = (PlayerDistSq <= Mem->WaitRadiusSq);
			const bool bTimedOut = (Mem->WaitTimeout > 0.f)
				&& (Mem->WaitElapsed >= Mem->WaitTimeout);

			if (!bPlayerClose && !bTimedOut) break;

			if (bTimedOut && !bPlayerClose)
			{
				UE_LOG(LogCompanionRoute, Log,
					TEXT("%s: wait-for-player timed out at WP %d (%.1fs)"),
					*Companion->GetName(), Mem->CurrentIndex, Mem->WaitElapsed);
			}
		}

		// Proceed to dwell or advance
		if (WP.DwellSeconds > 0.f)
		{
			Mem->Phase = ERoutePhase::Dwelling;
			Mem->DwellElapsed = 0.f;
			Mem->DwellTarget = WP.DwellSeconds;
			break;
		}

		const EBTNodeResult::Type Result = AdvanceToNext(OwnerComp, *Mem);
		if (Result != EBTNodeResult::InProgress)
			return FinishLatentTask(OwnerComp, Result);
		break;
	}

	// ----- Dwelling -----
	case ERoutePhase::Dwelling:
	{
		UpdateAimFocus(*Mem, Mem->CurrentIndex);
		Mem->DwellElapsed += DeltaSeconds;
		if (Mem->DwellElapsed < Mem->DwellTarget) break;

		const EBTNodeResult::Type Result = AdvanceToNext(OwnerComp, *Mem);
		if (Result != EBTNodeResult::InProgress)
			return FinishLatentTask(OwnerComp, Result);
		break;
	}

	// ----- Holding at final -----
	case ERoutePhase::Holding:
	{
		UpdateAimFocus(*Mem, Mem->CurrentIndex);
		break;
	}
	}
}

// ---------------------------------------------------------------------------
// AbortTask (fix 1 + fix 3: clear BB keys via StopRoute, once-only broadcast)
// ---------------------------------------------------------------------------

EBTNodeResult::Type UBTTask_CompanionFollowRoute::AbortTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FRouteMemory* Mem = reinterpret_cast<FRouteMemory*>(NodeMemory);

	ACompanionAIController* Controller = Mem->CachedController.Get();
	if (IsValid(Controller))
		Controller->StopMovement();

	// Fix 3: broadcast aborted only if we haven't already broadcast completed
	if (!Mem->bCompletedBroadcast)
	{
		ACompanionRoute* Route = Mem->CachedRoute.Get();
		if (IsValid(Route))
		{
			Route->NotifyRouteCompleted(true);
			Mem->bCompletedBroadcast = true;
		}
	}

	// Fix 1: clear BB keys so the route branch doesn't re-enter
	if (IsValid(Controller))
		Controller->StopRoute(true);

	ACompanionCharacter* Companion = Mem->CachedCharacter.Get();
	UE_LOG(LogCompanionRoute, Log, TEXT("%s: route aborted at WP %d"),
		IsValid(Companion) ? *Companion->GetName() : TEXT("Unknown"), Mem->CurrentIndex);

	return EBTNodeResult::Aborted;
}

// ---------------------------------------------------------------------------
// OnTaskFinished (fix 2: idempotent cleanup funnel for all exit paths)
// ---------------------------------------------------------------------------

void UBTTask_CompanionFollowRoute::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	// Memory is always freshly constructed: ExecuteTask runs placement-new before any guards,
	// so even a synchronous Failed path leaves valid default state (null weak ptrs, false
	// bools, index 0). All operations below no-op safely on defaults. No manual dtor needed.
	FRouteMemory* Mem = reinterpret_cast<FRouteMemory*>(NodeMemory);

	UE_LOG(LogCompanionRoute, Log,
		TEXT("[Route] OnTaskFinished (result=%d, WP=%d)"),
		static_cast<int32>(TaskResult), Mem->CurrentIndex);

	// Idempotent locomotion restore — safe to run twice (AbortTask may have run first)
	RestoreDefaults(*Mem);

	// Release the route-wide player speed lock, if one was applied. Safe to run when already
	// cleared or if the player went DBNO mid-route (crawl speed must not be stomped) — it just
	// re-fires OnRouteSpeedLockChanged(false), which the kit BP handles idempotently.
	if (AExtractionPlayer* LockedPlayer = Mem->CachedSpeedLockedPlayer.Get())
		LockedPlayer->ClearRouteSpeedLock();

	// Idempotent BB key clear — StopRoute is safe on already-cleared keys
	ACompanionAIController* Controller = Mem->CachedController.Get();
	if (IsValid(Controller))
	{
		const bool bAborted = (TaskResult != EBTNodeResult::Succeeded);
		Controller->StopRoute(bAborted);
	}

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

// ---------------------------------------------------------------------------
// IssueMoveToWaypoint (fix 8: bProjectDestinationToNavigation = true)
// ---------------------------------------------------------------------------

bool UBTTask_CompanionFollowRoute::IssueMoveToWaypoint(
	AAIController& Controller, FRouteMemory& Mem, int32 Index) const
{
	ACompanionRoute* Route = Mem.CachedRoute.Get();
	if (!IsValid(Route)) return false;

	const FVector Goal = Route->GetWorldPoint(Index);
	// Params: Dest, AcceptanceRadius, bStopOnOverlap, bUsePathfinding,
	//         bProjectDestinationToNavigation (param 5 = true), bCanStrafe
	const EPathFollowingRequestResult::Type Result =
		Controller.MoveToLocation(Goal, Route->WaypointAcceptanceRadius, false, true, true, true);

	Mem.bMoveIssued = true;

	UE_LOG(LogCompanionRoute, Verbose,
		TEXT("MoveTo WP %d (%.0f,%.0f,%.0f) result=%d"),
		Index, Goal.X, Goal.Y, Goal.Z, static_cast<int32>(Result));

	if (Result == EPathFollowingRequestResult::Failed)
	{
		++Mem.MoveFailCount;
		UE_LOG(LogCompanionRoute, Warning,
			TEXT("MoveToLocation failed for WP %d (attempt %d/%d)"),
			Index, Mem.MoveFailCount, MaxMoveFailsPerWaypoint);
		return false;
	}

	Mem.MoveFailCount = 0;
	return true;
}

// ---------------------------------------------------------------------------
// ApplyStance (fix 5: MaxWalkSpeedCrouched for crouch; route-wide CompanionSpeed resolution)
// ---------------------------------------------------------------------------

void UBTTask_CompanionFollowRoute::ApplyStance(
	FRouteMemory& Mem, ECompanionRouteStance Stance, float SpeedOverride) const
{
	ACompanionCharacter* Companion = Mem.CachedCharacter.Get();
	if (!IsValid(Companion)) return;

	UCharacterMovementComponent* CMC = Companion->GetCharacterMovement();
	if (!CMC) return;

	// Determine walk speed. Resolution order: per-waypoint SpeedOverride > route-wide
	// CompanionSpeed > stance default.
	float TargetSpeed = RelaxedSpeed;
	switch (Stance)
	{
	case ECompanionRouteStance::Alert:  TargetSpeed = AlertSpeed;  break;
	case ECompanionRouteStance::Crouch: TargetSpeed = CrouchSpeed; break;
	default: break;
	}

	const ACompanionRoute* Route = Mem.CachedRoute.Get();
	if (IsValid(Route) && Route->CompanionSpeed > 0.f)
		TargetSpeed = Route->CompanionSpeed;

	if (SpeedOverride > 0.f)
		TargetSpeed = SpeedOverride;

	// Fix 5: crouch speed goes to MaxWalkSpeedCrouched, standing speed to MaxWalkSpeed
	if (Stance == ECompanionRouteStance::Crouch)
	{
		CMC->MaxWalkSpeedCrouched = TargetSpeed;
		if (!Companion->bIsCrouched)
			Companion->Crouch();
	}
	else
	{
		CMC->MaxWalkSpeed = TargetSpeed;
		if (Companion->bIsCrouched)
			Companion->UnCrouch();
	}

	// Scripted aim: weapon up for Alert/Crouch, weapon down for Relaxed
	Companion->SetScriptedAim(Stance != ECompanionRouteStance::Relaxed);
}

// ---------------------------------------------------------------------------
// UpdateAimFocus (fix 6: use WP.Stance directly; fix 10: Relaxed face-travel;
//                 fix 12: focal-point dedup)
// ---------------------------------------------------------------------------

void UBTTask_CompanionFollowRoute::UpdateAimFocus(
	FRouteMemory& Mem, int32 TargetIndex) const
{
	ACompanionAIController* Controller = Mem.CachedController.Get();
	ACompanionRoute* Route = Mem.CachedRoute.Get();
	if (!IsValid(Controller) || !IsValid(Route)) return;

	const FCompanionRouteWaypoint& WP = Route->GetWaypoint(TargetIndex);
	// Fix 6: use WP.Stance directly, not DefaultStance substitution
	const ECompanionRouteStance Stance = WP.Stance;

	// Authored aim (waypoint focus actor / local override, or the route-wide focus actor)
	// takes priority regardless of stance
	if (Route->HasAuthoredAim(TargetIndex))
	{
		SetFocalPointCached(Mem, *Controller, Route->GetWaypointAimWorld(TargetIndex));
		return;
	}

	// Fix 10: Relaxed legs still face travel via path look-ahead (not ClearFocus, which
	// leaves stale yaw on bUseControllerDesiredRotation pawns). Alert/Crouch also use
	// look-ahead but with weapon up (handled by ApplyStance).
	UPathFollowingComponent* PF = Controller->GetPathFollowingComponent();
	ACompanionCharacter* Companion = Mem.CachedCharacter.Get();

	if (IsValid(PF) && IsValid(Companion) && PF->GetStatus() == EPathFollowingStatus::Moving)
	{
		const FVector PathTarget = PF->GetCurrentTargetLocation();
		const FVector PawnLoc = Companion->GetActorLocation();
		const FVector ToTarget = (PathTarget - PawnLoc).GetSafeNormal();

		if (!ToTarget.IsNearlyZero())
		{
			// Look-ahead at VIEW height, not actor-centre height: the controller now preserves
			// focal-point pitch (UpdateControlRotation override), so a centre-height point 300cm
			// out would aim the weapon ~13 degrees down for the whole leg.
			FVector LookAhead = PawnLoc + ToTarget * AimLookAheadDistance;
			LookAhead.Z = Companion->GetPawnViewLocation().Z;
			SetFocalPointCached(Mem, *Controller, LookAhead);
			return;
		}
	}

	// Fallback: use the route's computed aim point
	if (Stance != ECompanionRouteStance::Relaxed)
	{
		SetFocalPointCached(Mem, *Controller, Route->GetWaypointAimWorld(TargetIndex));
	}
	// Relaxed with no path movement and no override — clear focus is acceptable here
	// (pawn is stationary: dwelling/waiting, stale yaw doesn't matter)
}

// ---------------------------------------------------------------------------
// SetFocalPointCached (fix 12: skip redundant SetFocalPoint)
// ---------------------------------------------------------------------------

void UBTTask_CompanionFollowRoute::SetFocalPointCached(
	FRouteMemory& Mem, ACompanionAIController& Controller, const FVector& Point) const
{
	if (FVector::DistSquared(Mem.LastFocalPoint, Point) < FocalPointEpsilonSq)
		return;

	Mem.LastFocalPoint = Point;
	Controller.SetFocalPoint(Point, EAIFocusPriority::Gameplay);
}

// ---------------------------------------------------------------------------
// AdvanceToNext (fix 6 + fix 8: bounded loop instead of recursion, no
//                OnWaypointReached for skipped waypoints)
// ---------------------------------------------------------------------------

EBTNodeResult::Type UBTTask_CompanionFollowRoute::AdvanceToNext(
	UBehaviorTreeComponent& OwnerComp, FRouteMemory& Mem) const
{
	ACompanionRoute* Route = Mem.CachedRoute.Get();
	ACompanionAIController* Controller = Mem.CachedController.Get();
	if (!IsValid(Route) || !IsValid(Controller))
		return EBTNodeResult::Failed;

	const int32 NumPoints = Route->NumPoints();

	// Bounded loop: try each remaining waypoint until a move succeeds or we exhaust the route.
	// MoveFailCount accumulates across skipped waypoints — once MaxMoveFailsPerWaypoint total
	// failures hit, finish the task rather than silently skipping the entire route.
	while (true)
	{
		++Mem.CurrentIndex;

		if (Mem.CurrentIndex >= NumPoints)
			return HandleReachFinal(OwnerComp, Mem);

		const FCompanionRouteWaypoint& NextWP = Route->GetWaypoint(Mem.CurrentIndex);
		ApplyStance(Mem, NextWP.Stance, NextWP.SpeedOverride);
		Mem.Phase = ERoutePhase::Moving;

		if (IssueMoveToWaypoint(*Controller, Mem, Mem.CurrentIndex))
			return EBTNodeResult::InProgress;

		// Move failed — MoveFailCount was incremented inside IssueMoveToWaypoint
		if (Mem.MoveFailCount >= MaxMoveFailsPerWaypoint)
		{
			UE_LOG(LogCompanionRoute, Warning,
				TEXT("Aborting route after %d total move failures (at WP %d)"),
				Mem.MoveFailCount, Mem.CurrentIndex);
			return EBTNodeResult::Failed;
		}

		UE_LOG(LogCompanionRoute, Warning,
			TEXT("Skipping unreachable WP %d (fail %d/%d)"),
			Mem.CurrentIndex, Mem.MoveFailCount, MaxMoveFailsPerWaypoint);
	}
}

// ---------------------------------------------------------------------------
// HandleReachFinal (fix 3: once-only broadcast via bCompletedBroadcast;
//                   fix 1: no inline StopRoute — OnTaskFinished handles it)
// ---------------------------------------------------------------------------

EBTNodeResult::Type UBTTask_CompanionFollowRoute::HandleReachFinal(
	UBehaviorTreeComponent& OwnerComp, FRouteMemory& Mem) const
{
	ACompanionRoute* Route = Mem.CachedRoute.Get();
	ACompanionAIController* Controller = Mem.CachedController.Get();

	// Unlock combat regardless of end behaviour
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (BB)
		BB->SetValueAsBool(ACompanionAIController::BB_RouteBlocksCombat, false);

	// Fix 3: broadcast exactly once. If no waypoints were actually reached (all skipped),
	// broadcast as aborted rather than falsely reporting success.
	if (!Mem.bCompletedBroadcast && IsValid(Route))
	{
		const bool bAborted = !Mem.bAnyReached;
		Route->NotifyRouteCompleted(bAborted);
		Mem.bCompletedBroadcast = true;
	}

	if (!IsValid(Route) || !IsValid(Controller))
		return EBTNodeResult::Failed;

	const int32 LastIndex = Route->NumPoints() - 1;

	if (Route->EndBehavior == ECompanionRouteEndBehavior::HoldAtFinal)
	{
		Mem.CurrentIndex = LastIndex;
		Mem.Phase = ERoutePhase::Holding;
		SetFocalPointCached(Mem, *Controller, Route->GetWaypointAimWorld(LastIndex));

		// The walk is done — flag the hold so player commands (breach) re-enable even though
		// this task stays latent and BB_RouteActive stays true.
		if (ACompanionCharacter* Character = Mem.CachedCharacter.Get())
			Character->SetRouteHoldingAtFinal(true);

		UE_LOG(LogCompanionRoute, Log, TEXT("Route complete — holding at final WP %d"), LastIndex);
		return EBTNodeResult::InProgress;
	}

	// ReturnToFollow — OnTaskFinished will call StopRoute + RestoreDefaults on Succeeded
	UE_LOG(LogCompanionRoute, Log, TEXT("Route complete — returning to follow"));
	return EBTNodeResult::Succeeded;
}

// ---------------------------------------------------------------------------
// RestoreDefaults (fix 5 + fix 11: restore both speeds + original low-ready)
// ---------------------------------------------------------------------------

void UBTTask_CompanionFollowRoute::RestoreDefaults(FRouteMemory& Mem) const
{
	ACompanionCharacter* Companion = Mem.CachedCharacter.Get();
	ACompanionAIController* Controller = Mem.CachedController.Get();

	if (IsValid(Companion))
	{
		if (Companion->bIsCrouched)
			Companion->UnCrouch();

		// Always clear scripted aim + the hold flag on route exit
		Companion->SetScriptedAim(false);
		Companion->SetRouteHoldingAtFinal(false);

		// Restore both walk speeds
		if (UCharacterMovementComponent* CMC = Companion->GetCharacterMovement())
		{
			if (Mem.OriginalMaxWalkSpeed > 0.f)
				CMC->MaxWalkSpeed = Mem.OriginalMaxWalkSpeed;
			if (Mem.OriginalMaxWalkSpeedCrouched > 0.f)
				CMC->MaxWalkSpeedCrouched = Mem.OriginalMaxWalkSpeedCrouched;
		}
	}

	if (IsValid(Controller))
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
}

// ---------------------------------------------------------------------------
// GetStaticDescription
// ---------------------------------------------------------------------------

FString UBTTask_CompanionFollowRoute::GetStaticDescription() const
{
	return TEXT("Follow the ACompanionRoute from BB_ActiveRoute. "
		"Per-waypoint stance, aim, dwell, and wait-for-player. "
		"End behaviour: ReturnToFollow (Succeeded) or HoldAtFinal (InProgress until abort).");
}
