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
#include "EngineUtils.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogExtractee);

namespace
{
	// State machine cadence -- reaction logic, not movement, so slow is fine.
	constexpr float StateEvalInterval = 0.25f;
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
	const float Dist = DistToPlayer2D();

	if (Dist > T->CatchUpStartDistance) { EnterCatchUp(); return; }
	if (IsCombatActive(T->CombatActiveWindow)) { EnterSeekCoverOrCower(); return; }

	AExtracteeAIController* AIC = GetExtracteeController();
	if (!AIC) return;

	if (Dist > T->FollowDistance + T->FollowResumeSlack)
	{
		if (!IsMoveActive())
		{
			SetMovementFacing(/*bMoving=*/true);
			AIC->MoveToActor(PlayerPawn.Get(), T->FollowDistance, /*bStopOnOverlap=*/true,
				/*bUsePathfinding=*/true, /*bCanStrafe=*/false);
		}
		return;
	}

	if (Dist <= T->FollowDistance && IsMoveActive())
	{
		AIC->StopMovement();
		SetMovementFacing(/*bMoving=*/false);
	}
	else if (!IsMoveActive())
	{
		SetMovementFacing(/*bMoving=*/false);
	}
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

	if (!Companion.IsValid())
		Companion = ACompanionCharacter::GetPrimaryCompanion(GetWorld());
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
	if (const ACompanionCharacter* Comp = Companion.Get())
	{
		if (Comp->GetIsCompanionDBNO()) return true;
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

	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		AEnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy)) continue;
		const UHealthComponent* Health = Enemy->GetHealthComponent();
		if (Health && Health->IsDead()) continue;
		if (!Enemy->HasDetectedPlayer()) continue;	// Combat awareness state only

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
	if (ACharacter* Comp = Companion.Get())
	{
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
