// BT task — companion approaches the player's traversed obstacle and plays its own traversal.

#include "BTTask_MirrorPlayerTraversal.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "AI/Cover/AICoverSlot.h"
#include "Companion/CompanionCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Movement/TraversalComponent.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
	// DepenetrateFromPlayer must be defined before TryTeleportToLanding which calls it.
	void DepenetrateFromPlayer(ACompanionAIController* AIC, FMirrorMemory* Mem)
	{
		if (!AIC || !Mem) return;

		ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
		APawn* PlayerPawn = AIC->GetPlayerCharacter();
		if (!IsValid(Companion) || !IsValid(PlayerPawn)) return;

		const UCapsuleComponent* CompanionCapsule = Companion->GetCapsuleComponent();
		const ACharacter* PlayerCharacter = Cast<ACharacter>(PlayerPawn);
		const UCapsuleComponent* PlayerCapsule = PlayerCharacter ? PlayerCharacter->GetCapsuleComponent() : nullptr;
		if (!IsValid(CompanionCapsule) || !IsValid(PlayerCapsule)) return;

		const UCompanionTuningDataAsset* T = AIC->GetTuning();
		if (!T) return;

		const FVector CompanionLoc = Companion->GetActorLocation();
		const FVector PlayerLoc    = PlayerPawn->GetActorLocation();

		const float CompanionRadius = CompanionCapsule->GetScaledCapsuleRadius();
		const float PlayerRadius    = PlayerCapsule->GetScaledCapsuleRadius();
		const float MinSep          = FMath::Max(T->MirrorPlayerClearance, CompanionRadius + PlayerRadius);

		const float Dist2D = FVector::Dist2D(CompanionLoc, PlayerLoc);
		if (Dist2D >= MinSep) return;

		FVector PushDir = FVector(CompanionLoc.X - PlayerLoc.X, CompanionLoc.Y - PlayerLoc.Y, 0.f).GetSafeNormal();
		if (PushDir.IsNearlyZero())
		{
			// Companion is exactly on top of player — pick a perpendicular push along the path axis.
			PushDir = FVector::CrossProduct(FVector::UpVector, Mem->PathDir).GetSafeNormal() * Mem->LateralSign;
		}

		// Final guard: if PushDir is still zero (e.g. PathDir was also near-zero), use a
		// stable fallback axis so NewLoc doesn't collapse onto the player's centre.
		if (PushDir.IsNearlyZero())
		{
			PushDir = Companion->GetActorRightVector();
			PushDir.Z = 0.f;
			PushDir = PushDir.GetSafeNormal();
			if (PushDir.IsNearlyZero())
				PushDir = FVector::RightVector;
		}

		FVector NewLoc = PlayerLoc + PushDir * MinSep;
		NewLoc.Z       = CompanionLoc.Z;

		if (UWorld* World = AIC->GetWorld())
		{
			if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
			{
				FNavLocation NavLoc;
				if (NavSys->ProjectPointToNavigation(NewLoc, NavLoc, FVector(150.f, 150.f, 200.f)))
					NewLoc = NavLoc.Location;
			}
		}

		bool bMoved = Companion->TeleportTo(NewLoc, Companion->GetActorRotation());
		if (!bMoved)
			bMoved = Companion->TeleportTo(NewLoc, Companion->GetActorRotation(), /*bIsATest=*/false, /*bNoCheck=*/true);

		UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] DepenetrateFromPlayer: dist=%.0f minSep=%.0f pushDir=%s newLoc=%s -> %s"),
			Dist2D, MinSep, *PushDir.ToCompactString(), *NewLoc.ToCompactString(), bMoved ? TEXT("OK") : TEXT("FAIL"));
	}

	bool TryTeleportToLanding(UBehaviorTreeComponent& OwnerComp, FMirrorMemory* Mem)
	{
		if (!Mem) return false;
		ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
		if (!AIC) return false;
		APawn* Pawn = AIC->GetPawn();
		UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
		if (!Pawn || !BB) return false;

		// OnPlayerTraversalEnded deliberately does NOT clear BB_PlayerTraversalLanding — this
		// function relies on that invariant to read a valid landing point after traversal ends.
		const FVector Landing = BB->GetValueAsVector(ACompanionAIController::BB_PlayerTraversalLanding);
		if (Landing.IsNearlyZero()) return false;

		// Use the raw Landing, then apply the same lateral offset used during approach so the
		// companion still ends up beside the player, not on top. If the narrow-obstacle fallback
		// zeroed the offset (bUseLateralOffset == false), the shift is zero and we land on the
		// player's exact spot — matching legacy behaviour.
		FVector Destination = Landing;
		if (Mem->bUseLateralOffset && !Mem->PathDir.IsNearlyZero())
		{
			const UCompanionTuningDataAsset* T = AIC->GetTuning();
			if (T)
			{
				const FVector Right = FVector::CrossProduct(FVector::UpVector, Mem->PathDir).GetSafeNormal();
				Destination += Right * Mem->LateralSign * T->MirrorLandingLateralOffset;
			}
		}

		// Cancel any in-flight traversal or approach first (defence — usually not in one here).
		if (UTraversalComponent* OwnTrav = Pawn->FindComponentByClass<UTraversalComponent>())
		{
			if (OwnTrav->IsBusy())
				OwnTrav->CancelTraversal();
		}

		// Slight Z lift so we don't spawn intersecting geometry. Pawn settles via gravity.
		Destination.Z += 20.f;

		const bool bTeleported = Pawn->TeleportTo(Destination, Pawn->GetActorRotation());
		UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] Per-obstacle fallback teleport: landing=%s dest=%s -> %s"),
			*Landing.ToCompactString(), *Destination.ToCompactString(), bTeleported ? TEXT("OK") : TEXT("FAIL"));
		DepenetrateFromPlayer(AIC, Mem);
		return bTeleported;
	}
}

UBTTask_MirrorPlayerTraversal::UBTTask_MirrorPlayerTraversal()
{
	NodeName = TEXT("Mirror Player Traversal");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	// bCreateNodeInstance left false: per-instance state lives on FMirrorMemory.
}

EBTNodeResult::Type UBTTask_MirrorPlayerTraversal::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// All guards run BEFORE placement-new — synchronous Failed paths skip OnTaskFinished,
	// which would leak FMirrorMemory's non-trivial members (TWeakObjectPtr, FDelegateHandle).
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	const UCompanionTuningDataAsset* T = AIC ? AIC->GetTuning() : nullptr;
	if (!T) return EBTNodeResult::Failed;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EBTNodeResult::Failed;

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn());
	if (!Companion) return EBTNodeResult::Failed;

	UTraversalComponent* OwnTraversal = Companion->GetTraversalComponent();
	if (!OwnTraversal) return EBTNodeResult::Failed;

	// Suppress mirror while companion is occupying a cover slot — vaulting the cover
	// wall mid-approach breaks the cover workflow.
	AAICoverSlot* CoverSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(ACompanionAIController::BB_CoverSlot));
	if (IsValid(CoverSlot) && CoverSlot->IsClaimedBy(Companion))
	{
		UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] Suppressed: cover slot active"));
		return EBTNodeResult::Succeeded;
	}

	// All guards passed — safe to allocate per-instance memory.
	FMirrorMemory* Mem = new (NodeMemory) FMirrorMemory();
	Mem->OwnTraversalComp = OwnTraversal;

	// Approach along the player's traversal axis, not from the companion's offset position —
	// keeps the trace square-on with the wall so the clearance trace inside TryStartTraversal
	// actually hits the obstacle.
	const FVector Landing = BB->GetValueAsVector(ACompanionAIController::BB_PlayerTraversalLanding);
	const FVector Obstacle = BB->GetValueAsVector(ACompanionAIController::BB_PlayerTraversalObstacle);
	const FVector PawnLoc = Companion->GetActorLocation();
	FVector PathDir = (Landing - Obstacle).GetSafeNormal2D();

	// If the player's traversal axis collapsed to zero (Landing≈Obstacle), fall back to
	// approaching from the companion's current position. If THAT's also zero, abort —
	// we're standing on the obstacle and can't compute a meaningful approach.
	if (PathDir.IsNearlyZero())
	{
		PathDir = (PawnLoc - Obstacle).GetSafeNormal2D();
		if (PathDir.IsNearlyZero())
		{
			UE_LOG(LogCompanionAI, Warning, TEXT("[MirrorTraversal] Approach direction collapsed (Landing≈Obstacle≈PawnLoc) — failing task."));
			Mem->~FMirrorMemory();
			return EBTNodeResult::Failed;
		}
	}

	// Compute the perpendicular (right) vector relative to the traversal axis.
	// PathDir points in the direction the player traveled (away from the obstacle);
	// Right = Cross(Up, PathDir) points to the actor's left when facing the obstacle.
	const FVector Right = FVector::CrossProduct(FVector::UpVector, PathDir).GetSafeNormal();

	// Determine which side the companion is already on relative to the obstacle centerline.
	// This keeps the companion on its natural side rather than forcing a fixed direction.
	const float Dot = FVector::DotProduct(PawnLoc - Obstacle, Right);
	const float LateralSign = (Dot >= 0.f) ? 1.f : -1.f;

	// Store traversal axis and side for use in TickTask facing and TryTeleportToLanding.
	Mem->PathDir = PathDir;
	Mem->LateralSign = LateralSign;
	Mem->bUseLateralOffset = true;

	FVector ApproachPoint = Obstacle - PathDir * T->MirrorEdgeApproachOffset
		+ Right * LateralSign * T->MirrorLandingLateralOffset;
	ApproachPoint.Z = PawnLoc.Z;
	Mem->ApproachPoint = ApproachPoint;
	Mem->Phase = FMirrorMemory::EPhase::Approach;
	Mem->Elapsed = 0.f;

	// Bind own traversal-ended → finish task succeeded. Capture a weak ref to the BT
	// component so a late broadcast (after the BT component is GC'd / level-transitioned)
	// can't dereference invalid memory. We also unbind in OnTaskFinished.
	// Capture MemPtr and gate on Phase==WaitForTraversalEnd so a stale TraversalEnded
	// broadcast from a traversal we did NOT start (e.g. one kicked off by a designer-placed
	// nav-link traversal that completed during the approach phase) can't finish us early.
	FMirrorMemory* MemPtr = Mem;
	TWeakObjectPtr<UBehaviorTreeComponent> WeakOwner(&OwnerComp);
	Mem->EndedHandle = OwnTraversal->OnTraversalEnded.AddLambda([WeakOwner, this, MemPtr]()
	{
		if (!MemPtr || MemPtr->Phase != FMirrorMemory::EPhase::WaitForTraversalEnd)
			return;
		UBehaviorTreeComponent* SafeOwner = WeakOwner.Get();
		if (!SafeOwner) return;
		DepenetrateFromPlayer(Cast<ACompanionAIController>(SafeOwner->GetAIOwner()), MemPtr);
		FinishLatentTask(*SafeOwner, EBTNodeResult::Succeeded);
	});

	// Sprint to catch up — mirrors BTTask_FollowPlayer's pattern.
	Companion->SetSprinting(true);

	UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] task started — Obstacle=%s ApproachPoint=%s LateralSign=%.0f"),
		*Obstacle.ToCompactString(), *Mem->ApproachPoint.ToCompactString(), LateralSign);

	// Pre-project the offset approach point to navmesh with a TIGHT extent matching the
	// lateral offset. Using a generous (500) extent here would allow projection to pull the
	// point back to the obstacle centerline, defeating the purpose of the offset.
	// If projection yanks the point back to within half the lateral offset of the centerline,
	// the offset was effectively cancelled (narrow/edge obstacle) — fall back to a zero-offset
	// approach (on the traversal axis) and re-project that with a generous extent instead.
	// Worst case equals legacy behaviour (land on player spot) rather than teleporting off-nav.
	if (UWorld* World = GetWorld())
	{
		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
		{
			FNavLocation OutLoc;
			const float TightExtentXY = T->MirrorLandingLateralOffset;
			constexpr float TightExtentZ = 200.f;
			if (NavSys->ProjectPointToNavigation(Mem->ApproachPoint, OutLoc, FVector(TightExtentXY, TightExtentXY, TightExtentZ)))
			{
				// Check if projection pulled the point back toward the obstacle centerline.
				// If the lateral clearance is less than half the intended offset, the obstacle
				// is too narrow for the offset to be useful — zero it out.
				const float LateralDist = FMath::Abs(FVector::DotProduct(OutLoc.Location - Obstacle, Right));
				if (LateralDist < T->MirrorLandingLateralOffset * 0.5f)
				{
					// Narrow obstacle: recompute a zero-offset approach point on the axis.
					UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] Narrow obstacle detected (LateralDist=%.0f < %.0f) — zeroing lateral offset."),
						LateralDist, T->MirrorLandingLateralOffset * 0.5f);
					Mem->bUseLateralOffset = false;
					FVector FallbackApproach = Obstacle - PathDir * T->MirrorEdgeApproachOffset;
					FallbackApproach.Z = PawnLoc.Z;
					FNavLocation FallbackLoc;
					if (NavSys->ProjectPointToNavigation(FallbackApproach, FallbackLoc, FVector(500.f, 500.f, 500.f)))
					{
						Mem->ApproachPoint = FallbackLoc.Location;
						UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] Fallback ApproachPoint (no offset) projected to nav: %s"), *Mem->ApproachPoint.ToCompactString());
					}
					else
					{
						Mem->ApproachPoint = FallbackApproach;
						UE_LOG(LogCompanionAI, Warning, TEXT("[MirrorTraversal] Fallback ApproachPoint failed nav projection — trying anyway."));
					}
				}
				else
				{
					Mem->ApproachPoint = OutLoc.Location;
					UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] ApproachPoint projected to nav: %s"), *Mem->ApproachPoint.ToCompactString());
				}
			}
			else
			{
				UE_LOG(LogCompanionAI, Warning, TEXT("[MirrorTraversal] ApproachPoint failed nav projection — trying anyway."));
			}
		}
	}

	const EPathFollowingRequestResult::Type MoveResult = AIC->MoveToLocation(
		Mem->ApproachPoint,
		T->MirrorReachToleranceXY,
		/*bStopOnOverlap*/ false,
		/*bUsePathfinding*/ true,
		/*bProjectDestinationToNavigation*/ true,
		/*bCanStrafe*/ true,
		nullptr,
		/*bAllowPartialPath*/ true);

	const FString MoveResultStr =
		MoveResult == EPathFollowingRequestResult::RequestSuccessful ? TEXT("Successful") :
		MoveResult == EPathFollowingRequestResult::AlreadyAtGoal ? TEXT("AlreadyAtGoal") :
		MoveResult == EPathFollowingRequestResult::Failed ? TEXT("Failed") : TEXT("Unknown");
	UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] MoveToLocation result: %s (DistToApproach=%.0f)"),
		*MoveResultStr, FVector::Dist2D(Companion->GetActorLocation(), Mem->ApproachPoint));

	return EBTNodeResult::InProgress;
}

void UBTTask_MirrorPlayerTraversal::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	// BT framework always allocates GetInstanceMemorySize() bytes — Mem cannot be null.
	FMirrorMemory* Mem = reinterpret_cast<FMirrorMemory*>(NodeMemory);

	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	const UCompanionTuningDataAsset* T = AIC ? AIC->GetTuning() : nullptr;
	if (!T) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Re-assert sprint every tick during approach — FollowPlayer's OnTaskFinished can
	// clear it after its abort even though we set it in ExecuteTask.
	if (Mem->Phase == FMirrorMemory::EPhase::Approach)
	{
		if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn))
			Companion->SetSprinting(true);
	}

	Mem->Elapsed += DeltaSeconds;

	Mem->DebugLogAccumulator += DeltaSeconds;
	if (Mem->DebugLogAccumulator >= 1.0f)
	{
		Mem->DebugLogAccumulator = 0.f;
		if (UE_LOG_ACTIVE(LogCompanionAI, Verbose))
		{
			const FVector PawnLocNow = Pawn->GetActorLocation();
			const float DistToApproach = FVector::Dist2D(PawnLocNow, Mem->ApproachPoint);
			const float Speed = Pawn->GetVelocity().Size2D();
			const FString PathState = AIC->GetMoveStatus() == EPathFollowingStatus::Moving ? TEXT("Moving")
				: AIC->GetMoveStatus() == EPathFollowingStatus::Idle ? TEXT("Idle")
				: AIC->GetMoveStatus() == EPathFollowingStatus::Paused ? TEXT("Paused") : TEXT("Waiting");
			UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] Heartbeat: Elapsed=%.1f Phase=%d DistToApproach=%.0f Speed=%.0f Path=%s"),
				Mem->Elapsed, (int32)Mem->Phase, DistToApproach, Speed, *PathState);
		}
	}

	// Catch-up timeout — fail and let warp safety net recover.
	if (Mem->Elapsed > T->CatchUpTimeout)
	{
		UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] CatchUpTimeout (%.2fs) — trying landing-teleport fallback."), Mem->Elapsed);
		if (TryTeleportToLanding(OwnerComp, Mem))
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	if (Mem->Phase != FMirrorMemory::EPhase::Approach)
		return;

	const FVector PawnLoc = Pawn->GetActorLocation();
	const float DistXY = FVector::Dist2D(PawnLoc, Mem->ApproachPoint);

	const EPathFollowingStatus::Type MoveStatus = AIC->GetMoveStatus();
	const bool bWithinTolerance = DistXY < T->MirrorReachToleranceXY;
	const bool bPathEndedNearby = (MoveStatus == EPathFollowingStatus::Idle && DistXY < 200.f && Mem->Elapsed > 0.5f);

	if (!bWithinTolerance && !bPathEndedNearby)
		return;

	if (bPathEndedNearby && !bWithinTolerance)
	{
		UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] Path ended at DistXY=%.0f (>tolerance) — forcing TryStartTraversal anyway."), DistXY);
	}

	UTraversalComponent* OwnTraversal = Mem->OwnTraversalComp.Get();
	if (!OwnTraversal) return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);

	// Face a laterally-shifted point on the wall so the actor's FORWARD stays perpendicular
	// to the obstacle surface. TryStartTraversal's detection uses GetActorForwardVector() and
	// requires FacingDot >= 0.5 — facing the raw obstacle from an offset position would angle
	// the trace relative to the wall and could miss. The shifted aim point keeps the actor
	// square-on while still facing the correct part of the wall.
	// If the narrow-obstacle fallback zeroed the offset (bUseLateralOffset == false), Right *
	// LateralSign * 0 = 0 and AimPoint collapses to ObstacleNow — identical to legacy.
	if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
	{
		const FVector ObstacleNow = BB->GetValueAsVector(ACompanionAIController::BB_PlayerTraversalObstacle);

		const float EffectiveLateralOffset = Mem->bUseLateralOffset ? T->MirrorLandingLateralOffset : 0.f;
		const FVector WallRight = FVector::CrossProduct(FVector::UpVector, Mem->PathDir).GetSafeNormal();
		const FVector AimPoint = ObstacleNow + WallRight * Mem->LateralSign * EffectiveLateralOffset;

		AIC->SetFocalPoint(AimPoint, EAIFocusPriority::Move);
		// Also snap actor rotation as backup — covers the same-frame trace before focus pulls.
		FVector ToAim = AimPoint - Pawn->GetActorLocation();
		ToAim.Z = 0.f;
		if (!ToAim.IsNearlyZero())
		{
			const FRotator FaceRot(0.f, ToAim.Rotation().Yaw, 0.f);
			Pawn->SetActorRotation(FaceRot);
		}
	}
	UE_LOG(LogCompanionAI, Verbose, TEXT("[MirrorTraversal] Pre-traversal yaw=%.1f, pawn=%s"), Pawn->GetActorRotation().Yaw, *Pawn->GetActorLocation().ToCompactString());

	UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] Approach reached (DistXY=%.1f) — TryStartTraversal"), DistXY);

	const bool bStarted = OwnTraversal->TryStartTraversal(/*bWasSprinting*/ true);
	if (!bStarted)
	{
		UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] TryStartTraversal returned false — trying landing-teleport fallback."));
		if (TryTeleportToLanding(OwnerComp, Mem))
			return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}

	Mem->Phase = FMirrorMemory::EPhase::WaitForTraversalEnd;

	// Guard against a synchronous traversal end: if TryStartTraversal completed immediately
	// (e.g. a zero-length montage or an internal early-exit), OnTraversalEnded may have already
	// fired before Phase was set to WaitForTraversalEnd — the lambda gated on that phase would
	// have silently skipped it. Finish the task here instead of riding to CatchUpTimeout.
	if (!OwnTraversal->IsInTraversal())
		return FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
}

void UBTTask_MirrorPlayerTraversal::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	// Mem is guaranteed non-null here: OnTaskFinished only runs for tasks that returned
	// InProgress (placement-new ran in ExecuteTask after all guards passed).
	FMirrorMemory* Mem = reinterpret_cast<FMirrorMemory*>(NodeMemory);

	UE_LOG(LogCompanionAI, Log, TEXT("[MirrorTraversal] OnTaskFinished (result=%d)"), (int32)TaskResult);

	if (UTraversalComponent* OwnTraversal = Mem->OwnTraversalComp.Get())
	{
		if (Mem->EndedHandle.IsValid())
			OwnTraversal->OnTraversalEnded.Remove(Mem->EndedHandle);
	}
	Mem->EndedHandle.Reset();

	if (ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner()))
	{
		AIC->ClearFocus(EAIFocusPriority::Move);
		AIC->StopMovement();
		if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(AIC->GetPawn()))
			Companion->SetSprinting(false);
		AIC->ClearTraversalBlackboard();
	}

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);

	// Destroy the placement-newed memory struct (FDelegateHandle / TWeakObjectPtr have non-trivial dtors).
	Mem->~FMirrorMemory();
}

FString UBTTask_MirrorPlayerTraversal::GetStaticDescription() const
{
	return TEXT("Mirror Player Traversal");
}
