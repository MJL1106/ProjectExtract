// Player-owned component that handles camera-trace pinging and command confirmation.

#include "CompanionCommandComponent.h"
#include "AI/CompanionCoverStatics.h"
#include "Companion/CompanionBarkTypes.h"
#include "CoverSystemPublicData.h"
#include "CoverGeometryStatics.h"
#include "AI/BlackboardKeyType_Cover.h"
#include "World/Breachable.h"
#include "World/DoorBase.h"
#include "World/BreachableDoor.h"
#include "World/Lootable.h"
#include "World/WorldInteractable.h"
#include "Enemy/EnemyCharacter.h"
#include "Companion/CompanionCharacter.h"
#include "Components/HealthComponent.h"
#include "Audio/MusicSubsystem.h"
#include "Character/ExtractionPlayer.h"
#include "AI/CompanionAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogCompanionCommand);

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

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(CoverReissueTimerHandle);

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

	// Primary only -- the armed extractee is a companion too, but takes no player commands.
	ACompanionCharacter* Companion = ACompanionCharacter::GetPrimaryCompanion(GetWorld());
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

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionPing), false, Owner);
	Params.AddIgnoredActor(Owner);

	// Same ally blindness the player's interaction traces need, from the same single source of truth:
	// an ally's mesh blocks ECC_Visibility (that block is what makes it take hitscan damage), so the
	// primary standing in front of the player won the nearer-hit comparison below and the ping resolved
	// to the ally — which matches no command and reads as a dead ping. No command targets an ally, and
	// an ally that is itself interactable is deliberately still traceable, so nothing is lost here.
	if (const AExtractionPlayer* PlayerOwner = Cast<AExtractionPlayer>(Owner))
		Params.AddIgnoredActors(PlayerOwner->GetInteractTraceIgnoredActors());

	// Second pass on the dedicated Interact channel so a loot volume (which ignores Visibility so
	// it can't block AI sight or cover generation) can still be pinged for the companion to search.
	// Nearer hit wins.
	FHitResult VisibilityHit;
	const bool bHitVisibility = World->LineTraceSingleByChannel(VisibilityHit, TraceStart, TraceEnd, ECC_Visibility, Params);

	FHitResult InteractHit;
	const bool bHitInteract = World->LineTraceSingleByChannel(
		InteractHit, TraceStart, TraceEnd, ExtractionInteraction::InteractTraceChannel, Params);

	const bool bPreferInteract = bHitInteract && (!bHitVisibility || InteractHit.Distance <= VisibilityHit.Distance);
	const bool bHit = bHitVisibility || bHitInteract;
	const FHitResult Hit = bPreferInteract ? InteractHit : VisibilityHit;

	UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] trace hit=%d actor=%s class=%s"), bHit,
		*GetNameSafe(Hit.GetActor()), Hit.GetActor() ? *Hit.GetActor()->GetClass()->GetName() : TEXT("None"));

	if (!bHit || !IsValid(Hit.GetActor()))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] no hit -> clear"));
		PlayPingFeedback(false);
		ClearPending();
		return;
	}

	AActor* HitActor = Hit.GetActor();

	// Priority 1: doors. The dedicated breachable door class pings as BREACH; every other door
	// (the normal scripted/pack doors — their CanBreach is true so the auto-open flows work, but
	// they are not breach TARGETS) pings as SEARCH: the companion breach-enters the door
	// (montage + synced swing), then engages/loots the room or dwells briefly and returns to
	// follow. A dead door ping (locked/route) stays a dead ping.
	if (ADoorBase* HitDoor = Cast<ADoorBase>(HitActor))
	{
		const bool bBreachDoor = HitActor->IsA<ABreachableDoor>();

		// Route legs are scripted — never offer a command that would yank the companion off one
		// (and a stairwell ping can grab a neighbouring door's collision mid-descent anyway).
		if (IsCompanionRouteActive())
		{
			UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] door %s suppressed — route active"), *GetNameSafe(HitActor));
			PlayPingFeedback(false);
			ClearPending();
			return;
		}

		if (bBreachDoor && IBreachable::Execute_CanBreach(HitActor))
		{
			PendingCommand = ECompanionCommand::Breach;
			PendingTarget  = HitActor;
			CapturePingAnchor(HitActor, Hit.ImpactPoint);
			SetPromptContextRegistered(false); // breach prompt confirms on B — no G/V shield needed
			OnPingChanged.Broadcast(PendingCommand, HitActor);
			PlayPingFeedback(true);
			UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> BREACH %s (broadcast)"), *GetNameSafe(HitActor));
			return;
		}

		// Search needs a door the companion can actually pass: an externally gate-locked door or
		// one with AI auto-open disabled would leave it shoving the closed leaf to timeout. The
		// interior-point query is a reachability gate only — the search task derives its own
		// geometry from the door.
		if (!bBreachDoor && !HitDoor->IsExternalGateLocked() && HitDoor->CanAutoOpenForAI())
		{
			FVector ThroughPoint;
			const ACompanionCharacter* Companion = ResolveCompanion();
			if (IsValid(Companion)
				&& IBreachable::Execute_GetPostBreachPoint(HitDoor, Companion, /*bEnterRoom*/ true, ThroughPoint))
			{
				PendingCommand = ECompanionCommand::Explore;
				PendingTarget  = HitActor; // marker rides the door
				CapturePingAnchor(HitActor, Hit.ImpactPoint);
				SetPromptContextRegistered(false); // explore confirms on the breach key — no G/V shield needed
				OnPingChanged.Broadcast(PendingCommand, HitActor);
				PlayPingFeedback(true);
				UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> SEARCH door %s (broadcast)"), *GetNameSafe(HitActor));
				return;
			}
		}

		PlayPingFeedback(false);
		ClearPending();
		return;
	}

	// Priority 1b: non-door breachables (BP-only Breachable actors keep the old contract).
	if (HitActor->Implements<UBreachable>() && IBreachable::Execute_CanBreach(HitActor))
	{
		if (IsCompanionRouteActive())
		{
			UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] breachable %s suppressed — route active"), *GetNameSafe(HitActor));
			PlayPingFeedback(false);
			ClearPending();
			return;
		}
		PendingCommand = ECompanionCommand::Breach;
		PendingTarget  = HitActor;
		CapturePingAnchor(HitActor, Hit.ImpactPoint);
		SetPromptContextRegistered(false);
		OnPingChanged.Broadcast(PendingCommand, HitActor);
		PlayPingFeedback(true);
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> BREACH %s (broadcast)"), *GetNameSafe(HitActor));
		return;
	}

	// Priority 2: Lootable container
	if (HitActor->Implements<ULootable>() && ILootable::Execute_CanLoot(HitActor))
	{
		PendingCommand = ECompanionCommand::Loot;
		PendingTarget  = HitActor;
		CapturePingAnchor(HitActor, Hit.ImpactPoint);
		SetPromptContextRegistered(false); // loot confirms on the breach key — no G/V shield needed
		OnPingChanged.Broadcast(PendingCommand, HitActor);
		PlayPingFeedback(true);
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
			CapturePingAnchor(Enemy, Hit.ImpactPoint);
			SetPromptContextRegistered(true);
			OnPingChanged.Broadcast(PendingCommand, Enemy);
			PlayPingFeedback(true);
			UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> TAKEDOWN %s (broadcast)"), *GetNameSafe(Enemy));
			return;
		}
		// An ineligible enemy stays a dead ping — never downgrade a hostile under the crosshair
		// to "search this spot".
		PlayPingFeedback(false);
		ClearPending();
		return;
	}

	// Priority 4: Cover — wall/crate with legal positions for the companion.
	// Route test runs before the octree query so a scripted-route ping does not pay the search cost.
	if (!IsCompanionRouteActive())
	{
		ACompanionCharacter* Companion = ResolveCompanion();
		ACompanionAIController* CoverController = GetCompanionController();
		if (IsValid(Companion) && IsValid(CoverController))
		{
			AActor* CombatTarget = nullptr;
			if (const UBlackboardComponent* BB = CoverController->GetBlackboardComponent())
				CombatTarget = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget));

			FCover FoundCover;
			if (CompanionCover::FindCoverOnPingedWall(World, Hit.ImpactPoint, Hit.ImpactNormal,
				CoverPingRadius, CoverController, Companion, CombatTarget, FoundCover))
			{
				PendingCommand = ECompanionCommand::TakeCover;
				PendingTarget.Reset(); // location-only command, no target actor
				PendingPingOffset = FVector::ZeroVector; // stale offset from a prior ping -- not used by TakeCover, but don't leave it dangling
				bPendingPingOffsetValid = false;
				PendingCoverPingImpact = Hit.ImpactPoint;
				PendingCoverPingNormal = Hit.ImpactNormal;
				PendingCoverLocation = FoundCover.Data.Location;
				SetPromptContextRegistered(false);
				OnPingChanged.Broadcast(PendingCommand, nullptr);
				PlayPingFeedback(true);
				UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] -> TAKECOVER at %s (broadcast)"), *FoundCover.Data.Location.ToString());
				return;
			}
		}
	}

	// No open-ground fallback: searches are door-targeted — pinging ground offers nothing.
	UE_LOG(LogCompanionCommand, Warning, TEXT("[Ping] hit %s matched no command -> clear"), *GetNameSafe(HitActor));
	PlayPingFeedback(false);
	ClearPending();
}

void UCompanionCommandComponent::PlayPingFeedback(bool bAccepted) const
{
	USoundBase* Sound = bAccepted ? PingConfirmSound : PingFailSound;
	if (IsValid(Sound))
		UGameplayStatics::PlaySound2D(GetWorld(), Sound);
}

// ---- Confirm helpers ----

void UCompanionCommandComponent::ClearPending()
{
	PendingCommand = ECompanionCommand::None;
	PendingTarget.Reset();
	PendingPingOffset = FVector::ZeroVector;
	bPendingPingOffsetValid = false;
	SetPromptContextRegistered(false);
	OnPingChanged.Broadcast(ECompanionCommand::None, nullptr);
}

void UCompanionCommandComponent::CapturePingAnchor(AActor* TargetActor, const FVector& ImpactPoint)
{
	PendingPingOffset = IsValid(TargetActor)
		? TargetActor->GetActorTransform().InverseTransformPosition(ImpactPoint)
		: ImpactPoint;
	bPendingPingOffsetValid = true;
}

bool UCompanionCommandComponent::GetPingWorldLocation(FVector& OutLocation) const
{
	if (PendingCommand == ECompanionCommand::TakeCover)
	{
		OutLocation = PendingCoverLocation;
		return true;
	}
	if (PendingCommand == ECompanionCommand::None) return false;

	AActor* Target = PendingTarget.Get();
	if (IsValid(Target) && bPendingPingOffsetValid)
	{
		OutLocation = Target->GetActorTransform().TransformPosition(PendingPingOffset);
		return true;
	}
	if (IsValid(Target))
	{
		OutLocation = Target->GetActorLocation();
		return true;
	}

	// No live target: PendingPingOffset (when valid) is always actor-LOCAL -- every
	// CapturePingAnchor call site passes a validated actor, so there is no bare-world-point
	// case to fall back to. A target that dies mid-ping (looted pickup, killed enemy) must
	// collapse the marker, not resolve a local offset as if it were a world position.
	return false;
}

void UCompanionCommandComponent::ClearCommandedCoverIfActive()
{
	ACompanionCharacter* Companion = ResolveCompanion();
	if (IsValid(Companion) && Companion->IsCommandedCoverHoldActive())
		Companion->ClearCommandedCoverHold();

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(CoverReissueTimerHandle);
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

	ClearCommandedCoverIfActive();
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

	ClearCommandedCoverIfActive();
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

	ClearCommandedCoverIfActive();
	Controller->IssueCommand(ECompanionCommand::Loot, ETakedownMethod::Knife, Target, Target->GetActorLocation());
	ClearPending();
}

void UCompanionCommandComponent::ConfirmExplore()
{
	if (PendingCommand != ECompanionCommand::Explore) return;
	AActor* Target = PendingTarget.Get();
	if (!IsValid(Target)) { ClearPending(); return; }

	// Backstop for a prompt raised just before a route started — the ping-time gate can't catch it.
	if (IsCompanionRouteActive())
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] search of %s rejected — route active"), *GetNameSafe(Target));
		ClearPending();
		return;
	}

	ACompanionAIController* Controller = GetCompanionController();
	if (!IsValid(Controller))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("ConfirmExplore: companion controller not found"));
		return;
	}

	// Breach type follows the companion mode (same mapping as ConfirmBreach); the search task
	// re-derives it from the live mode when the montage starts — this BB write is its fallback.
	EBreachType BreachType = EBreachType::Tactical;
	switch (GetCompanionMode())
	{
	case ECompanionMode::Combat:  BreachType = EBreachType::Loud;  break;
	case ECompanionMode::Stealth: BreachType = EBreachType::Quiet; break;
	default:                      BreachType = EBreachType::Tactical; break;
	}
	Controller->SetBreachType(BreachType);

	ClearCommandedCoverIfActive();
	Controller->IssueCommand(ECompanionCommand::Explore, ETakedownMethod::Knife, Target, Target->GetActorLocation());
	ClearPending();
}

void UCompanionCommandComponent::ConfirmTakeCover()
{
	if (PendingCommand != ECompanionCommand::TakeCover) return;

	// Route suppression backstop (same as ConfirmBreach / ConfirmExplore).
	if (IsCompanionRouteActive())
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] TakeCover rejected — route active"));
		ClearPending();
		return;
	}

	ACompanionCharacter* Companion = ResolveCompanion();
	ACompanionAIController* Controller = GetCompanionController();
	if (!IsValid(Companion) || !IsValid(Controller))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("ConfirmTakeCover: companion controller not found"));
		ClearPending();
		return;
	}

	// Re-resolve the cover at confirm time (state may have changed since ping).
	AActor* CombatTarget = nullptr;
	if (const UBlackboardComponent* BB = Controller->GetBlackboardComponent())
		CombatTarget = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget));

	FCover ResolvedCover;
	if (!CompanionCover::FindCoverOnPingedWall(GetWorld(), PendingCoverPingImpact, PendingCoverPingNormal,
		CoverPingRadius, Controller, Companion, CombatTarget, ResolvedCover))
	{
		UE_LOG(LogCompanionCommand, Warning, TEXT("[Confirm] TakeCover re-resolve failed — no legal cover left"));
		PlayPingFeedback(false);
		ClearPending();
		return;
	}

	// Re-ping while already holding: the clear and the re-issue must be in SEPARATE FRAMES.
	// Frame N: key -> None (abort processes, ClearCommandIfStillActive sees None, does not wipe).
	// Frame N+1: None -> TakeCover (genuine false->true decorator edge on a settled tree).
	// SetTimerForNextTick returns an FTimerHandle in UE5, satisfying the timer-cleanup rule.
	if (Companion->IsCommandedCoverHoldActive())
	{
		Companion->ClearCommandedCoverHold();
		Controller->ClearActiveCommand(); // frame N: key -> None

		// Cancel any pending deferral from a rapid double-confirm.
		if (UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(CoverReissueTimerHandle);

		TWeakObjectPtr<ACompanionCharacter> WeakComp = Companion;
		TWeakObjectPtr<ACompanionAIController> WeakCtrl = Controller;
		const FCover Pending = ResolvedCover;
		if (UWorld* World = GetWorld())
		{
			CoverReissueTimerHandle = World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateLambda([WeakComp, WeakCtrl, Pending]()
				{
					if (!WeakComp.IsValid() || !WeakCtrl.IsValid()) return;
					WeakComp->SetCommandedCoverTarget(Pending);
					WeakCtrl->IssueCommand(ECompanionCommand::TakeCover, ETakedownMethod::Knife,
						nullptr, Pending.Data.Location);
				}));
		}
		ClearPending();
		return;
	}

	// First-time order: straight through, no deferral.
	Companion->SetCommandedCoverTarget(ResolvedCover);

	// IssueCommand barks TakingCover internally -- no duplicate bark here.
	Controller->IssueCommand(ECompanionCommand::TakeCover, ETakedownMethod::Knife, nullptr, ResolvedCover.Data.Location);

	ClearPending();
}

bool UCompanionCommandComponent::IsCoverMeAvailable()
{
	return EvaluateCoverMe() == ECoverMeGate::Ready;
}

UCompanionCommandComponent::ECoverMeGate UCompanionCommandComponent::EvaluateCoverMe(
	AActor** OutCombatTarget, ACompanionCharacter** OutCompanion)
{
	ACompanionCharacter* Companion = ResolveCompanion();
	if (!IsValid(Companion)) return ECoverMeGate::NoCompanion;
	if (Companion->GetIsCompanionDBNO()) return ECoverMeGate::NoCompanion;
	const UHealthComponent* HC = Companion->GetHealthComponent();
	if (IsValid(HC) && HC->IsDead()) return ECoverMeGate::NoCompanion;

	if (Companion->IsCoveringFireActive() || Companion->IsCoveringFirePending()) return ECoverMeGate::AlreadyActive;
	if (Companion->IsCoveringFireOnCooldown()) return ECoverMeGate::OnCooldown;

	ACompanionAIController* Controller = GetCompanionController();
	if (!IsValid(Controller)) return ECoverMeGate::NoCompanion;
	const UBlackboardComponent* BB = Controller->GetBlackboardComponent();
	if (!BB) return ECoverMeGate::NoCompanion;

	// Companion passed all validity gates — write it out so callers skip re-resolve.
	if (OutCompanion) *OutCompanion = Companion;

	// Populate the live blackboard target when there is one — TriggerCoverMe uses it to decide
	// whether to grant a cover commit. Its absence is NOT a reason to refuse.
	AActor* BBTarget = Cast<AActor>(BB->GetValueAsObject(ACompanionAIController::BB_CombatTarget));
	if (IsValid(BBTarget) && OutCombatTarget) *OutCombatTarget = BBTarget;

	// No combat gate. Every "is a fight happening" signal tried here was a proxy that goes false
	// during the exact moments the player wants this: the companion's blackboard target needs
	// current sight perception, player-damage recency needs to be actively taking rounds, and the
	// music state needs shots fired inside a window — so a lull in cover silently revoked the
	// command and the row vanished mid-fight. Availability is now purely mechanical: alive, not
	// already covering, off cooldown. Spending it with no enemies around is the player's call.
	return ECoverMeGate::Ready;
}

bool UCompanionCommandComponent::CanPeekShootTarget(ACompanionCharacter* Companion, AActor* Target)
{
	ACompanionAIController* Controller = GetCompanionController();
	if (!IsValid(Controller)) return false;
	const UBlackboardComponent* BB = Controller->GetBlackboardComponent();
	if (!BB) return false;
	const FBlackboard::FKey CoverKeyID = BB->GetKeyID(TEXT("CoverTarget"));
	if (CoverKeyID == FBlackboard::InvalidKey) return false;
	const FCover CurrentCover = BB->GetValue<UBlackboardKeyType_Cover>(CoverKeyID);
	if (!CurrentCover.IsValid()) return false;
	const bool bCrouched = UCoverGeometryStatics::GetCoverHeight(CurrentCover.Data) == ECoverHeight::Crouch;
	return UCoverGeometryStatics::CanPeekShoot(
		Companion->GetWorld(), CurrentCover.Data, bCrouched,
		Target->GetActorLocation(), 140.f, Target, Companion);
}

void UCompanionCommandComponent::TriggerCoverMe()
{
	// Silent no-op if the picker already auto-closed. Logged because it is indistinguishable
	// from a broken command in play: the press makes no sound and nothing moves.
	if (!bModeMenuOpen)
	{
		UE_LOG(LogCompanionCommand, Log, TEXT("[CoverMe] ignored — picker not open (timed out?)"));
		return;
	}

	AActor* CombatTarget = nullptr;
	ACompanionCharacter* Companion = nullptr;
	const ECoverMeGate Gate = EvaluateCoverMe(&CombatTarget, &Companion);
	if (Gate != ECoverMeGate::Ready)
	{
		PlayPingFeedback(false);
		CloseModeMenu();
		const TCHAR* Reason =
			Gate == ECoverMeGate::NoCompanion  ? TEXT("no companion")  :
			Gate == ECoverMeGate::NoCombat     ? TEXT("no combat")     :
			Gate == ECoverMeGate::AlreadyActive? TEXT("already active"):
			Gate == ECoverMeGate::OnCooldown   ? TEXT("on cooldown")   : TEXT("unknown");
		UE_LOG(LogCompanionCommand, Log, TEXT("[CoverMe] rejected — %s"), Reason);
		return;
	}

	// Close the menu WITHOUT writing ECompanionMode — this differs from SelectCompanionMode.
	CloseModeMenu();
	if (!IsValid(Companion)) return; // guard: CloseModeMenu broadcasts, weak ref may go stale

	// The window ALWAYS starts on the press — bark and countdown are immediate, never deferred to
	// cover arrival. Waiting for the cover machinery meant the common mid-fight case (live target,
	// no shot from the current spot) gave no feedback at all and rejected every re-press as
	// "already active" until a fallback fired seconds later. The clock now runs while he moves.
	Companion->ArmCoveringFire(CoveringFireDuration);
	Companion->StartCoveringFire();
	Companion->Bark(ECompanionBarkType::Suppressing);

	// Live target he cannot shoot from here: grant commit so the cover machinery still repositions
	// him — now during the already-running window instead of before it.
	if (IsValid(CombatTarget) && !CanPeekShootTarget(Companion, CombatTarget))
		Companion->SetCoverCommitGrant(true);

	UE_LOG(LogCompanionCommand, Log, TEXT("[CoverMe] started (%.1fs), hasTarget=%d"),
		CoveringFireDuration, IsValid(CombatTarget));
}
