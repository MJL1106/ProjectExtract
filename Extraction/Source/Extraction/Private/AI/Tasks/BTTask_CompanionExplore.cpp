// BT task — companion commanded search: breach-enter the pinged door, then engage/loot/dwell.

#include "AI/Tasks/BTTask_CompanionExplore.h"
#include "AIController.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionSearchRoomPolicy.h"
#include "AI/CompanionTuningDataAsset.h"
#include "CompanionBreachStatics.h"
#include "Companion/CompanionCharacter.h"
#include "Components/HealthComponent.h"
#include "Enemy/EnemyCharacter.h"
#include "World/Breachable.h"
#include "World/DoorBase.h"
#include "World/Lootable.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogCompanionExplore, Log, All);

DEFINE_LOG_CATEGORY_STATIC(LogCompanionExploreLoS, Log, All);

// Same-room gate: the radius alone reaches through walls into neighbouring rooms, which sent the
// companion chasing a crate behind a second door. A candidate only counts when the companion
// standing in the room can actually see it: eyes -> candidate bounds centre, ignoring the swung
// door leaf (it sits right beside the interior anchor and shadows crates near the doorway wall).
static bool ExploreViewerHasLoS(UWorld* World, const APawn* Viewer, const AActor* Candidate, const AActor* IgnoreDoor)
{
	FVector EyeLoc; FRotator EyeRot;
	Viewer->GetActorEyesViewPoint(EyeLoc, EyeRot);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionSearchLoS), false);
	Params.AddIgnoredActor(Viewer);
	Params.AddIgnoredActor(Candidate);
	if (IgnoreDoor) Params.AddIgnoredActor(IgnoreDoor);

	// Bounds centre, not actor location — crate pivots sit at a base corner and can bury the
	// trace end inside the shelf/table the crate stands on.
	const FVector Target = Candidate->GetComponentsBoundingBox().GetCenter();
	if (!World->LineTraceSingleByChannel(Hit, EyeLoc, Target, ECC_Visibility, Params)) return true;

	UE_LOG(LogCompanionExploreLoS, Log, TEXT("candidate %s rejected — LoS blocked by %s"),
		*GetNameSafe(Candidate), *GetNameSafe(Hit.GetActor()));
	return false;
}

// Any live VISIBLE enemy within the radius = the room is hot; the loot chain must not hold the
// command slot open (the command selector outranks the combat branch). LoS-gated so an enemy in a
// neighbouring room can't flip the pinged room hot through a wall.
static bool ExploreAnyLiveEnemyWithin(UWorld* World, const FVector& Center, float Radius,
	const APawn* Viewer, const AActor* IgnoreDoor)
{
	if (!World || !IsValid(Viewer)) return false;
	const float RadiusSq = FMath::Square(Radius);
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		const AEnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy)) continue;
		const UHealthComponent* Health = Enemy->GetHealthComponent();
		if (!IsValid(Health) || Health->IsDead()) continue;
		if (FVector::DistSquared(Enemy->GetActorLocation(), Center) > RadiusSq) continue;
		if (ExploreViewerHasLoS(World, Viewer, Enemy, IgnoreDoor)) return true;
	}
	return false;
}

// Nearest still-lootable container within the radius (mirrors BTTask_CompanionLoot's sweep filter),
// LoS-gated to the pinged room so the chain can't drag the companion through another door.
static AActor* ExploreFindNearestLootable(UWorld* World, const FVector& Center, float Radius,
	const APawn* Viewer, const AActor* IgnoreDoor)
{
	if (!World || !IsValid(Viewer)) return nullptr;

	TArray<AActor*> Lootables;
	UGameplayStatics::GetAllActorsWithInterface(World, ULootable::StaticClass(), Lootables);

	AActor* Best = nullptr;
	float BestDistSq = FMath::Square(Radius);
	for (AActor* Candidate : Lootables)
	{
		if (!IsValid(Candidate) || !ILootable::Execute_CanLoot(Candidate)) continue;
		const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), Center);
		if (DistSq > BestDistSq) continue;
		if (!ExploreViewerHasLoS(World, Viewer, Candidate, IgnoreDoor)) continue;
		BestDistSq = DistSq;
		Best = Candidate;
	}
	return Best;
}

UBTTask_CompanionExplore::UBTTask_CompanionExplore()
{
	NodeName = TEXT("Companion Search Door");
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

EBTNodeResult::Type UBTTask_CompanionExplore::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(AIC))
	{
		UE_LOG(LogCompanionExplore, Warning, TEXT("ExecuteTask: no CompanionAIController"));
		return EBTNodeResult::Failed;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!IsValid(BB))
	{
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	AActor* Door = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CommandTargetActor));
	return BeginSearch(OwnerComp, AIC, Door);
}

EBTNodeResult::Type UBTTask_CompanionExplore::BeginSearch(UBehaviorTreeComponent& OwnerComp,
	ACompanionAIController* AIC, AActor* Door)
{
	Phase = ESearchPhase::MovingToDoor;
	bHasStandPoint = false;
	bSkipMontage = false;
	AlignElapsed = 0.f;
	PhaseElapsed = 0.f;
	MoveElapsed = 0.f;
	MontageLength = 0.f;
	DwellDuration = 0.f;
	RoomAnchor = FVector::ZeroVector;
	CachedDoor.Reset();

	ADoorBase* DoorBase = Cast<ADoorBase>(Door);
	if (!IsValid(Door) || !DoorBase)
	{
		UE_LOG(LogCompanionExplore, Warning, TEXT("BeginSearch: target is not a door (%s)"), *GetNameSafe(Door));
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	if (DoorBase->IsExternalGateLocked())
	{
		UE_LOG(LogCompanionExplore, Log, TEXT("BeginSearch: %s is gate-locked"), *GetNameSafe(Door));
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	// A closed door gets the breach treatment; an already-open one is walked straight through.
	if (!IBreachable::Execute_CanBreach(Door))
	{
		if (!DoorBase->IsOpenForAcoustics())
		{
			UE_LOG(LogCompanionExplore, Log, TEXT("BeginSearch: %s is neither breachable nor open"), *GetNameSafe(Door));
			FailAndClear(OwnerComp);
			return EBTNodeResult::Failed;
		}
		bSkipMontage = true;
	}

	APawn* Pawn = AIC->GetPawn();
	if (!IsValid(Pawn))
	{
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	CachedDoor = Door;
	RoomAnchor = CompanionBreachStatics::ResolveInteriorAnchor(Door, Pawn);
	SetDoorAutoOpenSuppressed(true);
	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	bHadCombatTargetAtStart = IsValid(BB)
		&& IsValid(Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget)));

	// Stand point: where the breach montage was authored to play from. Doors provide one; a
	// door with none falls back to "in front of its origin, facing it".
	bHasStandPoint = IBreachable::Execute_GetBreachStandPoint(Door, Pawn, StandLocation, StandFacing);
	if (!bHasStandPoint)
	{
		const FVector ToDoor = Door->GetActorLocation() - Pawn->GetActorLocation();
		StandFacing = FRotator(0.f, ToDoor.Rotation().Yaw, 0.f);
		StandLocation = Door->GetActorLocation() - ToDoor.GetSafeNormal2D() * 110.f;
		StandLocation.Z = Pawn->GetActorLocation().Z;
	}

	const EPathFollowingRequestResult::Type MoveResult =
		AIC->MoveToLocation(StandLocation, StandPointAcceptRadius, /*bStopOnOverlap*/ true,
			/*bUsePathfinding*/ true, /*bProjectDestinationToNavigation*/ true);

	if (MoveResult == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogCompanionExplore, Warning, TEXT("BeginSearch: move to stand point failed for %s"), *GetNameSafe(Door));
		FailAndClear(OwnerComp);
		return EBTNodeResult::Failed;
	}

	UE_LOG(LogCompanionExplore, Log, TEXT("BeginSearch: searching through %s (openAlready=%d)"),
		*GetNameSafe(Door), bSkipMontage ? 1 : 0);
	return EBTNodeResult::InProgress;
}

void UBTTask_CompanionExplore::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	APawn* Pawn = IsValid(AIC) ? AIC->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Latest search ping wins: a fresh Explore command re-writes BB_CommandTargetActor but the
	// command enum stays Explore, so no observer-abort fires — watch the target key ourselves and
	// restart on the new door. Deferred while the montage plays out (aborting mid-kick looks
	// broken); the check catches up the first tick after.
	if (Phase != ESearchPhase::PlayingMontage && Phase != ESearchPhase::Holding)
	{
		const UBlackboardComponent* RetargetBB = OwnerComp.GetBlackboardComponent();
		AActor* BBTarget = IsValid(RetargetBB)
			? Cast<AActor>(RetargetBB->GetValueAsObject(ACompanionAIController::BB_CommandTargetActor))
			: nullptr;
		if (IsValid(BBTarget) && BBTarget != CachedDoor.Get())
		{
			UE_LOG(LogCompanionExplore, Log, TEXT("TickTask: re-pinged to %s — restarting search"), *GetNameSafe(BBTarget));
			AIC->StopMovement();
			SetDoorAutoOpenSuppressed(false);
			if (BeginSearch(OwnerComp, AIC, BBTarget) == EBTNodeResult::Failed)
				FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}
	}

	AActor* Door = CachedDoor.Get();
	if (!IsValid(Door) && Phase != ESearchPhase::Entering && Phase != ESearchPhase::Dwelling)
	{
		// Destroyed after the swing = the entry already happened; search the spot we reached.
		if (Phase == ESearchPhase::Holding)
		{
			EvaluateRoom(OwnerComp, AIC, Pawn);
			return;
		}

		UE_LOG(LogCompanionExplore, Warning, TEXT("TickTask: door destroyed mid-search"));
		if (Phase == ESearchPhase::PlayingMontage)
			if (ACharacter* Character = Cast<ACharacter>(Pawn)) Character->StopAnimMontage();
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Combat breaks off an in-flight search: the fight takes priority, and the player re-pings
	// the door afterwards. Only a NEWLY acquired target breaks off — a ping issued while already
	// fighting is a deliberate override and proceeds. Once the montage has started it completes —
	// the post-entry room check hands a hot room to the combat brain anyway.
	const UBlackboardComponent* TickBB = OwnerComp.GetBlackboardComponent();
	const bool bInCombat = IsValid(TickBB)
		&& IsValid(Cast<AActor>(TickBB->GetValueAsObject(ACompanionAIController::BB_CombatTarget)));
	const bool bFreshCombat = bInCombat && !bHadCombatTargetAtStart;

	if (bFreshCombat && (Phase == ESearchPhase::MovingToDoor || Phase == ESearchPhase::Aligning))
	{
		UE_LOG(LogCompanionExplore, Log, TEXT("TickTask: combat target acquired — breaking off search of %s"), *GetNameSafe(Door));
		FailAndClear(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	switch (Phase)
	{
	case ESearchPhase::MovingToDoor:
	{
		// Opened by something else on the way over degrades to the walk-in — never play a breach
		// anim at an open door. Still closed but no longer breachable (re-locked) fails instead.
		if (!bSkipMontage && !IBreachable::Execute_CanBreach(Door))
		{
			const ADoorBase* DoorBase = Cast<ADoorBase>(Door);
			if (!DoorBase || !DoorBase->IsOpenForAcoustics())
			{
				UE_LOG(LogCompanionExplore, Log, TEXT("TickTask: %s no longer searchable"), *GetNameSafe(Door));
				FailAndClear(OwnerComp);
				FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
				return;
			}
			bSkipMontage = true;
		}

		MoveElapsed += DeltaSeconds;
		if (AIC->GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			if (MoveElapsed < MoveTimeout) return;
			UE_LOG(LogCompanionExplore, Warning, TEXT("TickTask: walk to %s timed out"), *GetNameSafe(Door));
			FailAndClear(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}

		if (bSkipMontage)
		{
			// Open door: no align/montage — straight through to the interior point.
			if (!StartEnterMove(AIC, Pawn, Door))
			{
				EvaluateRoom(OwnerComp, AIC, Pawn);
			}
			return;
		}

		const float Dist = FVector::Dist2D(Pawn->GetActorLocation(), StandLocation);
		if (Dist > MaxAlignSnapDistance)
		{
			UE_LOG(LogCompanionExplore, Warning, TEXT("TickTask: path completed but too far from stand point (%.0f > %.0f)"),
				Dist, MaxAlignSnapDistance);
			FailAndClear(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}

		// Re-query the stand point from where the pawn actually ended up: nav projection can land
		// the move on the far side of the door, and the door flips the stand normal onto the
		// breacher's CURRENT side — so the align target is never through the closed panel.
		if (bHasStandPoint)
			IBreachable::Execute_GetBreachStandPoint(Door, Pawn, StandLocation, StandFacing);

		StartAlign(Pawn);
		return;
	}

	case ESearchPhase::Aligning:
	{
		// Opened elsewhere during the align beat — walk in instead of kicking an open door.
		if (!IBreachable::Execute_CanBreach(Door))
		{
			const ADoorBase* DoorBase = Cast<ADoorBase>(Door);
			if (DoorBase && DoorBase->IsOpenForAcoustics())
			{
				bSkipMontage = true;
				if (!StartEnterMove(AIC, Pawn, Door))
				{
					EvaluateRoom(OwnerComp, AIC, Pawn);
				}
				return;
			}
			FailAndClear(OwnerComp);
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}

		AlignElapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(AlignElapsed / AlignDuration, 0.f, 1.f);
		const float Eased = FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f);

		FVector NewLoc = FMath::Lerp(AlignStartLocation, StandLocation, Eased);
		NewLoc.Z = Pawn->GetActorLocation().Z; // keep the capsule's own floor height
		const float NewYaw = AlignStartYaw + FMath::FindDeltaAngleDegrees(AlignStartYaw, StandFacing.Yaw) * Eased;
		Pawn->SetActorLocation(NewLoc, /*bSweep*/ true); // don't shove through the player/door
		Pawn->SetActorRotation(FRotator(0.f, NewYaw, 0.f));

		if (Alpha < 1.f) return;

		// Aligned — start the montage; the door swings at the contact time. Breach type follows
		// the companion's mode AT THIS MOMENT (Combat=Loud kick, Stealth=Quiet, Normal=Tactical).
		// BB_BreachType (written at confirm, same mapping) is only the fallback when the pawn
		// isn't a companion.
		ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn);
		if (Companion)
		{
			switch (Companion->GetMode())
			{
			case ECompanionMode::Combat:  PendingBreachType = EBreachType::Loud;  break;
			case ECompanionMode::Stealth: PendingBreachType = EBreachType::Quiet; break;
			default:                      PendingBreachType = EBreachType::Tactical; break;
			}
		}
		else
		{
			const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
			PendingBreachType = BB
				? static_cast<EBreachType>(BB->GetValueAsEnum(ACompanionAIController::BB_BreachType))
				: EBreachType::Tactical;
		}

		MontageLength = Companion ? Companion->PlayBreachMontage(PendingBreachType) : 0.f;

		const UCompanionTuningDataAsset* Tuning = AIC->GetTuning();
		const float* TunedDelay = Tuning ? Tuning->BreachOpenDelay.Find(PendingBreachType) : nullptr;
		OpenDelay = TunedDelay ? *TunedDelay : DefaultOpenDelay;
		if (MontageLength > 0.f) OpenDelay = FMath::Min(OpenDelay, MontageLength);

		Phase = ESearchPhase::PlayingMontage;
		PhaseElapsed = 0.f;
		UE_LOG(LogCompanionExplore, Log, TEXT("TickTask: aligned at %s — montage len=%.2fs, door opens in %.2fs (type=%d)"),
			*GetNameSafe(Door), MontageLength, MontageLength > 0.f ? OpenDelay : 0.f, static_cast<int32>(PendingBreachType));
		return;
	}

	case ESearchPhase::PlayingMontage:
	{
		PhaseElapsed += DeltaSeconds;
		if (MontageLength > 0.f && PhaseElapsed < OpenDelay) return;

		const UBlackboardComponent* ActiveBB = OwnerComp.GetBlackboardComponent();
		const bool bStillSearchingThisDoor = IsValid(ActiveBB)
			&& static_cast<ECompanionCommand>(ActiveBB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand)) == ECompanionCommand::Explore
			&& ActiveBB->GetValueAsObject(ACompanionAIController::BB_CommandTargetActor) == Door;
		const UCompanionTuningDataAsset* Tuning = AIC->GetTuning();
		const float ExposureRadius = bStillSearchingThisDoor
			? (Tuning ? Tuning->ExploreLootRadius : 1200.f)
			: 0.f;
		if (CompanionBreachStatics::OpenBreachDoor(AIC, Door, PendingBreachType,
			RoomAnchor, ExposureRadius))
		{
			UE_LOG(LogCompanionExplore, Log, TEXT("TickTask: breached %s for search (type=%d)"),
				*GetNameSafe(Door), static_cast<int32>(PendingBreachType));
		}
		else
		{
			UE_LOG(LogCompanionExplore, Log, TEXT("TickTask: %s opened elsewhere during wind-up — skipping swing"), *GetNameSafe(Door));
		}

		Phase = ESearchPhase::Holding;
		PhaseElapsed = 0.f;
		return;
	}

	case ESearchPhase::Holding:
	{
		PhaseElapsed += DeltaSeconds;
		const float MontageRemainder = FMath::Max(0.f, MontageLength - OpenDelay);
		if (PhaseElapsed < MontageRemainder) return;

		// A firefight that started during the montage skips the walk-in — never step deeper into
		// a room that just started shooting. The room check still runs from the doorway.
		if (bFreshCombat || !StartEnterMove(AIC, Pawn, Door))
		{
			EvaluateRoom(OwnerComp, AIC, Pawn);
		}
		return;
	}

	case ESearchPhase::Entering:
	{
		PhaseElapsed += DeltaSeconds;

		// The entry already happened — a fresh firefight runs the room check from wherever the
		// companion got to, and the grant + guarded clear hand straight over to the combat brain.
		if (bFreshCombat)
		{
			AIC->StopMovement();
			EvaluateRoom(OwnerComp, AIC, Pawn);
			return;
		}

		if (AIC->GetMoveStatus() != EPathFollowingStatus::Idle && PhaseElapsed < EnterTimeout) return;
		if (AIC->GetMoveStatus() != EPathFollowingStatus::Idle) AIC->StopMovement();

		EvaluateRoom(OwnerComp, AIC, Pawn);
		return;
	}

	case ESearchPhase::Dwelling:
	{
		PhaseElapsed += DeltaSeconds;

		// ANY live combat target ends the dwell — the search already succeeded, so even a
		// held-over mid-fight target hands control back to the combat brain here.
		if (!bInCombat && PhaseElapsed < DwellDuration) return;

		UE_LOG(LogCompanionExplore, Log, TEXT("TickTask: dwell over (%.1fs%s) — returning to follow"),
			PhaseElapsed, bInCombat ? TEXT(", combat") : TEXT(""));
		FinishSearch(OwnerComp, AIC);
		return;
	}
	}
}

void UBTTask_CompanionExplore::StartAlign(APawn* Pawn)
{
	if (ACompanionAIController* AIC = Cast<ACompanionAIController>(Pawn->GetController()))
	{
		AIC->StopMovement();
		AIC->ClearFocus(EAIFocusPriority::Gameplay); // nothing may fight the breach facing
	}

	AlignStartLocation = Pawn->GetActorLocation();
	AlignStartYaw = Pawn->GetActorRotation().Yaw;
	AlignElapsed = 0.f;
	Phase = ESearchPhase::Aligning;
}

bool UBTTask_CompanionExplore::StartEnterMove(ACompanionAIController* AIC, APawn* Pawn, AActor* Door)
{
	FVector PostPoint;
	if (!IBreachable::Execute_GetPostBreachPoint(Door, Pawn, /*bEnterRoom*/ true, PostPoint))
	{
		UE_LOG(LogCompanionExplore, Warning, TEXT("StartEnterMove: no navigable interior point for %s — searching from the doorway"),
			*GetNameSafe(Door));
		return false;
	}

	// Full path only: a partial path "succeeds" by stopping in the doorway — exactly where the
	// companion must not stand.
	FAIMoveRequest MoveReq(PostPoint);
	MoveReq.SetAcceptanceRadius(EnterAcceptRadius);
	MoveReq.SetAllowPartialPath(false);
	MoveReq.SetProjectGoalLocation(true);
	MoveReq.SetUsePathfinding(true);

	if (AIC->MoveTo(MoveReq) == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogCompanionExplore, Warning, TEXT("StartEnterMove: enter move rejected for %s"), *GetNameSafe(Door));
		return false;
	}

	if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn);
		Companion && !Companion->IsSearchRoomExposureActive())
	{
		const UBlackboardComponent* ActiveBB = AIC->GetBlackboardComponent();
		const bool bStillSearchingThisDoor = IsValid(ActiveBB)
			&& static_cast<ECompanionCommand>(ActiveBB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand)) == ECompanionCommand::Explore
			&& ActiveBB->GetValueAsObject(ACompanionAIController::BB_CommandTargetActor) == Door;
		if (bStillSearchingThisDoor)
		{
			const UCompanionTuningDataAsset* Tuning = AIC->GetTuning();
			Companion->BeginSearchRoomExposure(RoomAnchor,
				Tuning ? Tuning->ExploreLootRadius : 1200.f, false);
		}
	}
	Phase = ESearchPhase::Entering;
	PhaseElapsed = 0.f;
	UE_LOG(LogCompanionExplore, Log, TEXT("StartEnterMove: entering room through %s"), *GetNameSafe(Door));
	return true;
}

void UBTTask_CompanionExplore::EvaluateRoom(UBehaviorTreeComponent& OwnerComp, ACompanionAIController* AIC, APawn* Pawn)
{
	// The dwell may outlast the task's ownership of the door — release it before deciding.
	SetDoorAutoOpenSuppressed(false);

	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Pawn);
	const UCompanionTuningDataAsset* Tuning = AIC->GetTuning();
	const float LootRadius = Tuning ? Tuning->ExploreLootRadius : 1200.f;
	const float GrantDuration = Tuning ? Tuning->ExploreEngagementDuration : 8.f;

	// Combat is weapons-free and treats a visible live enemy as hot. Normal and Stealth preserve
	// no-first-shot behavior, so an unaware/searching enemy can be watched while loot continues.
	if (IsValid(Companion) && LootRadius > 0.f)
	{
		const ECompanionMode Mode = Companion->GetMode();
		if (CompanionSearchRoomPolicy::CanEngageUnawareEnemy(Mode))
			Companion->SetPostBreachEngagement(RoomAnchor, LootRadius, GrantDuration);

		const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
		const bool bInCombat = IsValid(BB)
			&& IsValid(Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget)));
		AActor* SearchedDoor = CachedDoor.Get();
		const bool bVisibleLiveEnemy = Mode == ECompanionMode::Combat
			&& ExploreAnyLiveEnemyWithin(Companion->GetWorld(), RoomAnchor, LootRadius,
				Companion, SearchedDoor);
		if (CompanionSearchRoomPolicy::ShouldTreatRoomAsHot(Mode, bInCombat, bVisibleLiveEnemy))
		{
			UE_LOG(LogCompanionExplore, Log, TEXT("EvaluateRoom: room hot — handing to combat"));
			FinishSearch(OwnerComp, AIC);
			return;
		}

		if (AActor* Container = ExploreFindNearestLootable(Companion->GetWorld(), RoomAnchor, LootRadius, Companion, SearchedDoor))
		{
			UE_LOG(LogCompanionExplore, Log, TEXT("EvaluateRoom: room quiet — chaining loot sweep to %s"),
				*GetNameSafe(Container));
			AIC->IssueCommand(ECompanionCommand::Loot, ETakedownMethod::Knife,
				Container, Container->GetActorLocation(), true);
			// The Explore decorator observes CompanionCommand and self-aborts this task.
			// That abort owns the handoff; finishing this task again would cancel Loot.
			return;
		}
	}

	// Empty room: stand a beat, then hand back to follow.
	const float DwellMin = Tuning ? Tuning->SearchDwellMin : 2.f;
	const float DwellMax = Tuning ? Tuning->SearchDwellMax : 4.f;
	DwellDuration = FMath::FRandRange(DwellMin, FMath::Max(DwellMin, DwellMax));
	Phase = ESearchPhase::Dwelling;
	PhaseElapsed = 0.f;
	UE_LOG(LogCompanionExplore, Log, TEXT("EvaluateRoom: room empty — dwelling %.1fs"), DwellDuration);
}

void UBTTask_CompanionExplore::SetDoorAutoOpenSuppressed(bool bSuppressed)
{
	if (ADoorBase* Door = Cast<ADoorBase>(CachedDoor.Get()))
		Door->SetAutoOpenSuppressed(bSuppressed);
}

void UBTTask_CompanionExplore::FailAndClear(UBehaviorTreeComponent& OwnerComp)
{
	ACompanionAIController* AIC = Cast<ACompanionAIController>(OwnerComp.GetAIOwner());
	if (IsValid(AIC))
	{
		AIC->StopMovement();

		// Guarded clear: an abort caused by a fresh replacement command (takedown/loot) must not
		// wipe the command it was replaced by.
		const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
		const ECompanionCommand Active = IsValid(BB)
			? static_cast<ECompanionCommand>(BB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand))
			: ECompanionCommand::None;
		if (!IsValid(BB) || Active == ECompanionCommand::Explore)
			AIC->ClearActiveCommand();
	}

	SetDoorAutoOpenSuppressed(false);
	CachedDoor.Reset();
	Phase = ESearchPhase::MovingToDoor;
}

void UBTTask_CompanionExplore::FinishSearch(UBehaviorTreeComponent& OwnerComp, ACompanionAIController* AIC)
{
	SetDoorAutoOpenSuppressed(false);
	CachedDoor.Reset();

	// Guarded clear: the loot chain replaced the command — only a still-Explore slot is cleared.
	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	const ECompanionCommand Active = IsValid(BB)
		? static_cast<ECompanionCommand>(BB->GetValueAsEnum(ACompanionAIController::BB_CompanionCommand))
		: ECompanionCommand::None;
	if (!IsValid(BB) || Active == ECompanionCommand::Explore)
		AIC->ClearActiveCommand();

	FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
}

EBTNodeResult::Type UBTTask_CompanionExplore::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// A replacement command should not leave the breach anim playing — the montage remainder
	// still runs into the Holding phase, so cover both.
	const float MontageRemainder = FMath::Max(0.f, MontageLength - OpenDelay);
	const bool bMontageStillPlaying = (Phase == ESearchPhase::PlayingMontage)
		|| (Phase == ESearchPhase::Holding && PhaseElapsed < MontageRemainder);
	if (bMontageStillPlaying)
	{
		if (AAIController* AIC = OwnerComp.GetAIOwner())
			if (ACharacter* Character = Cast<ACharacter>(AIC->GetPawn())) Character->StopAnimMontage();
	}

	FailAndClear(OwnerComp);
	return EBTNodeResult::Aborted;
}

FString UBTTask_CompanionExplore::GetStaticDescription() const
{
	return TEXT("Search: breach-enter the pinged door, engage/loot the room, or dwell briefly then follow");
}
