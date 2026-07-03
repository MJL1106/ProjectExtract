// AI controller for the companion — perception, blackboard, behaviour tree.

#include "CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "AI/Cover/AICoverSlot.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Kismet/GameplayStatics.h"
#include "Character/ExtractionPlayerInterface.h"
#include "CompanionCharacter.h"
#include "Companion/CompanionRoute.h"
#include "WeaponBase.h"
#include "Movement/TraversalComponent.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogCompanionAI);

namespace
{
	// Single source of truth for the warp-fallback timer rate. Used both to schedule the
	// timer and to advance TimeSinceClosedToPlayer in TickWarpFallback.
	static constexpr float WarpTickInterval = 0.5f;

	// Cap on TryBindToPlayerTraversal retries (~10s at 0.5s intervals) to stop runaway
	// log spam if the player never spawns (e.g. PIE without a player pawn).
	static constexpr int32 MaxBindAttempts = 20;
}

const FName ACompanionAIController::BB_PlayerActor(TEXT("PlayerActor"));
const FName ACompanionAIController::BB_PlayerNeedsRevive(TEXT("PlayerNeedsRevive"));
const FName ACompanionAIController::BB_CombatTarget(TEXT("CombatTarget"));
const FName ACompanionAIController::BB_CoverLocation(TEXT("CoverLocation"));
const FName ACompanionAIController::BB_HasCoverPosition(TEXT("HasCoverPosition"));
const FName ACompanionAIController::BB_PlayerTraversalActive(TEXT("PlayerTraversalActive"));
const FName ACompanionAIController::BB_PlayerTraversalObstacle(TEXT("PlayerTraversalObstacle"));
const FName ACompanionAIController::BB_PlayerTraversalLanding(TEXT("PlayerTraversalLanding"));
const FName ACompanionAIController::BB_PlayerTraversalType(TEXT("PlayerTraversalType"));
const FName ACompanionAIController::BB_Posture(TEXT("Posture"));
const FName ACompanionAIController::BB_ScoringWeight_LoSPlayer(TEXT("ScoringWeight_LoSPlayer"));
const FName ACompanionAIController::BB_ScoringWeight_AvoidEnemy(TEXT("ScoringWeight_AvoidEnemy"));
const FName ACompanionAIController::BB_ScoringWeight_CoverFromTarget(TEXT("ScoringWeight_CoverFromTarget"));
const FName ACompanionAIController::BB_CoverSlot(TEXT("CoverSlot"));
const FName ACompanionAIController::BB_NextCoverSlot(TEXT("NextCoverSlot"));
const FName ACompanionAIController::BB_ActiveRoute(TEXT("ActiveRoute"));
const FName ACompanionAIController::BB_RouteActive(TEXT("RouteActive"));
const FName ACompanionAIController::BB_RouteBlocksCombat(TEXT("RouteBlocksCombat"));
const FName ACompanionAIController::BB_CompanionCommand(TEXT("CompanionCommand"));
const FName ACompanionAIController::BB_CommandTargetActor(TEXT("CommandTargetActor"));
const FName ACompanionAIController::BB_CommandTargetLocation(TEXT("CommandTargetLocation"));
const FName ACompanionAIController::BB_TakedownMethod(TEXT("TakedownMethod"));

ACompanionAIController::ACompanionAIController()
{
	// CompanionBehaviorTree is assigned by the designer on BP_CompanionAIController
	// (UPROPERTY EditDefaultsOnly). Do NOT hardcode /Game/ paths in C++.

	SetGenericTeamId(FGenericTeamId(0));

	// Perception component
	PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
	SetPerceptionComponent(*PerceptionComponent);

	// Sight config
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 4200.0f;
	SightConfig->LoseSightRadius = 4900.0f;
	SightConfig->PeripheralVisionAngleDegrees = 90.0f;
	SightConfig->SetMaxAge(5.0f);
	SightConfig->AutoSuccessRangeFromLastSeenLocation = 500.0f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

	// Hearing config
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 2000.0f;
	HearingConfig->SetMaxAge(3.0f);
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = false;

	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->ConfigureSense(*HearingConfig);
	PerceptionComponent->SetDominantSense(UAISense_Sight::StaticClass());
}

void ACompanionAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	CachedPlayerCharacter = Cast<APawn>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));

	if (!CompanionBehaviorTree)
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: No BehaviorTree assigned"), *GetName());
		return;
	}

	UBlackboardComponent* BBComp = nullptr;
	UseBlackboard(CompanionBehaviorTree->BlackboardAsset, BBComp);

	if (BBComp && CachedPlayerCharacter.IsValid())
		BBComp->SetValueAsObject(BB_PlayerActor, CachedPlayerCharacter.Get());

	RunBehaviorTree(CompanionBehaviorTree);

	// Diagnostic: log possession state so user can confirm config at a glance.
	const ACompanionCharacter* Companion = Cast<ACompanionCharacter>(InPawn);
	const FString WeaponClassName = (Companion && Companion->GetWeaponClass()) ? Companion->GetWeaponClass()->GetName() : TEXT("NONE");
	const float MaxEngageRange = Companion ? Companion->MaxEngageRange : -1.0f;
	const float SightRadius = SightConfig ? SightConfig->SightRadius : -1.0f;

	UE_LOG(LogCompanionAI, Log,
		TEXT("Possessed %s | BT=%s | WeaponClass=%s | MaxEngageRange=%.0f | Sight=%.0f"),
		*GetNameSafe(InPawn),
		*GetNameSafe(CompanionBehaviorTree),
		*WeaponClassName,
		MaxEngageRange,
		SightRadius);

	// Bind to player traversal events; start the warp-stuck poll.
	TryBindToPlayerTraversal();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			WarpStuckTimer, this, &ACompanionAIController::TickWarpFallback,
			WarpTickInterval, /*bLoop*/ true);
	}

	// Fail-loud if the designer left Tuning unset — otherwise mirror + warp silently no-op.
	if (!Tuning)
	{
		UE_LOG(LogCompanionAI, Warning,
			TEXT("ACompanionAIController: Tuning DA is not assigned. Mirror + Warp behaviour will be disabled."));
	}
}

void ACompanionAIController::OnUnPossess()
{
	// Must run before StopLogic/BB teardown — BB still valid here.
	ReleaseNextCoverSlotIfClaimed();

	if (BrainComponent)
		BrainComponent->StopLogic(TEXT("Unpossessed"));

	if (PlayerTraversalComp.IsValid())
	{
		PlayerTraversalComp->OnTraversalStarted.Remove(TraversalStartedHandle);
		PlayerTraversalComp->OnTraversalEnded.Remove(TraversalEndedHandle);
	}
	TraversalStartedHandle.Reset();
	TraversalEndedHandle.Reset();
	PlayerTraversalComp.Reset();
	TimeSinceClosedToPlayer = 0.f;
	TimeOnDifferentLevel = 0.f;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearAllTimersForObject(this);

	Super::OnUnPossess();
}

void ACompanionAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseNextCoverSlotIfClaimed();

	if (PlayerTraversalComp.IsValid())
	{
		PlayerTraversalComp->OnTraversalStarted.Remove(TraversalStartedHandle);
		PlayerTraversalComp->OnTraversalEnded.Remove(TraversalEndedHandle);
	}
	TraversalStartedHandle.Reset();
	TraversalEndedHandle.Reset();
	PlayerTraversalComp.Reset();
	TimeSinceClosedToPlayer = 0.f;
	TimeOnDifferentLevel = 0.f;

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearAllTimersForObject(this);

	Super::EndPlay(EndPlayReason);
}

void ACompanionAIController::TryBindToPlayerTraversal()
{
	UWorld* World = GetWorld();
	if (!World) return;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerCharacter(this, 0);
	UTraversalComponent* Comp = PlayerPawn ? PlayerPawn->FindComponentByClass<UTraversalComponent>() : nullptr;

	if (!IsValid(Comp))
	{
		// Player not spawned yet (or no traversal component) — retry shortly, up to a cap.
		++BindAttempts;
		if (BindAttempts >= MaxBindAttempts)
		{
			UE_LOG(LogCompanionAI, Warning,
				TEXT("TryBindToPlayerTraversal: gave up after %d attempts (no player pawn / no UTraversalComponent). Mirror behaviour disabled."),
				BindAttempts);
			World->GetTimerManager().ClearTimer(BindRetryTimer);
			return;
		}

		World->GetTimerManager().SetTimer(
			BindRetryTimer, this, &ACompanionAIController::TryBindToPlayerTraversal,
			0.5f, /*bLoop*/ false);
		return;
	}

	PlayerTraversalComp = Comp;
	TraversalStartedHandle = Comp->OnTraversalStarted.AddUObject(this, &ACompanionAIController::OnPlayerTraversalStarted);
	TraversalEndedHandle = Comp->OnTraversalEnded.AddUObject(this, &ACompanionAIController::OnPlayerTraversalEnded);

	World->GetTimerManager().ClearTimer(BindRetryTimer);
	BindAttempts = 0;

	UE_LOG(LogCompanionAI, Log, TEXT("TraversalStarted bound to player %s"), *GetNameSafe(PlayerPawn));
}

void ACompanionAIController::OnPlayerTraversalStarted(ETraversalType Type, float /*PlayRate*/, FVector ObstacleLocation, FVector LandingLocation)
{
	const APawn* MyPawn = GetPawn();
	const UCompanionTuningDataAsset* T = Tuning;
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!MyPawn || !T || !BB) return;

	if (!T->bMirrorPlayerTraversalEnabled) return;

	const float DistToObstacle = FVector::Dist(MyPawn->GetActorLocation(), ObstacleLocation);
	if (DistToObstacle > T->MirrorTriggerRange)
	{
		UE_LOG(LogCompanionAI, Verbose, TEXT("Mirror skipped — obstacle %.0f UU away (> %.0f)"), DistToObstacle, T->MirrorTriggerRange);
		return;
	}

	BB->SetValueAsBool(BB_PlayerTraversalActive, true);
	BB->SetValueAsVector(BB_PlayerTraversalObstacle, ObstacleLocation);
	BB->SetValueAsVector(BB_PlayerTraversalLanding, LandingLocation);
	BB->SetValueAsEnum(BB_PlayerTraversalType, static_cast<uint8>(Type));
}

void ACompanionAIController::OnPlayerTraversalEnded()
{
	// Do NOT clear BB here — let the mirror task finish its own catch-up first.
	// Mirror task clears BB when it succeeds/fails/aborts via OnTaskFinished.
	UE_LOG(LogCompanionAI, Verbose, TEXT("Player traversal ended (BB kept until mirror task completes)."));
}

void ACompanionAIController::TickWarpFallback()
{
	const APawn* MyPawn = GetPawn();
	const APawn* Player = CachedPlayerCharacter.Get();
	const UCompanionTuningDataAsset* T = Tuning;
	if (!MyPawn || !Player || !T) return;

	if (!T->bWarpToPlayerEnabled)
	{
		TimeSinceClosedToPlayer = 0.f;
		TimeOnDifferentLevel = 0.f;
		return;
	}

	const float DistToPlayer = FVector::Dist(MyPawn->GetActorLocation(), Player->GetActorLocation());
	if (DistToPlayer < T->WarpMinDistance)
	{
		TimeSinceClosedToPlayer = 0.f;
		return;
	}

	TimeSinceClosedToPlayer += WarpTickInterval;

	const float ZDiff = FMath::Abs(MyPawn->GetActorLocation().Z - Player->GetActorLocation().Z);
	constexpr float ZMismatchThreshold = 250.f;     // ~capsule height
	constexpr float ZMismatchTimeBudget = 3.0f;     // seconds

	if (ZDiff > ZMismatchThreshold)
	{
		TimeOnDifferentLevel += WarpTickInterval;
		if (TimeOnDifferentLevel > ZMismatchTimeBudget)
		{
			UE_LOG(LogCompanionAI, Log,
				TEXT("Z-mismatch warp triggered (ZDiff=%.0f, time=%.1fs)"),
				ZDiff, TimeOnDifferentLevel);
			ExecuteWarpBehindPlayer();
			TimeOnDifferentLevel = 0.f;
		}
	}
	else
	{
		TimeOnDifferentLevel = 0.f;
	}

	if (ShouldWarp())
		ExecuteWarpBehindPlayer();
}

bool ACompanionAIController::IsCompanionRecentlyRendered() const
{
	const APawn* MyPawn = GetPawn();
	const UWorld* World = GetWorld();
	const UCompanionTuningDataAsset* T = Tuning;
	if (!MyPawn || !World || !T) return false;

	const float SinceRendered = World->GetTimeSeconds() - MyPawn->GetLastRenderTime();
	return SinceRendered <= T->RecentlyRenderedTolerance;
}

bool ACompanionAIController::ShouldWarp() const
{
	const UCompanionTuningDataAsset* T = Tuning;
	const APawn* MyPawn = GetPawn();
	const APawn* Player = CachedPlayerCharacter.Get();
	if (!T || !MyPawn || !Player) return false;

	if (TimeSinceClosedToPlayer <= T->WarpStuckTimeout) return false;

	const float DistToPlayer = FVector::Dist(MyPawn->GetActorLocation(), Player->GetActorLocation());
	if (DistToPlayer <= T->WarpMinDistance) return false;

	// Hard threshold — after 2x the soft timeout, warp regardless of camera visibility.
	// Accepts a small immersion break to guarantee the companion never gets permanently stranded.
	const bool bHardThreshold = TimeSinceClosedToPlayer > T->WarpStuckTimeout * 2.f;
	if (bHardThreshold) return true;

	// Soft threshold — only warp when the companion is off-camera.
	return !IsCompanionRecentlyRendered();
}

bool ACompanionAIController::TeleportToLocation(const FVector& Location, const FRotator& Rotation)
{
	static constexpr float DefaultNavProjectExtent = 500.f;

	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World) return false;

	if (UTraversalComponent* OwnTrav = MyPawn->FindComponentByClass<UTraversalComponent>())
	{
		if (OwnTrav->IsBusy())
			OwnTrav->CancelTraversal();
	}

	StopMovement();

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys) return false;

	const float ProjectExtent = IsValid(Tuning) ? Tuning->WarpNavProjectExtent : DefaultNavProjectExtent;
	FNavLocation OutLoc;
	if (!NavSys->ProjectPointToNavigation(Location, OutLoc, FVector(ProjectExtent)))
	{
		UE_LOG(LogCompanionAI, Verbose, TEXT("TeleportToLocation: navmesh project failed near %s"), *Location.ToString());
		return false;
	}

	MyPawn->TeleportTo(OutLoc.Location, Rotation);
	return true;
}

void ACompanionAIController::ExecuteWarpBehindPlayer()
{
	const APawn* Player = CachedPlayerCharacter.Get();
	const UCompanionTuningDataAsset* T = Tuning;
	if (!Player || !T) return;

	const FVector PlayerLoc = Player->GetActorLocation();
	const FVector PlayerFwd = Player->GetActorForwardVector();
	const FVector Goal = PlayerLoc - PlayerFwd * T->WarpBehindOffset;

	const bool bWarped = TeleportToLocation(Goal, Player->GetActorRotation());

	const bool bHardWarp = TimeSinceClosedToPlayer > T->WarpStuckTimeout * 2.f;
	if (bWarped)
	{
		UE_LOG(LogCompanionAI, Log, TEXT("Warped companion to %s (behind player, %s threshold, stuck=%.1fs)"),
			*Goal.ToString(), bHardWarp ? TEXT("HARD") : TEXT("SOFT"), TimeSinceClosedToPlayer);
		TimeSinceClosedToPlayer = 0.f;
		TimeOnDifferentLevel = 0.f;
	}
}

void ACompanionAIController::ReleaseNextCoverSlotIfClaimed()
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	APawn* ControlledPawn = GetPawn();
	AAICoverSlot* PendingSlot = Cast<AAICoverSlot>(BB->GetValueAsObject(BB_NextCoverSlot));
	if (IsValid(PendingSlot) && IsValid(ControlledPawn) && PendingSlot->IsClaimedBy(ControlledPawn))
	{
		PendingSlot->Release(ControlledPawn);
		UE_LOG(LogCompanionAI, Log,
			TEXT("%s: released stale NextCoverSlot claim on %s (controller teardown)"),
			*GetName(), *PendingSlot->GetName());
	}
	BB->SetValueAsObject(BB_NextCoverSlot, nullptr);
}

void ACompanionAIController::IssueCommand(ECompanionCommand Command, ETakedownMethod Method, AActor* TargetActor, const FVector& TargetLocation)
{
	// Tear down any armed takedown from a prior command before overwriting BB keys.
	// Guarantees "latest ping replaces previous" even if the BT branch lacks an observer-abort.
	if (ACompanionCharacter* Comp = Cast<ACompanionCharacter>(GetPawn()))
		Comp->DisarmCommandedTakedown();

	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	const bool bNeedsTarget = (Command == ECompanionCommand::Breach || Command == ECompanionCommand::Takedown);
	if (bNeedsTarget && !IsValid(TargetActor))
	{
		UE_LOG(LogCompanionAI, Warning,
			TEXT("IssueCommand: command %d requires a valid TargetActor — command rejected."),
			static_cast<int32>(Command));
		return;
	}

	BB->SetValueAsEnum(BB_CompanionCommand,       static_cast<uint8>(Command));
	BB->SetValueAsObject(BB_CommandTargetActor,   TargetActor);
	BB->SetValueAsVector(BB_CommandTargetLocation, TargetLocation);
	BB->SetValueAsEnum(BB_TakedownMethod,         static_cast<uint8>(Method));

	UE_LOG(LogCompanionAI, Warning,
		TEXT("[IssueCommand] BB written: Command=%d Method=%d Target=%s Loc=%s"),
		static_cast<int32>(Command),
		static_cast<int32>(Method),
		*GetNameSafe(TargetActor),
		*TargetLocation.ToString());
}

void ACompanionAIController::ClearActiveCommand()
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	BB->SetValueAsEnum(BB_CompanionCommand,       static_cast<uint8>(ECompanionCommand::None));
	BB->SetValueAsObject(BB_CommandTargetActor,   nullptr);
	BB->ClearValue(BB_CommandTargetLocation);
	BB->SetValueAsEnum(BB_TakedownMethod,         static_cast<uint8>(ETakedownMethod::Knife));

	UE_LOG(LogCompanionAI, Verbose, TEXT("ClearActiveCommand: companion command cleared."));
}

void ACompanionAIController::ClearTraversalBlackboard()
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	BB->SetValueAsBool(BB_PlayerTraversalActive, false);
	BB->ClearValue(BB_PlayerTraversalObstacle);
	BB->ClearValue(BB_PlayerTraversalLanding);
	BB->ClearValue(BB_PlayerTraversalType);
}

void ACompanionAIController::StartRoute(ACompanionRoute* Route)
{
	if (!IsValid(Route) || Route->NumPoints() == 0)
	{
		UE_LOG(LogCompanionAI, Warning, TEXT("%s: StartRoute called with %s route"),
			*GetName(), IsValid(Route) ? TEXT("empty") : TEXT("null"));
		return;
	}

	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	// Fix 7: guard against double-start — if a route is already active, ignore
	if (BB->GetValueAsBool(BB_RouteActive))
	{
		UE_LOG(LogCompanionAI, Warning,
			TEXT("%s: StartRoute ignored — BB_RouteActive already true (route %s)"),
			*GetName(), *Route->GetName());
		return;
	}

	ClearTraversalBlackboard();
	StopMovement();

	BB->SetValueAsObject(BB_ActiveRoute, Route);
	BB->SetValueAsBool(BB_RouteActive, true);
	BB->SetValueAsBool(BB_RouteBlocksCombat, !Route->bInterruptibleByCombat);

	UE_LOG(LogCompanionAI, Log, TEXT("%s: StartRoute %s (%d pts, interruptible=%d)"),
		*GetName(), *Route->GetName(), Route->NumPoints(),
		Route->bInterruptibleByCombat ? 1 : 0);
}

void ACompanionAIController::StopRoute(bool bAborted)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	BB->SetValueAsBool(BB_RouteActive, false);
	BB->SetValueAsBool(BB_RouteBlocksCombat, false);
	BB->SetValueAsObject(BB_ActiveRoute, nullptr);

	UE_LOG(LogCompanionAI, Log, TEXT("%s: StopRoute (aborted=%d)"),
		*GetName(), bAborted ? 1 : 0);
}
