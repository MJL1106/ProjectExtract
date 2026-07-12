// Player-owned component that handles camera-trace pinging and command confirmation.

#include "CompanionCommandComponent.h"
#include "World/Breachable.h"
#include "World/DoorBase.h"
#include "World/BreachableDoor.h"
#include "World/Lootable.h"
#include "EngineUtils.h"
#include "Enemy/EnemyCharacter.h"
#include "Companion/CompanionCharacter.h"
#include "AI/CompanionAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogCompanionCommand);

// A still-breachable door near the pinged ground point means that spot is breach territory —
// offering "search" beside the door would let the player bypass the breach interaction.
static bool AnyBreachableDoorWithin(UWorld* World, const FVector& Center, float Radius)
{
	if (!World || Radius <= 0.f) return false;
	const float RadiusSq = FMath::Square(Radius);
	for (TActorIterator<ABreachableDoor> It(World); It; ++It)
	{
		ABreachableDoor* Door = *It;
		if (!IsValid(Door) || !IBreachable::Execute_CanBreach(Door)) continue;
		// Portal point, not actor location — the actor origin sits at the hinge on swing doors.
		if (FVector::DistSquared(Door->GetAcousticPortalPoint(), Center) <= RadiusSq) return true;
	}
	return false;
}

UCompanionCommandComponent::UCompanionCommandComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCompanionCommandComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UCompanionCommandComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseModeMenu(); // clears the auto-close timer + unregisters ModeSelectContext
	SetPromptContextRegistered(false);

	if (ACompanionCharacter* Companion = CachedCompanion.Get())
		Companion->OnModeChanged.RemoveDynamic(this, &UCompanionCommandComponent::HandleCompanionModeChanged);

	CachedCompanion.Reset();
	CachedCamera.Reset();
	PendingTarget.Reset();
	Super::EndPlay(EndPlayReason);
}

// ---- Companion lookup ----

ACompanionCharacter* UCompanionCommandComponent::ResolveCompanion()
{
	if (CachedCompanion.IsValid()) return CachedCompanion.Get();

	AActor* Found = UGameplayStatics::GetActorOfClass(GetWorld(), ACompanionCharacter::StaticClass());
	ACompanionCharacter* Companion = Cast<ACompanionCharacter>(Found);
	if (IsValid(Companion))
	{
		CachedCompanion = Companion;
		if (!Companion->OnModeChanged.IsAlreadyBound(this, &UCompanionCommandComponent::HandleCompanionModeChanged))
			Companion->OnModeChanged.AddDynamic(this, &UCompanionCommandComponent::HandleCompanionModeChanged);
	}
	return Companion;
}

ACompanionAIController* UCompanionCommandComponent::GetCompanionController()
{
	ACompanionCharacter* Companion = ResolveCompanion();
	if (!IsValid(Companion)) return nullptr;
	return Cast<ACompanionAIController>(Companion->GetController());
}

bool UCompanionCommandComponent::IsCompanionRouteActive()
{
	const ACompanionAIController* Controller = GetCompanionController();
	const UBlackboardComponent* BB = IsValid(Controller) ? Controller->GetBlackboardComponent() : nullptr;
	if (!BB || !BB->GetValueAsBool(ACompanionAIController::BB_RouteActive)) return false;

	// A HoldAtFinal park keeps BB_RouteActive true indefinitely — the walk is done, so commands
	// (breach at the objective door) must work again.
	const ACompanionCharacter* Companion = ResolveCompanion();
	return !(IsValid(Companion) && Companion->IsRouteHoldingAtFinal());
}

// ---- Ping ----

void UCompanionCommandComponent::IssuePing()
{
	UWorld* World = GetWorld();
	if (!World) return;

	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!IsValid(Owner)) return;

	UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] IssuePing: owner=%s range=%.0f"), *GetNameSafe(Owner), PingTraceRange);

	// Lazily cache the camera component — avoids FindComponentByClass every ping press.
	if (!CachedCamera.IsValid())
		CachedCamera = Owner->FindComponentByClass<UCameraComponent>();
	UCameraComponent* Cam = CachedCamera.Get();

	FVector EyesLoc; FRotator EyesRot;
	Owner->GetActorEyesViewPoint(EyesLoc, EyesRot);
	const FVector TraceStart = Cam ? Cam->GetComponentLocation() : EyesLoc;
	const FVector TraceDir   = Cam ? Cam->GetForwardVector() : EyesRot.Vector();
	const FVector TraceEnd   = TraceStart + TraceDir * PingTraceRange;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionPing), false, Owner);
	Params.AddIgnoredActor(Owner);
	const bool bHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);
	UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] trace hit=%d actor=%s class=%s"), bHit,
		*GetNameSafe(Hit.GetActor()), Hit.GetActor() ? *Hit.GetActor()->GetClass()->GetName() : TEXT("None"));

	if (!bHit || !IsValid(Hit.GetActor()))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] no hit -> clear"));
		ClearPending();
		return;
	}

	AActor* HitActor = Hit.GetActor();

	// Priority 1: doors. The dedicated breachable door class pings as BREACH; every other door
	// (the normal scripted/pack doors — their CanBreach is true so the auto-open flows work, but
	// they are not breach TARGETS) pings as SEARCH-through: the companion walks through the
	// auto-opening door to the far side and explores there. A door hit never falls through to the
	// ground-search fallback — a dead door ping (locked/route) stays a dead ping.
	if (ADoorBase* HitDoor = Cast<ADoorBase>(HitActor))
	{
		const bool bBreachDoor = HitActor->IsA<ABreachableDoor>();

		// Route legs are scripted — never offer a command that would yank the companion off one
		// (and a stairwell ping can grab a neighbouring door's collision mid-descent anyway).
		if (IsCompanionRouteActive())
		{
			UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] door %s suppressed — route active"), *GetNameSafe(HitActor));
			ClearPending();
			return;
		}

		if (bBreachDoor && IBreachable::Execute_CanBreach(HitActor))
		{
			PendingCommand = ECompanionCommand::Breach;
			PendingTarget  = HitActor;
			SetPromptContextRegistered(false); // breach prompt confirms on B — no G/V shield needed
			OnPingChanged.Broadcast(PendingCommand, HitActor);
			UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> BREACH %s (broadcast)"), *GetNameSafe(HitActor));
			return;
		}

		// Search-through needs a door the companion can actually pass: an externally gate-locked
		// door or one with AI auto-open disabled would leave it shoving the closed leaf to timeout.
		if (!bBreachDoor && !HitDoor->IsExternalGateLocked() && HitDoor->CanAutoOpenForAI())
		{
			FVector ThroughPoint;
			const ACompanionCharacter* Companion = ResolveCompanion();
			if (IsValid(Companion)
				&& IBreachable::Execute_GetPostBreachPoint(HitDoor, Companion, /*bEnterRoom*/ true, ThroughPoint))
			{
				PendingCommand  = ECompanionCommand::Explore;
				PendingTarget   = HitActor; // marker rides the door
				PendingLocation = ThroughPoint;
				SetPromptContextRegistered(false); // explore confirms on the breach key — no G/V shield needed
				OnPingChanged.Broadcast(PendingCommand, HitActor);
				UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> EXPLORE through door %s at %s (broadcast)"),
					*GetNameSafe(HitActor), *PendingLocation.ToCompactString());
				return;
			}
		}

		ClearPending();
		return;
	}

	// Priority 1b: non-door breachables (BP-only Breachable actors keep the old contract).
	if (HitActor->Implements<UBreachable>() && IBreachable::Execute_CanBreach(HitActor))
	{
		if (IsCompanionRouteActive())
		{
			UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] breachable %s suppressed — route active"), *GetNameSafe(HitActor));
			ClearPending();
			return;
		}
		PendingCommand = ECompanionCommand::Breach;
		PendingTarget  = HitActor;
		SetPromptContextRegistered(false);
		OnPingChanged.Broadcast(PendingCommand, HitActor);
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> BREACH %s (broadcast)"), *GetNameSafe(HitActor));
		return;
	}

	// Priority 2: Lootable container
	if (HitActor->Implements<ULootable>() && ILootable::Execute_CanLoot(HitActor))
	{
		PendingCommand = ECompanionCommand::Loot;
		PendingTarget  = HitActor;
		SetPromptContextRegistered(false); // loot confirms on the breach key — no G/V shield needed
		OnPingChanged.Broadcast(PendingCommand, HitActor);
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> LOOT %s (broadcast)"), *GetNameSafe(HitActor));
		return;
	}

	// Priority 3: Takedown-eligible enemy
	AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(HitActor);
	if (IsValid(Enemy))
	{
		const bool bElig = Enemy->IsTakedownEligible();
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] hit enemy %s eligible=%d"), *GetNameSafe(Enemy), bElig);
		if (bElig)
		{
			PendingCommand = ECompanionCommand::Takedown;
			PendingTarget  = Enemy;
			SetPromptContextRegistered(true);
			OnPingChanged.Broadcast(PendingCommand, Enemy);
			UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> TAKEDOWN %s (broadcast)"), *GetNameSafe(Enemy));
			return;
		}
		// An ineligible enemy stays a dead ping — never downgrade a hostile under the crosshair
		// to "search this spot".
		ClearPending();
		return;
	}

	// Priority 4: open ground — explore/search the pinged spot. Only offered where the companion
	// can actually stand (navmesh projection; a wall hit projects to the floor at its base), and
	// never while a scripted route runs (same contract as breach — commands don't yank route legs).
	if (!IsCompanionRouteActive())
	{
		FNavLocation NavLoc;
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (NavSys && NavSys->ProjectPointToNavigation(Hit.ImpactPoint, NavLoc, FVector(300.f, 300.f, 500.f))
			&& !AnyBreachableDoorWithin(World, NavLoc.Location, ExploreSuppressNearBreachRadius))
		{
			PendingCommand  = ECompanionCommand::Explore;
			PendingTarget.Reset();
			PendingLocation = NavLoc.Location;
			SetPromptContextRegistered(false); // explore confirms on the breach key — no G/V shield needed
			OnPingChanged.Broadcast(PendingCommand, nullptr);
			UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> EXPLORE %s (broadcast)"), *PendingLocation.ToCompactString());
			return;
		}
	}

	UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] hit %s matched no command -> clear"), *GetNameSafe(HitActor));
	ClearPending();
}

// ---- Confirm helpers ----

void UCompanionCommandComponent::ClearPending()
{
	PendingCommand = ECompanionCommand::None;
	PendingTarget.Reset();
	SetPromptContextRegistered(false);
	OnPingChanged.Broadcast(ECompanionCommand::None, nullptr);
}

void UCompanionCommandComponent::SetPromptContextRegistered(bool bRegister)
{
	if (bRegister == bPromptContextRegistered) return;
	if (!TakedownPromptContext) return;

	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = PC
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())
		: nullptr;
	if (!Subsystem) return;

	if (bRegister)
		Subsystem->AddMappingContext(TakedownPromptContext, TakedownPromptContextPriority);
	else
		Subsystem->RemoveMappingContext(TakedownPromptContext);
	bPromptContextRegistered = bRegister;
}

void UCompanionCommandComponent::ConfirmTakedown(ETakedownMethod Method)
{
	UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] ConfirmTakedown method=%d pending=%d target=%s"),
		(int32)Method, (int32)PendingCommand, *GetNameSafe(PendingTarget.Get()));
	if (PendingCommand != ECompanionCommand::Takedown) { UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] no takedown pending -> ignore")); return; }
	AActor* Target = PendingTarget.Get();
	if (!IsValid(Target)) { UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] target invalid -> clear")); ClearPending(); return; }

	ACompanionAIController* Controller = GetCompanionController();
	if (!IsValid(Controller))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] companion controller NOT FOUND (no companion in level?)"));
		return;
	}

	UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] -> IssueCommand Takedown on %s via %s"), *GetNameSafe(Target), *GetNameSafe(Controller));
	Controller->IssueCommand(ECompanionCommand::Takedown, Method, Target, Target->GetActorLocation());
	ClearPending();
}

void UCompanionCommandComponent::ConfirmTakedownKnife()
{
	ConfirmTakedown(ETakedownMethod::Knife);
}

void UCompanionCommandComponent::ConfirmTakedownShoot()
{
	ConfirmTakedown(ETakedownMethod::Shoot);
}

// ---- Mode ----

void UCompanionCommandComponent::CycleCompanionMode()
{
	ACompanionCharacter* Companion = ResolveCompanion();
	if (!IsValid(Companion))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("CycleCompanionMode: no companion in level"));
		return;
	}

	ECompanionMode NextMode;
	switch (Companion->GetMode())
	{
	case ECompanionMode::Normal:  NextMode = ECompanionMode::Combat;  break;
	case ECompanionMode::Combat:  NextMode = ECompanionMode::Stealth; break;
	default:                      NextMode = ECompanionMode::Normal;  break;
	}

	UE_LOG(LogCompanionCommand, Log, TEXT("[Mode] cycle -> %s"), *UEnum::GetValueAsString(NextMode));
	Companion->SetMode(NextMode);
}

void UCompanionCommandComponent::ToggleModeMenu()
{
	if (bModeMenuOpen)
		CloseModeMenu();
	else
		OpenModeMenu();
}

void UCompanionCommandComponent::OpenModeMenu()
{
	if (bModeMenuOpen) return;

	if (!IsValid(ResolveCompanion()))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Mode] OpenModeMenu: no companion in level"));
		return;
	}

	if (!ModeSelectContext)
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Mode] OpenModeMenu: ModeSelectContext unassigned — picker opens but 1/2/3 won't select (assign IMC_CompanionModeSelect in BP)"));

	bModeMenuOpen = true;
	SetModeSelectContextRegistered(true);

	if (ModeMenuTimeout > 0.f)
	{
		if (UWorld* World = GetWorld())
			World->GetTimerManager().SetTimer(ModeMenuTimeoutHandle, this,
				&UCompanionCommandComponent::OnModeMenuTimeout, ModeMenuTimeout, false);
	}

	OnModeMenuChanged.Broadcast(true);
	UE_LOG(LogCompanionCommand, Log, TEXT("[Mode] picker open"));
}

void UCompanionCommandComponent::CloseModeMenu()
{
	// Always tear down the timer + context so EndPlay can call this unconditionally.
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ModeMenuTimeoutHandle);
	SetModeSelectContextRegistered(false);

	if (!bModeMenuOpen) return;
	bModeMenuOpen = false;
	OnModeMenuChanged.Broadcast(false);
	UE_LOG(LogCompanionCommand, Log, TEXT("[Mode] picker close"));
}

void UCompanionCommandComponent::OnModeMenuTimeout()
{
	CloseModeMenu();
}

void UCompanionCommandComponent::SelectCompanionMode(ECompanionMode Mode)
{
	if (!bModeMenuOpen) return; // number keys only act while the picker is open

	if (ACompanionCharacter* Companion = ResolveCompanion())
	{
		UE_LOG(LogCompanionCommand, Log, TEXT("[Mode] select -> %s"), *UEnum::GetValueAsString(Mode));
		Companion->SetMode(Mode);
	}

	CloseModeMenu();
}

void UCompanionCommandComponent::SetModeSelectContextRegistered(bool bRegister)
{
	if (bRegister == bModeSelectContextRegistered) return;
	if (!ModeSelectContext) return;

	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = PC
		? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())
		: nullptr;
	if (!Subsystem) return;

	if (bRegister)
		Subsystem->AddMappingContext(ModeSelectContext, ModeSelectContextPriority);
	else
		Subsystem->RemoveMappingContext(ModeSelectContext);
	bModeSelectContextRegistered = bRegister;
}

ECompanionMode UCompanionCommandComponent::GetCompanionMode()
{
	const ACompanionCharacter* Companion = ResolveCompanion();
	return IsValid(Companion) ? Companion->GetMode() : ECompanionMode::Normal;
}

void UCompanionCommandComponent::HandleCompanionModeChanged(ECompanionMode NewMode)
{
	OnCompanionModeChanged.Broadcast(NewMode);
}

void UCompanionCommandComponent::ConfirmBreach()
{
	if (PendingCommand != ECompanionCommand::Breach) return;
	AActor* Target = PendingTarget.Get();
	if (!IsValid(Target)) { ClearPending(); return; }

	// Backstop for a prompt raised just before a route started — the ping-time gate can't catch it.
	if (IsCompanionRouteActive())
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] breach on %s rejected — route active"), *GetNameSafe(Target));
		ClearPending();
		return;
	}

	ACompanionAIController* Controller = GetCompanionController();
	if (!IsValid(Controller))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("ConfirmBreach: companion controller not found"));
		return;
	}

	// Breach type follows the companion mode: Combat -> Loud, Stealth -> Quiet, Normal -> Tactical.
	// BTTask_CompanionBreach re-derives this from the live mode when the montage starts (a mode
	// switch while the companion walks over wins); this confirm-time BB write is its fallback.
	EBreachType BreachType = EBreachType::Tactical;
	switch (GetCompanionMode())
	{
	case ECompanionMode::Combat:  BreachType = EBreachType::Loud;  break;
	case ECompanionMode::Stealth: BreachType = EBreachType::Quiet; break;
	default:                      BreachType = EBreachType::Tactical; break;
	}
	Controller->SetBreachType(BreachType);

	Controller->IssueCommand(ECompanionCommand::Breach, ETakedownMethod::Knife, Target, Target->GetActorLocation());
	ClearPending();
}

void UCompanionCommandComponent::ConfirmLoot()
{
	if (PendingCommand != ECompanionCommand::Loot) return;
	AActor* Target = PendingTarget.Get();
	if (!IsValid(Target)) { ClearPending(); return; }

	ACompanionAIController* Controller = GetCompanionController();
	if (!IsValid(Controller))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("ConfirmLoot: companion controller not found"));
		return;
	}

	Controller->IssueCommand(ECompanionCommand::Loot, ETakedownMethod::Knife, Target, Target->GetActorLocation());
	ClearPending();
}

void UCompanionCommandComponent::ConfirmExplore()
{
	if (PendingCommand != ECompanionCommand::Explore) return;

	// Backstop for a prompt raised just before a route started — the ping-time gate can't catch it.
	if (IsCompanionRouteActive())
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] explore at %s rejected — route active"), *PendingLocation.ToCompactString());
		ClearPending();
		return;
	}

	ACompanionAIController* Controller = GetCompanionController();
	if (!IsValid(Controller))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("ConfirmExplore: companion controller not found"));
		return;
	}

	Controller->IssueCommand(ECompanionCommand::Explore, ETakedownMethod::Knife, nullptr, PendingLocation);
	ClearPending();
}
