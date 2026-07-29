// AI controller for the companion — perception, blackboard, behaviour tree.

#include "CompanionAIController.h"
#include "AI/CompanionTuningDataAsset.h"
#include "AI/Tasks/BTTask_FollowPlayer.h" // LeaderLeashReleaseHysteresis — see GetFollowLeader
#include "AI/Cover/AICoverSlot.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Kismet/GameplayStatics.h"
#include "Character/ExtractionPlayerInterface.h"
#include "CompanionCharacter.h"
#include "HealthComponent.h"
#include "Companion/CompanionBarkTypes.h"
#include "Companion/CompanionRoute.h"
#include "WeaponBase.h"
#include "Movement/TraversalComponent.h"
#include "Components/SuppressionComponent.h"
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
const FName ACompanionAIController::BB_BreachType(TEXT("BreachType"));
const FName ACompanionAIController::BB_FollowLeader(TEXT("FollowLeader"));

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

float ACompanionAIController::GetHearingSenseMaxAge() const
{
	return HearingConfig ? HearingConfig->GetMaxAge() : 3.f;
}

void ACompanionAIController::NoteAlertedThreat(const FVector& Location)
{
	LastAlertedThreatLocation = Location;
	LastAlertedThreatTime = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : -1e9f;
}

bool ACompanionAIController::GetRecentAlertedThreat(float MaxAge, FVector& OutLocation) const
{
	if (MaxAge <= 0.f || LastAlertedThreatTime < 0.f) return false;
	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() - LastAlertedThreatTime > MaxAge) return false;
	OutLocation = LastAlertedThreatLocation;
	return true;
}

// ---------------------------------------------------------------------
// Follow leader — one published anchor for every system that has to form up on the same actor the
// follow task does: the follow-slot EQS context and the cover-switch monitor's formation point.
// Resolution itself stays in UBTTask_FollowPlayer::ResolveFollowLeader (throttled TActorIterator +
// leash latches); this is the publish/read seam only.
// ---------------------------------------------------------------------

bool ACompanionAIController::IsUsableFollowLeader(const ACompanionCharacter* Leader, const ACompanionCharacter* Follower)
{
	if (!IsValid(Leader) || Leader == Follower) return false;
	if (!Leader->GetController()) return false; // a still-captive extractee has no brain to trail
	if (Leader->GetIsCompanionDBNO()) return false;

	const UHealthComponent* LeaderHealth = Leader->GetHealthComponent();
	return !IsValid(LeaderHealth) || !LeaderHealth->IsDead();
}

void ACompanionAIController::SetFollowLeader(AActor* Leader)
{
	// Assigned unconditionally: this member is the load-bearing value that GetFollowLeader and every
	// consumer read, so it must land even on a tick where the blackboard is unavailable.
	PublishedFollowLeader = Leader;

	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	// The per-tick dedup guards the blackboard WRITE only, and is keyed on what was last actually
	// mirrored — not on the member above. Keying it on the member meant a publish arriving while the BB
	// was momentarily null still counted as "already published", so the key stayed empty for that
	// leader for good.
	if (MirroredFollowLeader.Get() == Leader) return;

	// The key is authored on the companion blackboard asset in-engine. Until it exists, follow and
	// cover both still read the leader through the accessors above — only the blackboard mirror goes
	// dark — so this warns once and carries on rather than failing anything.
	if (BB->GetKeyID(BB_FollowLeader) == FBlackboard::InvalidKey)
	{
		if (!bFollowLeaderKeyWarned)
		{
			bFollowLeaderKeyWarned = true;
			UE_LOG(LogCompanionAI, Warning,
				TEXT("%s: blackboard has no '%s' key — add it as Object with base class Actor to mirror the follow leader."),
				*GetName(), *BB_FollowLeader.ToString());
		}
		return;
	}

	BB->SetValueAsObject(BB_FollowLeader, Leader);
	MirroredFollowLeader = Leader;
}

AActor* ACompanionAIController::GetFollowLeader() const
{
	AActor* Leader = PublishedFollowLeader.Get();
	if (!IsValid(Leader)) return CachedPlayerCharacter.Get();

	// A COMPANION leader is re-tested at read time, not just at publish time: cover scoring reads this
	// on the combat branch, long after the follow task that published it stopped ticking, so a primary
	// that goes DBNO mid-fight must stop anchoring the VIP straight away. The player never fails the
	// test (it isn't a companion) and so is returned unchanged.
	const ACompanionCharacter* LeaderCompanion = Cast<ACompanionCharacter>(Leader);
	if (!LeaderCompanion) return Leader;

	if (!IsUsableFollowLeader(LeaderCompanion, Cast<ACompanionCharacter>(GetPawn())))
		return CachedPlayerCharacter.Get();

	// The LEASH is re-tested here for the same reason and at the same level as capability above —
	// ResolveFollowLeader owns it, but that only runs on the follow branch, so a primary commanded away
	// while the VIP is fighting would otherwise anchor its cover scoring at a departing leader forever.
	// Overwatch happens to self-correct on its own radius; cover scoring has nothing that would.
	//
	// A BACKSTOP, not a co-equal arbiter — hence the margin, and it is load-bearing. This accessor is
	// const and cannot latch, and on the combat branch it is the SOLE authority because the task is not
	// ticking. Testing the same raw break distance the task tests would make a primary loitering on that
	// boundary flip the answer every service tick, and BTService_CoverSwitchMonitor reads this straight
	// into its formation anchor — so the anchor would jump the whole leader-to-player distance tick to
	// tick and churn the cover scorer's proximity term. That is the exact indecision this round removes.
	// Sitting a full release band ABOVE the break distance puts this strictly outside the task's latched
	// hysteresis, so it can only ever fire for the runaway leader it was added to catch, never contend
	// with a decision the task has already made.
	const APawn* Player = CachedPlayerCharacter.Get();
	const float BackstopDistance = Tuning
		? Tuning->SecondaryLeaderLeashDistance + UBTTask_FollowPlayer::LeaderLeashReleaseHysteresis : 0.f;
	if (Tuning && Tuning->SecondaryLeaderLeashDistance > 0.f && IsValid(Player)
		&& FVector::Dist2D(Leader->GetActorLocation(), Player->GetActorLocation()) > BackstopDistance)
	{
		return CachedPlayerCharacter.Get();
	}

	return Leader;
}

// NB deliberately NO UpdateControlRotation override (2nd attempt REVERTED 2026-07-12, director
// call): restoring pitch for location focals makes route/watch aims pitch the AO, and even with
// the aim gate + global clamps live and combat AO playtest-validated, the route look degraded
// (grip layer drops on cover-state flicker; gun off the hands on the stairs). Location focals
// stay pitch-flat (engine zeroes pitch for non-pawn focus); combat is unaffected (SetFocus on a
// pawn keeps pitch). Don't re-add without fixing the ABP grip cluster's bInCover zeroing first.

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

	// Suppression resistance from tuning — without this the companion keeps the component default
	// (1.0, two near-misses = pinned) while every enemy gets its archetype resistance.
	if (Companion)
	{
		if (USuppressionComponent* Supp = Companion->GetSuppressionComponent())
		{
			const UCompanionTuningDataAsset* SuppTuning = GetTuning();
			Supp->ConfigureSuppression(SuppTuning ? SuppTuning->SuppressionResistance : 2.5f);
		}
	}
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
	bWarpRefusalWarned = false;

	// The reference is scoped to the pawn this controller drives. Death and revive don't unpossess,
	// so this doesn't cost the player their section direction — it only stops a recycled controller
	// inheriting the last pawn's facing.
	FacingReferenceRoute.Reset();

	// Same scope, and the blackboard is rebuilt on the next possess: a stale published leader would
	// otherwise dedup the first publish of the new pawn's follow and leave the mirror key empty.
	PublishedFollowLeader.Reset();
	MirroredFollowLeader.Reset();
	bFollowLeaderKeyWarned = false;

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
	bWarpRefusalWarned = false;
	PublishedFollowLeader.Reset();
	MirroredFollowLeader.Reset();
	bFollowLeaderKeyWarned = false;

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

	// The player's own handler runs first on this broadcast and aborts the traversal on the
	// spot when its montage can't play — don't mirror a vault the player never performed.
	APawn* PlayerPawn = UGameplayStatics::GetPlayerCharacter(this, 0);
	const IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(PlayerPawn);
	if (PlayerIface && !PlayerIface->IsInTraversal()) return;

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

	// The refusal latch tracks one streak of "stranded and un-warpable". Whenever the stranded timer
	// resets the streak is over, so a later refusal is new and deserves its own Warning.
	if (!T->bWarpToPlayerEnabled)
	{
		TimeSinceClosedToPlayer = 0.f;
		TimeOnDifferentLevel = 0.f;
		bWarpRefusalWarned = false;
		return;
	}

	const float DistToPlayer = FVector::Dist(MyPawn->GetActorLocation(), Player->GetActorLocation());
	if (DistToPlayer < T->WarpMinDistance)
	{
		TimeSinceClosedToPlayer = 0.f;
		bWarpRefusalWarned = false;
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
			UE_LOG(LogCompanionAI, Verbose,
				TEXT("Z-mismatch warp triggered (ZDiff=%.0f, time=%.1fs)"),
				ZDiff, TimeOnDifferentLevel);
			ExecuteWarpBehindPlayer();

			// One warp attempt per tick, maximum. A refused warp no longer resets either timer, so
			// without this the ShouldWarp() block below would fire a second attempt on the same tick —
			// two nav projections and two teleport probes for one decision.
			return;
		}
	}
	else
	{
		TimeOnDifferentLevel = 0.f;
	}

	// Fallthrough for the non-Z case: same-floor stranding, judged on time and camera visibility.
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

bool ACompanionAIController::TeleportToLocation(const FVector& Location, const FRotator& Rotation, bool bForce)
{
	static constexpr float DefaultNavProjectExtent = 500.f;

	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World) return false;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys) return false;

	UTraversalComponent* OwnTrav = MyPawn->FindComponentByClass<UTraversalComponent>();

	// A traversing pawn cannot be probed truthfully: StartTraversal drops the capsule to NoCollision,
	// so the overlap test finds nothing and reports success vacuously — and CancelTraversal then moves
	// the pawn itself (depenetration + floor snap), invalidating the answer before the real teleport
	// runs. Refuse instead; traversals are short and every caller of the unforced path retries.
	if (!bForce && IsValid(OwnTrav) && OwnTrav->IsBusy())
	{
		UE_LOG(LogCompanionAI, Verbose,
			TEXT("TeleportToLocation: refused — traversal in progress, cannot probe %s; retrying once it ends"),
			*Location.ToString());
		return false;
	}

	const float ProjectExtent = IsValid(Tuning) ? Tuning->WarpNavProjectExtent : DefaultNavProjectExtent;
	FNavLocation OutLoc;
	if (!NavSys->ProjectPointToNavigation(Location, OutLoc, FVector(ProjectExtent)))
	{
		UE_LOG(LogCompanionAI, Verbose, TEXT("TeleportToLocation: navmesh project failed near %s"), *Location.ToString());
		return false;
	}

	// Probe first. Everything below this point is destructive to the companion's current state, and
	// callers retry on failure — a refused destination must cost nothing so the retry is free.
	// FindTeleportSpot is the non-mutating query: TeleportTo(bIsATest) still relocates the component,
	// it only suppresses the OnTeleported notification. The scratch vector absorbs the adjusted spot;
	// the real TeleportTo below stays the authoritative result.
	if (!bForce)
	{
		FVector ProbeLocation = OutLoc.Location;
		if (!World->FindTeleportSpot(MyPawn, ProbeLocation, Rotation))
		{
			UE_LOG(LogCompanionAI, Verbose,
				TEXT("TeleportToLocation: destination %s refused (encroachment / blocked) — pawn state untouched"),
				*OutLoc.Location.ToString());
			return false;
		}
	}

	if (IsValid(OwnTrav) && OwnTrav->IsBusy())
		OwnTrav->CancelTraversal();

	StopMovement();

	return MyPawn->TeleportTo(OutLoc.Location, Rotation, /*bIsATest*/ false, /*bNoCheck*/ false);
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
		bWarpRefusalWarned = false;
		return;
	}

	// Timers deliberately left running — the companion is still stranded, so the next warp tick
	// retries. A refused teleport is side-effect free, so the retry cadence costs nothing.

	// Past the hard threshold the companion is stranded beyond what the design tolerates AND cannot be
	// recovered — worth shouting about, but exactly once. bHardWarp latches true and this path runs
	// twice a second, so an unreachable goal would otherwise log a Warning for the rest of the session.
	// Everything else (soft-threshold retries, repeats of the same refusal) stays Verbose, formatted
	// lazily by UE_LOG so the suppressed path allocates nothing.
	if (bHardWarp && !bWarpRefusalWarned)
	{
		bWarpRefusalWarned = true;
		UE_LOG(LogCompanionAI, Warning,
			TEXT("Warp REFUSED at %s (HARD threshold, stuck=%.1fs) — companion stranded, retrying every %.1fs (further refusals logged at Verbose)"),
			*Goal.ToString(), TimeSinceClosedToPlayer, WarpTickInterval);
		return;
	}

	UE_LOG(LogCompanionAI, Verbose,
		TEXT("Warp REFUSED at %s (%s threshold, stuck=%.1fs) — companion still stranded, retrying next tick"),
		*Goal.ToString(), bHardWarp ? TEXT("HARD") : TEXT("SOFT"), TimeSinceClosedToPlayer);
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

void ACompanionAIController::IssueCommand(ECompanionCommand Command, ETakedownMethod Method, AActor* TargetActor,
	const FVector& TargetLocation, bool bPreserveSearchRoomExposure)
{
	// Tear down any armed takedown from a prior command before overwriting BB keys.
	// Guarantees "latest ping replaces previous" even if the BT branch lacks an observer-abort.
	if (ACompanionCharacter* Comp = Cast<ACompanionCharacter>(GetPawn()))
		Comp->DisarmCommandedTakedown();

	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	const bool bNeedsTarget = (Command == ECompanionCommand::Breach || Command == ECompanionCommand::Takedown || Command == ECompanionCommand::Loot);
	if (bNeedsTarget && !IsValid(TargetActor))
	{
		UE_LOG(LogCompanionAI, Warning,
			TEXT("IssueCommand: command %d requires a valid TargetActor — command rejected."),
			static_cast<int32>(Command));
		return;
	}

	if (!bPreserveSearchRoomExposure)
		if (ACompanionCharacter* Comp = Cast<ACompanionCharacter>(GetPawn()))
			Comp->EndSearchRoomExposure();

	BB->SetValueAsObject(BB_CommandTargetActor,   TargetActor);
	BB->SetValueAsVector(BB_CommandTargetLocation, TargetLocation);
	BB->SetValueAsEnum(BB_TakedownMethod,         static_cast<uint8>(Method));
	// CompanionCommand is the BT transition edge. Commit it last so a newly selected
	// branch always observes the complete payload for this command.
	BB->SetValueAsEnum(BB_CompanionCommand,       static_cast<uint8>(Command));

	UE_LOG(LogCompanionAI, Warning,
		TEXT("[IssueCommand] BB written: Command=%d Method=%d Target=%s Loc=%s"),
		static_cast<int32>(Command),
		static_cast<int32>(Method),
		*GetNameSafe(TargetActor),
		*TargetLocation.ToString());

	// Acknowledge the order out loud — the accepted-command confirm the player acts on.
	if (const ACompanionCharacter* Comp = Cast<ACompanionCharacter>(GetPawn()))
	{
		switch (Command)
		{
		case ECompanionCommand::Breach:   Comp->Bark(ECompanionBarkType::AckBreach); break;
		case ECompanionCommand::Takedown: Comp->Bark(ECompanionBarkType::AckTakedown); break;
		case ECompanionCommand::Loot:     Comp->Bark(ECompanionBarkType::AckLoot); break;
		case ECompanionCommand::Explore:  Comp->Bark(ECompanionBarkType::AckExplore); break;
		default: break;
		}
	}
}

void ACompanionAIController::SetBreachType(EBreachType Type)
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB) return;

	BB->SetValueAsEnum(BB_BreachType, static_cast<uint8>(Type));
}

void ACompanionAIController::ClearActiveCommand()
{
	if (ACompanionCharacter* Comp = Cast<ACompanionCharacter>(GetPawn()))
		Comp->EndSearchRoomExposure();

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

// ---------------------------------------------------------------------
// Facing reference — which way the current level section runs. Read by the companion state service
// while following; deliberately holds no blackboard key, since nothing in the BT branches on it.
// ---------------------------------------------------------------------

void ACompanionAIController::SetFacingReferenceRoute(ACompanionRoute* Route)
{
	// Name the check that failed. A facing reference that quietly does nothing is invisible in PIE,
	// and the ways to get it wrong — untick the flag, point the trigger at a one-waypoint marker —
	// all look identical from the level.
	const TCHAR* Rejection = nullptr;
	if (!IsValid(Route))                    Rejection = TEXT("route is null or being destroyed");
	else if (!Route->bUseAsFacingReference) Rejection = TEXT("bUseAsFacingReference is not ticked on the route");
	else if (Route->NumPoints() < 2)        Rejection = TEXT("route has fewer than 2 waypoints, so it has no direction");

	if (Rejection)
	{
		UE_LOG(LogCompanionAI, Warning,
			TEXT("%s: SetFacingReferenceRoute(%s) rejected — %s. Any existing reference has been cleared."),
			*GetName(), *GetNameSafe(Route), Rejection);
		FacingReferenceRoute.Reset();
		return;
	}

	FacingReferenceRoute = Route;

	UE_LOG(LogCompanionAI, Log, TEXT("%s: facing reference set to %s (%d pts)"),
		*GetName(), *Route->GetName(), Route->NumPoints());
}

ACompanionRoute* ACompanionAIController::GetFacingReferenceRoute() const
{
	return FacingReferenceRoute.Get();
}

void ACompanionAIController::ClearFacingReferenceRoute()
{
	if (const ACompanionRoute* Previous = FacingReferenceRoute.Get())
		UE_LOG(LogCompanionAI, Log, TEXT("%s: facing reference cleared (was %s)"), *GetName(), *Previous->GetName());

	FacingReferenceRoute.Reset();
}
