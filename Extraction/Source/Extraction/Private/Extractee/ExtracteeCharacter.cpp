#include "Extractee/ExtracteeCharacter.h"

#include "Extractee/ExtracteeAIController.h"
#include "Extractee/ExtracteeTuningDataAsset.h"
#include "AI/Cover/AICoverSlot.h"
#include "AI/Cover/CoverRegistrySubsystem.h"
#include "Character/ExtractionPlayerInterface.h"
#include "Companion/CompanionCharacter.h"
#include "Enemy/EnemyCharacter.h"
#include "Enemy/EnemyDirectorSubsystem.h"
#include "Components/HealthComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "World/InteractionEventSubsystem.h"
#include "EngineUtils.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogExtractee);

namespace
{
	// State machine cadence -- reaction logic, not movement, so slow is fine.
	constexpr float StateEvalInterval = 0.25f;

	// Player 2D speed above which the follow anchor tracks their travel direction instead of
	// holding the bearing we already trail on. Mirrors the companion follow task's threshold.
	constexpr float PlayerMovingSpeed2DSq = 100.f * 100.f;

	// Follow anchor acceptance ring, as a fraction of FollowDistance. The stop gate uses the same
	// value so the gate and path-following arrival agree instead of fighting each other.
	constexpr float FollowAnchorAcceptFraction = 0.25f;

	// The anchor slides with the player, so a re-issue for anything smaller than this just restarts
	// the path and stutter-steps the extractee (same 200cm the companion follow task settled on).
	constexpr float FollowAnchorReissueDeadband = 200.f;

	// A companion can appear after the last scan (spawned, not placed), but TActorIterator is not
	// cheap enough to run on the 0.25s state tick -- rescan on this slower cadence instead.
	constexpr float CompanionRescanInterval = 2.f;

	// Primary + armed extractee VIP. Sized once on the first build; Reset() then keeps the slack, so
	// the Reserve is a deliberate no-op on every rebuild after that and none of them reallocate.
	constexpr int32 ExpectedCompanionCount = 2;
}

AExtracteeCharacter::AExtracteeCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->SetCapsuleSize(34.f, 88.f);

	// Pre-rescue the interact trace (ECC_Visibility) must be able to hit us; WorldInteract flips
	// this to Ignore so bullets pass through for the rest of the level.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bUseControllerDesiredRotation = true;
		MoveComp->bOrientRotationToMovement = false;
	}
	bUseControllerRotationYaw = false;

	AIControllerClass = AExtracteeAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void AExtracteeCharacter::BeginPlay()
{
	Super::BeginPlay();

	ApplyMoveSpeed(/*bSprint=*/false);

	// No per-frame work until rescued; WorldInteract enables tick (soft separation).
	SetActorTickEnabled(false);

	GetWorldTimerManager().SetTimer(StateTimerHandle, this,
		&AExtracteeCharacter::EvaluateState, StateEvalInterval, /*bLoop=*/true);
}

void AExtracteeCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(StateTimerHandle);
	ReleaseCover();
	Super::EndPlay(EndPlayReason);
}

void AExtracteeCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (State == EExtracteeState::Idle) return;
	TickSoftSeparation();
}

float AExtracteeCharacter::TakeDamage(float, const FDamageEvent&, AController*, AActor*)
{
	// Extractee is unkillable by design; no HealthComponent, no reaction.
	return 0.f;
}

// --- IWorldInteractable ---

bool AExtracteeCharacter::CanWorldInteract_Implementation(AActor* Interactor) const
{
	return State == EExtracteeState::Idle;
}

void AExtracteeCharacter::WorldInteract_Implementation(AActor* Interactor)
{
	if (State != EExtracteeState::Idle) return;

	// Rescued: from here on bullets (player and enemy hitscan both trace ECC_Visibility) pass
	// straight through -- the extractee can never shield anyone, and interact is no longer needed.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	UE_LOG(LogExtractee, Log, TEXT("Extractee rescued by %s -- following"), *GetNameSafe(Interactor));
	SetActorTickEnabled(true);
	RefreshPartyRefs();
	EnterFollow();

	// Success branch only — the state guard above is a refusal, and the player no longer raises
	// this blanket after every interact attempt.
	if (UInteractionEventSubsystem* Events = GetWorld() ? GetWorld()->GetSubsystem<UInteractionEventSubsystem>() : nullptr)
		Events->NotifyWorldInteract(this, Interactor);
}

FText AExtracteeCharacter::GetWorldInteractionPrompt_Implementation(AActor* Interactor) const
{
	return GetTuning()->InteractPrompt;
}

// --- State machine ---

void AExtracteeCharacter::EvaluateState()
{
	if (State == EExtracteeState::Idle) return;

	RefreshPartyRefs();
	if (!PlayerPawn.IsValid()) return;

	UpdateCombatSignal();

	if (State != EExtracteeState::Hold && IsPartyMemberDown())
	{
		EnterHold();
		return;
	}

	switch (State)
	{
	case EExtracteeState::Follow:    HandleFollow();    break;
	case EExtracteeState::SeekCover: HandleSeekCover(); break;
	case EExtracteeState::Cower:     HandleCower();     break;
	case EExtracteeState::CatchUp:   HandleCatchUp();   break;
	case EExtracteeState::Hold:      HandleHold();      break;
	default: break;
	}
}

void AExtracteeCharacter::HandleFollow()
{
	const UExtracteeTuningDataAsset* T = GetTuning();

	// Catch-up and the combat break stay keyed on the PLAYER: they measure how far the party has
	// left us behind, not where our formation slot sits.
	const float DistToPlayer = DistToPlayer2D();
	if (DistToPlayer > T->CatchUpStartDistance) { EnterCatchUp(); return; }
	if (IsCombatActive(T->CombatActiveWindow)) { EnterSeekCoverOrCower(); return; }

	AExtracteeAIController* AIC = GetExtracteeController();
	if (!AIC) return;

	// The follow/stop gate keys on the ANCHOR, not the player. The anchor already sits
	// FollowDistance astern, so a player-distance gate would compare the resting distance against
	// itself and stutter-step on the boundary forever.
	const FVector Anchor = ComputeFollowAnchor();
	const float DistToAnchor = FVector::Dist2D(GetActorLocation(), Anchor);
	const float AcceptRadius = T->FollowDistance * FollowAnchorAcceptFraction;

	if (DistToAnchor > AcceptRadius + T->FollowResumeSlack)
	{
		IssueFollowMove(Anchor, AcceptRadius);
		return;
	}

	// Between the ring and the slack is the hysteresis band -- a live move runs on undisturbed.
	if (DistToAnchor <= AcceptRadius && IsMoveActive())
	{
		AIC->StopMovement();
		SetMovementFacing(/*bMoving=*/false);
	}
	else if (!IsMoveActive())
	{
		SetMovementFacing(/*bMoving=*/false);
	}
}

FVector AExtracteeCharacter::ComputeFollowAnchor() const
{
	const APawn* Player = PlayerPawn.Get();
	if (!Player) return GetActorLocation();

	const FVector PlayerLoc = Player->GetActorLocation();
	const float FollowDistance = GetTuning()->FollowDistance;

	// Player moving -- straight back down their travel direction, no lateral offset.
	if (Player->GetVelocity().SizeSquared2D() > PlayerMovingSpeed2DSq)
	{
		const FVector MoveDir = Player->GetVelocity().GetSafeNormal2D();
		return PlayerLoc - MoveDir * FollowDistance;
	}

	// Player stationary -- hold the bearing we already sit on, or a settled extractee orbits them.
	FVector ToExtractee = (GetActorLocation() - PlayerLoc).GetSafeNormal2D();
	if (ToExtractee.IsNearlyZero())
		ToExtractee = (-Player->GetActorForwardVector()).GetSafeNormal2D();
	if (ToExtractee.IsNearlyZero())
		ToExtractee = FVector(1.f, 0.f, 0.f);

	return PlayerLoc + ToExtractee * FollowDistance;
}

void AExtracteeCharacter::IssueFollowMove(const FVector& Anchor, float AcceptRadius)
{
	AExtracteeAIController* AIC = GetExtracteeController();
	if (!AIC) return;

	// Deadband: the anchor slides with the player, so a live move is re-targeted rather than left
	// to finish -- but only once the anchor actually shifted, or every state tick restarts the path.
	// A finished or failed move always re-issues, so a path that ended short can never park us.
	// Deliberately 3D where the gate in HandleFollow is 2D: an anchor that changed storey without
	// moving in plan is exactly the case that MUST force a fresh path.
	if (bHasFollowAnchor && IsMoveActive()
		&& FVector::Dist(Anchor, LastFollowAnchor) < FollowAnchorReissueDeadband)
		return;

	LastFollowAnchor = Anchor;
	bHasFollowAnchor = true;

	// Projection OFF: the anchor is built at the player's Z, so on a staircase it sits metres above
	// the stair surface and on a corner it sits through a wall. Projection would snap those onto the
	// wrong poly (floor below, adjacent room), report success, and walk the civilian somewhere the
	// player never went -- silently skipping the fallback. Unprojected, that geometry fails instead.
	EPathFollowingRequestResult::Type MoveRes = AIC->MoveToLocation(Anchor, AcceptRadius,
		/*bStopOnOverlap=*/true, /*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/false,
		/*bCanStrafe=*/false);

	// Failed (off-mesh/blocked anchor) and AlreadyAtGoal (nothing started) both mean no path is
	// running, and neither may park the civilian: the player's own location is always nav-adjacent
	// and the acceptance ring still holds follow distance. LastFollowAnchor stays on the anchor so
	// the deadband suppresses the doomed re-issue while the fallback runs.
	if (MoveRes != EPathFollowingRequestResult::RequestSuccessful)
	{
		MoveRes = AIC->MoveToActor(PlayerPawn.Get(), GetTuning()->FollowDistance, /*bStopOnOverlap=*/true,
			/*bUsePathfinding=*/true, /*bCanStrafe=*/false);
	}

	// Facing tracks what actually happened, not what we asked for: orient-to-path only while a path
	// is really running, otherwise face the player. Setting it up-front left the civilian stood
	// still with focus cleared whenever both requests came back AlreadyAtGoal.
	SetMovementFacing(/*bMoving=*/MoveRes == EPathFollowingRequestResult::RequestSuccessful);
}

void AExtracteeCharacter::EnterSeekCoverOrCower()
{
	const UExtracteeTuningDataAsset* T = GetTuning();

	AEnemyCharacter* Threat = FindNearestCombatEnemy(T->CombatRelevanceRadius);
	UCoverRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UCoverRegistrySubsystem>() : nullptr;
	AAICoverSlot* Slot = (Threat && Registry)
		? Registry->FindBestCoverFor(GetActorLocation(), Threat, T->CoverSearchRadius, nullptr, this)
		: nullptr;

	// No reachable cover in range (or nothing to hide from) -> cower where we stand.
	if (!Slot || !Slot->TryClaim(this)) { EnterCower(); return; }

	ClaimedSlot = Slot;
	const float Standoff = GetCapsuleComponent()->GetScaledCapsuleRadius() + T->CoverStandoffPadding;
	CoverDestination = Slot->GetBehindCoverPosition(/*Alpha=*/0.5f, Standoff);

	AExtracteeAIController* AIC = GetExtracteeController();
	if (!AIC) { EnterCower(); return; }

	SetMovementFacing(/*bMoving=*/true);
	// Projection ON here and OFF for the follow anchor, deliberately: this destination is a small
	// standoff off a designer-placed cover slot, so the nearest poly is always the right one. The
	// follow anchor is a raw offset at the player's Z and can project onto a different storey.
	AIC->MoveToLocation(CoverDestination, /*AcceptanceRadius=*/T->CoverArriveTolerance * 0.5f,
		/*bStopOnOverlap=*/true, /*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true);
	State = EExtracteeState::SeekCover;
}

void AExtracteeCharacter::HandleSeekCover()
{
	const UExtracteeTuningDataAsset* T = GetTuning();

	if (DistToPlayer2D() > T->CatchUpStartDistance) { EnterCatchUp(); return; }

	const float DistToCover = FVector::Dist2D(GetActorLocation(), CoverDestination);
	if (DistToCover <= T->CoverArriveTolerance || !IsMoveActive())
	{
		// Arrived -- or the move failed, in which case cowering where we ended up is fine.
		EnterCower();
	}
}

void AExtracteeCharacter::EnterCower()
{
	if (AExtracteeAIController* AIC = GetExtracteeController())
		AIC->StopMovement();

	SetMovementFacing(/*bMoving=*/false);
	StartScaredLoop();
	State = EExtracteeState::Cower;
}

void AExtracteeCharacter::HandleCower()
{
	const UExtracteeTuningDataAsset* T = GetTuning();

	if (DistToPlayer2D() > T->CatchUpStartDistance) { EnterCatchUp(); return; }

	if (!IsCombatActive(T->CombatQuietExit))
	{
		StopScaredLoop();
		ReleaseCover();
		EnterFollow();
		return;
	}

	StartScaredLoop();	// keeps the montage looping while the state persists
}

void AExtracteeCharacter::EnterFollow()
{
	SetMovementFacing(/*bMoving=*/false);
	ApplyMoveSpeed(/*bSprint=*/false);
	// Every re-entry (rescue, post-Cower, post-CatchUp, post-Hold) re-issues from scratch -- an
	// anchor cached before the detour would sit inside the deadband and suppress the first move.
	bHasFollowAnchor = false;
	State = EExtracteeState::Follow;
}

void AExtracteeCharacter::EnterCatchUp()
{
	StopScaredLoop();
	ReleaseCover();
	ApplyMoveSpeed(/*bSprint=*/true);

	BestCatchUpDist = DistToPlayer2D();
	LastCatchUpProgressTime = GetWorld()->GetTimeSeconds();

	if (AExtracteeAIController* AIC = GetExtracteeController())
	{
		SetMovementFacing(/*bMoving=*/true);
		AIC->MoveToActor(PlayerPawn.Get(), GetTuning()->CaughtUpDistance * 0.5f,
			/*bStopOnOverlap=*/true, /*bUsePathfinding=*/true, /*bCanStrafe=*/false);
	}
	State = EExtracteeState::CatchUp;
}

void AExtracteeCharacter::HandleCatchUp()
{
	const UExtracteeTuningDataAsset* T = GetTuning();
	const float Dist = DistToPlayer2D();

	if (Dist <= T->CaughtUpDistance)
	{
		ApplyMoveSpeed(/*bSprint=*/false);
		if (AExtracteeAIController* AIC = GetExtracteeController())
			AIC->StopMovement();

		if (IsCombatActive(T->CombatActiveWindow)) EnterSeekCoverOrCower();
		else EnterFollow();
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Dist < BestCatchUpDist - T->CatchUpProgressEpsilon)
	{
		BestCatchUpDist = Dist;
		LastCatchUpProgressTime = Now;
	}
	else if (Now - LastCatchUpProgressTime > T->CatchUpStuckTimeout)
	{
		TeleportBehindPlayer();
		return;
	}

	// Path finished/failed while still far out (blocked door, partial path) -- re-issue.
	if (!IsMoveActive())
	{
		if (AExtracteeAIController* AIC = GetExtracteeController())
			AIC->MoveToActor(PlayerPawn.Get(), T->CaughtUpDistance * 0.5f, true, true, false);
	}
}

void AExtracteeCharacter::EnterHold()
{
	if (AExtracteeAIController* AIC = GetExtracteeController())
		AIC->StopMovement();

	ReleaseCover();
	ApplyMoveSpeed(/*bSprint=*/false);
	SetMovementFacing(/*bMoving=*/false);
	State = EExtracteeState::Hold;
}

void AExtracteeCharacter::HandleHold()
{
	const UExtracteeTuningDataAsset* T = GetTuning();

	if (IsPartyMemberDown())
	{
		if (IsCombatActive(T->CombatQuietExit)) StartScaredLoop();
		else StopScaredLoop();
		return;
	}

	StopScaredLoop();
	if (IsCombatActive(T->CombatActiveWindow)) EnterSeekCoverOrCower();
	else EnterFollow();
}

// --- Helpers ---

const UExtracteeTuningDataAsset* AExtracteeCharacter::GetTuning() const
{
	return Tuning ? Tuning.Get() : GetDefault<UExtracteeTuningDataAsset>();
}

AExtracteeAIController* AExtracteeCharacter::GetExtracteeController() const
{
	return Cast<AExtracteeAIController>(GetController());
}

void AExtracteeCharacter::RefreshPartyRefs()
{
	if (!PlayerPawn.IsValid())
		PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);

	UWorld* World = GetWorld();
	if (!World) return;

	// Rebuild when an entry goes stale, plus a slow rescan so a companion that appears after the
	// last scan still gets tracked (and so an empty list doesn't re-walk every state tick).
	// Deliberately not every tick -- TActorIterator walks the whole level.
	const float Now = World->GetTimeSeconds();
	bool bRebuild = (Now - LastCompanionScanTime) >= CompanionRescanInterval;
	if (!bRebuild)
	{
		for (const TWeakObjectPtr<ACompanionCharacter>& Entry : Companions)
		{
			if (Entry.IsValid()) continue;
			bRebuild = true;
			break;
		}
	}
	if (!bRebuild) return;

	LastCompanionScanTime = Now;
	Companions.Reset();
	Companions.Reserve(ExpectedCompanionCount);
	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
	{
		ACompanionCharacter* Comp = *It;
		if (IsValid(Comp)) Companions.Add(Comp);
	}
}

void AExtracteeCharacter::UpdateCombatSignal()
{
	const UWorld* World = GetWorld();
	if (!World) return;

	if (const UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
		LastCombatSignalTime = FMath::Max(LastCombatSignalTime, Director->GetLastCombatReportTime());

	// The director stamp marks combat ENTRY only; a long fight with no fresh entries would read
	// quiet. Any live enemy still in Combat awareness within relevance range keeps the signal
	// pinned to now (range-gated so a leftover far-away skirmish can't hold us in cower).
	if (FindNearestCombatEnemy(GetTuning()->CombatRelevanceRadius))
		LastCombatSignalTime = World->GetTimeSeconds();
}

bool AExtracteeCharacter::IsCombatActive(float MaxAge) const
{
	const UWorld* World = GetWorld();
	return World && (World->GetTimeSeconds() - LastCombatSignalTime) < MaxAge;
}

bool AExtracteeCharacter::IsPartyMemberDown() const
{
	// ANY companion down holds us, not just whichever one an iterator happened to grab first.
	for (const TWeakObjectPtr<ACompanionCharacter>& Entry : Companions)
	{
		const ACompanionCharacter* Comp = Entry.Get();
		if (IsValid(Comp) && Comp->GetIsCompanionDBNO()) return true;
	}
	if (const IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(PlayerPawn.Get()))
	{
		if (PlayerIface->GetIsDBNO()) return true;
	}
	return false;
}

AEnemyCharacter* AExtracteeCharacter::FindNearestCombatEnemy(float MaxRange) const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	AEnemyCharacter* Nearest = nullptr;
	float NearestDistSq = FMath::Square(MaxRange);
	const FVector MyLoc = GetActorLocation();
	const float ContactWindow = GetTuning()->CombatContactWindowSeconds;

	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		AEnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy)) continue;
		const UHealthComponent* Health = Enemy->GetHealthComponent();
		if (Health && Health->IsDead()) continue;
		// Combat awareness PLUS recent contact — a hidden enemy that idles out of contact is not a
		// fight, and counting it re-stamped the combat signal forever (never left cower).
		if (!Enemy->HasActiveCombatContact(ContactWindow)) continue;

		const float DistSq = FVector::DistSquared(MyLoc, Enemy->GetActorLocation());
		if (DistSq >= NearestDistSq) continue;
		NearestDistSq = DistSq;
		Nearest = Enemy;
	}
	return Nearest;
}

float AExtracteeCharacter::DistToPlayer2D() const
{
	const APawn* Player = PlayerPawn.Get();
	return Player ? FVector::Dist2D(GetActorLocation(), Player->GetActorLocation()) : 0.f;
}

bool AExtracteeCharacter::IsMoveActive() const
{
	const AExtracteeAIController* AIC = GetExtracteeController();
	return AIC && AIC->GetMoveStatus() == EPathFollowingStatus::Moving;
}

void AExtracteeCharacter::SetMovementFacing(bool bMoving)
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	AExtracteeAIController* AIC = GetExtracteeController();
	if (!MoveComp || !AIC) return;

	if (bMoving)
	{
		AIC->ClearFocus(EAIFocusPriority::Gameplay);
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->bUseControllerDesiredRotation = false;
		return;
	}

	MoveComp->bOrientRotationToMovement = false;
	MoveComp->bUseControllerDesiredRotation = true;
	if (AActor* Player = PlayerPawn.Get())
		AIC->SetFocus(Player, EAIFocusPriority::Gameplay);
}

void AExtracteeCharacter::ApplyMoveSpeed(bool bSprint)
{
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		const UExtracteeTuningDataAsset* T = GetTuning();
		MoveComp->MaxWalkSpeed = bSprint ? T->SprintSpeed : T->WalkSpeed;
	}
}

void AExtracteeCharacter::StartScaredLoop()
{
	if (!ScaredLoopMontage) return;

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance) return;
	if (AnimInstance->Montage_IsPlaying(ScaredLoopMontage)) return;

	AnimInstance->Montage_Play(ScaredLoopMontage);
}

void AExtracteeCharacter::StopScaredLoop()
{
	if (!ScaredLoopMontage) return;

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance) return;
	if (!AnimInstance->Montage_IsPlaying(ScaredLoopMontage)) return;

	AnimInstance->Montage_Stop(0.25f, ScaredLoopMontage);
}

void AExtracteeCharacter::ReleaseCover()
{
	if (AAICoverSlot* Slot = ClaimedSlot.Get())
		Slot->Release(this);
	ClaimedSlot = nullptr;
}

void AExtracteeCharacter::TeleportBehindPlayer()
{
	APawn* Player = PlayerPawn.Get();
	UWorld* World = GetWorld();
	if (!Player || !World) return;

	const UExtracteeTuningDataAsset* T = GetTuning();
	FVector Dest = Player->GetActorLocation() - Player->GetActorForwardVector() * T->FollowDistance;

	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Dest, Projected, FVector(200.f, 200.f, 500.f)))
			Dest = Projected.Location;
	}

	UE_LOG(LogExtractee, Log, TEXT("Extractee catch-up stuck -- teleporting behind player"));
	// TeleportTo fails on encroachment; force the drop rather than staying stuck (LevelObjectiveFlow pattern).
	if (!TeleportTo(Dest, GetActorRotation()))
		TeleportTo(Dest, GetActorRotation(), /*bIsATest=*/false, /*bNoCheck=*/true);

	BestCatchUpDist = DistToPlayer2D();
	LastCatchUpProgressTime = World->GetTimeSeconds();
}

// --- Soft collision (companion self-push pattern: nobody hard-blocks, overlap depth pushes us out) ---

void AExtracteeCharacter::TickSoftSeparation()
{
	if (ACharacter* Player = Cast<ACharacter>(PlayerPawn.Get()))
	{
		EnsureSoftCollisionIgnores(Player);
		SoftPushAwayFrom(Player);
	}
	// Every companion: EnsureSoftCollisionIgnores is what clears the mutual hard block, so missing
	// one leaves its capsule solid against ours -- the civilian/armed-VIP collision this fixes.
	for (const TWeakObjectPtr<ACompanionCharacter>& Entry : Companions)
	{
		ACharacter* Comp = Entry.Get();
		if (!IsValid(Comp)) continue;
		EnsureSoftCollisionIgnores(Comp);
		SoftPushAwayFrom(Comp);
	}
}

void AExtracteeCharacter::EnsureSoftCollisionIgnores(ACharacter* Other)
{
	// Idempotent per-tick assert (self-heals external clears, mirroring the player<->companion
	// wiring in AExtractionPlayer): our sweeps pass through them, theirs pass through us --
	// separation is entirely the soft push below.
	UCapsuleComponent* MyCapsule = GetCapsuleComponent();
	UCapsuleComponent* OtherCapsule = Other->GetCapsuleComponent();
	if (!MyCapsule || !OtherCapsule) return;

	MyCapsule->IgnoreActorWhenMoving(Other, true);
	OtherCapsule->IgnoreActorWhenMoving(this, true);
}

void AExtracteeCharacter::SoftPushAwayFrom(const ACharacter* Other)
{
	const UCapsuleComponent* OtherCapsule = Other->GetCapsuleComponent();
	if (!IsValid(OtherCapsule)) return;

	FVector Delta = GetActorLocation() - Other->GetActorLocation();
	Delta.Z = 0.f;

	const UExtracteeTuningDataAsset* T = GetTuning();
	const float CombinedRadius = GetCapsuleComponent()->GetScaledCapsuleRadius()
		+ OtherCapsule->GetScaledCapsuleRadius()
		+ T->PushPadding;

	const float Dist = Delta.Size();
	if (Dist >= CombinedRadius) return;

	FVector PushDir = (Dist > KINDA_SMALL_NUMBER) ? (Delta / Dist) : GetActorRightVector();
	PushDir.Z = 0.f;
	PushDir = PushDir.GetSafeNormal();

	const float DepthFraction = 1.f - (Dist / CombinedRadius);
	AddMovementInput(PushDir * (T->PushStrength * DepthFraction), 1.f);
}
