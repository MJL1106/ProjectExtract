// UExtractionHudBridgeComponent -- gameplay -> HUD delegate plumbing. See the header for the
// two timing facts (late pawn/companion, edge-only channels) that shape this file.

#include "UI/ExtractionHudBridgeComponent.h"

#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

#include "Character/ExtractionPlayerInterface.h"
#include "Companion/CompanionCharacter.h"
#include "Components/CompanionCommandComponent.h"
#include "Components/ConsumableInventoryComponent.h"
#include "Components/HealthComponent.h"
#include "Components/WeaponComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Weapon/WeaponBase.h"

DEFINE_LOG_CATEGORY(LogHudBridge);

namespace
{
	/** Unreal units per metre, and the epsilon below which the rendered line cannot change. Half a
	 *  metre, not a few centimetres: the readout itself rounds to whole metres, so anything finer
	 *  just pushes every objective on every tick while the player walks -- pure BP-call spam with no
	 *  visible effect.
	 *
	 *  HudBridge-prefixed because ScreenMarkerProjection.cpp and ObjectiveMarkerWidget.cpp declare
	 *  these same two names in their own anonymous namespaces: under a unity build every
	 *  Private/UI/*.cpp shares one translation unit, so those anonymous namespaces are the SAME
	 *  namespace and unprefixed names are redefinitions. */
	constexpr float HudBridgeUnrealUnitsPerMetre = 100.f;
	constexpr float HudBridgeDistanceBroadcastEpsilon = 0.5f;

	/** "No distance available" -- see FHudObjectiveEntry::DistanceMeters for why zero cannot mean it. */
	constexpr float NoDistance = -1.f;

	/** Metres from Origin to the marker's resolved position, or NoDistance when it cannot be
	 *  resolved. ResolveLocation() falls back to the static WorldLocation when a target is gone,
	 *  which for a marker authored to FOLLOW something is a location that was never set -- it would
	 *  read as a confident distance to the map origin, so the sentinel is the only honest answer. */
	float MeasureDistanceMeters(const FVector& Origin, const FObjectiveMarker& Marker)
	{
		const bool bStaleTarget = !Marker.TargetActor.IsExplicitlyNull() && !Marker.TargetActor.IsValid();
		if (bStaleTarget) return NoDistance;

		return FVector::Dist(Origin, Marker.ResolveLocation()) / HudBridgeUnrealUnitsPerMetre;
	}
}

UExtractionHudBridgeComponent::UExtractionHudBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UExtractionHudBridgeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!TryBindAll())
		StartBindRetry();
}

void UExtractionHudBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Set before anything else below can call into UnbindPawnSources -- the only thing this flag
	// exists to gate is the two BlueprintImplementableEvent pushes there, so it must be live before
	// UnbindAll runs, not just before the specific push.
	bTearingDown = true;

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
		World->GetTimerManager().ClearTimer(ObjectiveDistanceTimerHandle);
		World->GetTimerManager().ClearTimer(CoverMeStateTimerHandle);
		// SetTimerForNextTick hands back no handle, so the pending-notify flush can only be
		// cancelled object-wide.
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	bLootNotifyFlushScheduled = false;
	PendingLootNotifies.Reset();
	LastPushedDistances.Reset();

	UnbindAll();

	Super::EndPlay(EndPlayReason);
}

// ---- Binding ----

APlayerController* UExtractionHudBridgeComponent::ResolveOwningController()
{
	if (APlayerController* Cached = CachedController.Get()) return Cached;

	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return nullptr;

	// The owner is an AHUD subclass in practice, and that accessor is the authoritative answer.
	// The instigator fallback covers the bridge being dropped on some other actor instead.
	APlayerController* PC = nullptr;
	if (const AHUD* Hud = Cast<AHUD>(Owner))
		PC = Hud->GetOwningPlayerController();
	if (!IsValid(PC))
		PC = Cast<APlayerController>(Owner->GetInstigatorController());
	if (!IsValid(PC)) return nullptr;

	CachedController = PC;
	return PC;
}

bool UExtractionHudBridgeComponent::TryBindAll()
{
	APlayerController* PC = ResolveOwningController();
	if (!IsValid(PC)) return false;

	// Kept for this component's whole life rather than dropped on first success: possession
	// changes more than once (checkpoint restart, respawn), and a one-shot subscription is
	// exactly how the old HUD widgets stopped tracking the pawn after the first swap.
	PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandlePossessedPawnChanged);

	// Deliberately not short-circuited -- one missing source must not stop the others binding.
	const bool bObjectives = BindObjectiveSubsystem();
	const bool bInventory = BindInventorySubsystem();
	const bool bPawn = BindPawnSources(*PC);
	const bool bCompanion = BindCompanion();

	return bObjectives && bInventory && bPawn && bCompanion;
}

bool UExtractionHudBridgeComponent::BindPawnSources(APlayerController& PC)
{
	APawn* Pawn = PC.GetPawn();
	if (!IsValid(Pawn)) return false;

	const bool bHealth = BindHealth(*Pawn);
	const bool bWeapon = BindWeapon(*Pawn);
	const bool bConsumables = BindConsumables(*Pawn);
	const bool bPrompts = BindPrompts(*Pawn);
	const bool bCommand = BindCompanionCommand(*Pawn);

	return bHealth && bWeapon && bConsumables && bPrompts && bCommand;
}

bool UExtractionHudBridgeComponent::BindObjectiveSubsystem()
{
	const UWorld* World = GetWorld();
	UObjectiveSubsystem* Objectives = World ? World->GetSubsystem<UObjectiveSubsystem>() : nullptr;
	if (!IsValid(Objectives)) return false;

	// Armed on every pass, not just the first: it is idempotent, and the distance channel has no
	// delegate to re-establish it if the timer is ever lost.
	StartObjectiveDistanceUpdates();

	if (CachedObjectiveSubsystem.Get() == Objectives) return true;

	CachedObjectiveSubsystem = Objectives;
	Objectives->OnObjectivesChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleObjectivesChanged);
	Objectives->OnObjectiveLabelChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleObjectiveLabelChanged);
	Objectives->OnObjectiveStateChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleObjectiveStateChanged);
	PushObjectiveList();
	return true;
}

bool UExtractionHudBridgeComponent::BindInventorySubsystem()
{
	const UWorld* World = GetWorld();
	UMissionInventorySubsystem* Inventory = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!IsValid(Inventory)) return false;
	if (CachedInventorySubsystem.Get() == Inventory) return true;

	CachedInventorySubsystem = Inventory;
	Inventory->OnLootGranted.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleLootGranted);
	Inventory->OnToastNotify.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleToastNotify);
	Inventory->OnLootNotify.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleLootNotify);
	return true;
}

bool UExtractionHudBridgeComponent::BindHealth(APawn& Pawn)
{
	const IExtractionPlayerInterface* PlayerIface = Cast<IExtractionPlayerInterface>(&Pawn);
	UHealthComponent* Health = PlayerIface ? PlayerIface->GetHealthComponent() : nullptr;
	if (!IsValid(Health)) return false;
	if (CachedHealth.Get() == Health) return true;

	if (UHealthComponent* Old = CachedHealth.Get())
	{
		Old->OnHealthChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleHealthChanged);
		Old->OnShieldChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleShieldChanged);
	}

	CachedHealth = Health;
	Health->OnHealthChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleHealthChanged);
	Health->OnShieldChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleShieldChanged);
	RefreshHealth();
	return true;
}

bool UExtractionHudBridgeComponent::BindWeapon(APawn& Pawn)
{
	UWeaponComponent* WeaponComponent = Pawn.FindComponentByClass<UWeaponComponent>();
	if (!IsValid(WeaponComponent)) return false;

	const bool bComponentChanged = CachedWeaponComponent.Get() != WeaponComponent;
	if (bComponentChanged)
	{
		if (UWeaponComponent* Old = CachedWeaponComponent.Get())
			Old->OnActiveWeaponChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleActiveWeaponChanged);

		CachedWeaponComponent = WeaponComponent;
		WeaponComponent->OnActiveWeaponChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleActiveWeaponChanged);
	}

	// The component is stable across swaps; the weapon under it is not, and the equip that put it
	// there may well have fired before this bind landed. A NEW component is forced through: the
	// new pawn may have no weapon yet while the HUD still reads the old pawn's.
	RebindActiveWeapon(WeaponComponent->IsThrowableEquipped() ? nullptr : WeaponComponent->GetCurrentWeapon(),
		/*bForcePush=*/ bComponentChanged);
	return true;
}

bool UExtractionHudBridgeComponent::BindConsumables(APawn& Pawn)
{
	UConsumableInventoryComponent* Consumables = Pawn.FindComponentByClass<UConsumableInventoryComponent>();
	if (!IsValid(Consumables)) return false;
	if (CachedConsumables.Get() == Consumables) return true;

	if (UConsumableInventoryComponent* Old = CachedConsumables.Get())
		Old->OnStimCountChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleStimCountChanged);

	CachedConsumables = Consumables;
	Consumables->OnStimCountChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleStimCountChanged);
	RefreshConsumables();
	return true;
}

bool UExtractionHudBridgeComponent::BindPrompts(APawn& Pawn)
{
	AExtractionPlayer* Player = Cast<AExtractionPlayer>(&Pawn);
	if (!IsValid(Player)) return false;
	if (CachedPlayer.Get() == Player) return true;

	if (AExtractionPlayer* Old = CachedPlayer.Get())
	{
		Old->OnPromptStateChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandlePromptStateChanged);
		Old->OnPromptHoldStarted.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandlePromptHoldStarted);
		Old->OnPromptHoldEnded.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandlePromptHoldEnded);
	}

	CachedPlayer = Player;
	Player->OnPromptStateChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandlePromptStateChanged);
	Player->OnPromptHoldStarted.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandlePromptHoldStarted);
	Player->OnPromptHoldEnded.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandlePromptHoldEnded);
	RefreshPrompt();
	return true;
}

bool UExtractionHudBridgeComponent::BindCompanionCommand(APawn& Pawn)
{
	UCompanionCommandComponent* CommandComponent = Pawn.FindComponentByClass<UCompanionCommandComponent>();
	if (!IsValid(CommandComponent)) return false;
	if (CachedCommandComponent.Get() == CommandComponent) return true;

	if (UCompanionCommandComponent* Old = CachedCommandComponent.Get())
	{
		Old->OnCompanionModeChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleCompanionModeChanged);
		Old->OnModeMenuChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleModeMenuChanged);
		Old->OnPingChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleCompanionPromptChanged);
		Old->OnPingCandidateChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandlePingCandidateChanged);
	}

	CachedCommandComponent = CommandComponent;
	CommandComponent->OnCompanionModeChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleCompanionModeChanged);
	CommandComponent->OnModeMenuChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleModeMenuChanged);
	CommandComponent->OnPingChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleCompanionPromptChanged);
	CommandComponent->OnPingCandidateChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandlePingCandidateChanged);
	RefreshCompanion();
	return true;
}

bool UExtractionHudBridgeComponent::BindCompanion()
{
	UCompanionCommandComponent* CommandComponent = CachedCommandComponent.Get();
	if (!IsValid(CommandComponent)) return false;

	// Armed as soon as the command component exists, same as StartObjectiveDistanceUpdates below --
	// IsCoverMeAvailable and the cooldown getters don't need Companion resolved, and UpdateCoverMeState
	// reports "not available" on its own until the actor itself turns up.
	StartCoverMeStateUpdates();

	ACompanionCharacter* Companion = CommandComponent->GetCompanion();
	if (!IsValid(Companion)) return false; // not spawned yet -- the retry timer keeps asking
	if (CachedCompanion.Get() == Companion) return true;

	// Rebind rather than assume: a companion that died and respawned is a different actor.
	if (ACompanionCharacter* Old = CachedCompanion.Get())
		Old->OnCoveringFireTick.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleCoveringFireTick);

	CachedCompanion = Companion;
	Companion->OnCoveringFireTick.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleCoveringFireTick);
	return true;
}

void UExtractionHudBridgeComponent::RebindActiveWeapon(AWeaponBase* NewWeapon, bool bForcePush)
{
	// A weapon destroyed since the last push reads back as null through the weak pointer, which
	// would compare equal to a null NewWeapon and suppress the clear-readout push the destruction
	// needs. An EXPLICIT null (nothing ever bound, or null already pushed) is a genuine match.
	const bool bStale = !CachedWeapon.IsExplicitlyNull() && !CachedWeapon.IsValid();
	if (!bForcePush && !bStale && CachedWeapon.Get() == NewWeapon) return;

	if (AWeaponBase* Old = CachedWeapon.Get())
		Old->OnAmmoChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleAmmoChanged);

	CachedWeapon = NewWeapon;
	if (IsValid(NewWeapon))
		NewWeapon->OnAmmoChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleAmmoChanged);

	PushActiveWeapon(NewWeapon);

	// In the same call, never a frame later: a swap moves two weapons, and a second row still
	// showing the gun that just came into the hand reads as the HUD listing it twice. Null when
	// the component has gone (pawn torn down mid-swap) clears the row rather than stranding it.
	UWeaponComponent* WeaponComponent = CachedWeaponComponent.Get();
	PushStowedWeapon(IsValid(WeaponComponent) ? WeaponComponent->GetStowedWeapon() : nullptr);
}

void UExtractionHudBridgeComponent::UnbindPawnSources()
{
	if (UHealthComponent* Health = CachedHealth.Get())
	{
		Health->OnHealthChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleHealthChanged);
		Health->OnShieldChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleShieldChanged);
	}
	if (UWeaponComponent* WeaponComponent = CachedWeaponComponent.Get())
		WeaponComponent->OnActiveWeaponChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleActiveWeaponChanged);
	if (AWeaponBase* Weapon = CachedWeapon.Get())
		Weapon->OnAmmoChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleAmmoChanged);
	if (UConsumableInventoryComponent* Consumables = CachedConsumables.Get())
		Consumables->OnStimCountChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleStimCountChanged);
	if (AExtractionPlayer* Player = CachedPlayer.Get())
	{
		Player->OnPromptStateChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandlePromptStateChanged);
		Player->OnPromptHoldStarted.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandlePromptHoldStarted);
		Player->OnPromptHoldEnded.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandlePromptHoldEnded);
	}
	if (UCompanionCommandComponent* CommandComponent = CachedCommandComponent.Get())
	{
		CommandComponent->OnCompanionModeChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleCompanionModeChanged);
		CommandComponent->OnModeMenuChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleModeMenuChanged);
		CommandComponent->OnPingChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleCompanionPromptChanged);
		CommandComponent->OnPingCandidateChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandlePingCandidateChanged);
	}

	// Outside the if above, on purpose: a component already gone through the weak pointer (marked
	// garbage before the possession callback lands, or a RefreshAll -> TryBindAll after the pawn
	// died) must still clear the HUD, not just one that unbound cleanly. Neither channel has a
	// polling fallback like cover-me's timer, so this is the only place that can catch it.
	// Suppressed during EndPlay -- bTearingDown -- since every other channel's teardown is silent
	// and calling into the HUD Blueprint while the world is being torn down is the one thing this
	// pair must not do.
	if (!bTearingDown)
	{
		PushCompanionPrompt(ECompanionCommand::None, /*bForce=*/ false);
		OnPingCandidateChangedBP(false, FVector::ZeroVector);
	}

	if (ACompanionCharacter* Companion = CachedCompanion.Get())
		Companion->OnCoveringFireTick.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleCoveringFireTick);

	CachedHealth.Reset();
	CachedWeaponComponent.Reset();
	CachedWeapon.Reset();
	CachedConsumables.Reset();
	CachedPlayer.Reset();
	CachedCommandComponent.Reset();
	CachedCompanion.Reset();
}

void UExtractionHudBridgeComponent::UnbindAll()
{
	UnbindPawnSources();

	if (UObjectiveSubsystem* Objectives = CachedObjectiveSubsystem.Get())
	{
		Objectives->OnObjectivesChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleObjectivesChanged);
		Objectives->OnObjectiveLabelChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleObjectiveLabelChanged);
		Objectives->OnObjectiveStateChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleObjectiveStateChanged);
	}
	if (UMissionInventorySubsystem* Inventory = CachedInventorySubsystem.Get())
	{
		Inventory->OnLootGranted.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleLootGranted);
		Inventory->OnToastNotify.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleToastNotify);
		Inventory->OnLootNotify.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandleLootNotify);
	}
	if (APlayerController* PC = CachedController.Get())
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UExtractionHudBridgeComponent::HandlePossessedPawnChanged);

	CachedObjectiveSubsystem.Reset();
	CachedInventorySubsystem.Reset();
	CachedController.Reset();
}

void UExtractionHudBridgeComponent::StartBindRetry()
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	if (World->GetTimerManager().IsTimerActive(BindRetryTimerHandle)) return;

	World->GetTimerManager().SetTimer(BindRetryTimerHandle, this, &UExtractionHudBridgeComponent::RetryBind,
		FMath::Max(BindRetryInterval, MinBindRetryInterval), /*bLoop=*/ true);
}

void UExtractionHudBridgeComponent::RetryBind()
{
	if (!TryBindAll()) return;

	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(BindRetryTimerHandle);

	// The one line worth having when the HUD comes up blank: it says whether the bridge ever
	// finished binding, which separates a source problem from a Blueprint-side one.
	UE_LOG(LogHudBridge, Verbose, TEXT("HUD bridge fully bound on '%s'"), *GetNameSafe(GetOwner()));
}

void UExtractionHudBridgeComponent::StartObjectiveDistanceUpdates()
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	if (World->GetTimerManager().IsTimerActive(ObjectiveDistanceTimerHandle)) return;

	World->GetTimerManager().SetTimer(ObjectiveDistanceTimerHandle, this,
		&UExtractionHudBridgeComponent::UpdateObjectiveDistances,
		FMath::Max(ObjectiveDistanceInterval, MinObjectiveDistanceInterval), /*bLoop=*/ true);
}

void UExtractionHudBridgeComponent::UpdateObjectiveDistances()
{
	PushObjectiveDistances(/*bForce=*/ false);
}

void UExtractionHudBridgeComponent::StartCoverMeStateUpdates()
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	if (World->GetTimerManager().IsTimerActive(CoverMeStateTimerHandle)) return;

	World->GetTimerManager().SetTimer(CoverMeStateTimerHandle, this,
		&UExtractionHudBridgeComponent::TickCoverMeState,
		FMath::Max(CoverMeStateInterval, MinCoverMeStateInterval), /*bLoop=*/ true);
}

void UExtractionHudBridgeComponent::TickCoverMeState()
{
	UpdateCoverMeState(/*bForce=*/ false);
}

// ---- HUD-facing API ----

void UExtractionHudBridgeComponent::RefreshAll()
{
	// A HUD rebuild is also the best moment to notice a source that arrived late.
	TryBindAll();

	RefreshHealth();
	RefreshWeapon();
	RefreshObjectives();
	RefreshConsumables();
	RefreshPrompt();
	RefreshCompanion();
	OnGrenadeCountChangedBP(LastGrenadeCount);

	// Last, and unconditionally: a context switch rebuilds modules in their default visible state,
	// so a HUD hidden for a pause screen has to be told again or it pops back over the top of it.
	// Snap, never fade: this is a correction, not a transition. The rebuilt modules are already
	// on screen at default visibility, so replaying the fade leaves them showing over the pause
	// screen for its whole duration -- a shorter version of the pop this exists to prevent.
	if (bHudHidden)
		OnHudVisibilityRequestedBP(true, 0.f);
}

void UExtractionHudBridgeComponent::NotifyGrenadeCountChanged(int32 NewCount)
{
	LastGrenadeCount = FMath::Max(NewCount, 0);
	OnGrenadeCountChangedBP(LastGrenadeCount);
}

void UExtractionHudBridgeComponent::SetHudHidden(bool bHidden, float FadeDuration)
{
	// A repeat request would restart the fade from wherever it had got to, which reads as a
	// flicker when pause and resume land close together.
	if (bHidden == bHudHidden) return;

	bHudHidden = bHidden;
	HudHiddenFadeDuration = FMath::Max(FadeDuration, 0.f);
	OnHudVisibilityRequestedBP(bHudHidden, HudHiddenFadeDuration);
}

// ---- Refresh helpers ----

void UExtractionHudBridgeComponent::RefreshHealth()
{
	UHealthComponent* Health = CachedHealth.Get();
	if (!IsValid(Health)) return;

	OnHealthChangedBP(Health->GetCurrentHealth(), Health->GetMaxHealth());
	OnShieldChangedBP(Health->GetCurrentShield(), Health->GetMaxShield());
}

void UExtractionHudBridgeComponent::RefreshWeapon()
{
	UWeaponComponent* WeaponComponent = CachedWeaponComponent.Get();
	if (!IsValid(WeaponComponent)) return;

	PushActiveWeapon(WeaponComponent->IsThrowableEquipped() ? nullptr : WeaponComponent->GetCurrentWeapon());

	// Not gated on the throwable: the stowed slot is unchanged by having a grenade in hand, so the
	// second row keeps its weapon while the active row blanks.
	PushStowedWeapon(WeaponComponent->GetStowedWeapon());
}

void UExtractionHudBridgeComponent::RefreshObjectives()
{
	PushObjectiveList();

	// The rebuilt list already carries DistanceMeters, but a module wired only to the distance
	// channel has heard nothing on it -- RefreshAll's contract is that every channel replays.
	PushObjectiveDistances(/*bForce=*/ true);
}

void UExtractionHudBridgeComponent::RefreshConsumables()
{
	UConsumableInventoryComponent* Consumables = CachedConsumables.Get();
	if (!IsValid(Consumables)) return;

	OnStimCountChangedBP(Consumables->GetStimCount(), Consumables->GetMaxStims());
}

void UExtractionHudBridgeComponent::RefreshPrompt()
{
	AExtractionPlayer* Player = CachedPlayer.Get();
	if (!IsValid(Player)) return;

	OnPromptChangedBP(Player->GetHudPromptKind(), Player->GetHudPromptText(), Player->GetHudPromptHoldDuration());
}

void UExtractionHudBridgeComponent::RefreshCompanion()
{
	// Ahead of the early return: UpdateCoverMeState already reads its sources through weak pointers
	// and reports "not available" when either is gone, so a map whose pawn never gets a
	// UCompanionCommandComponent still gets one push instead of leaving the badge on the WBP default.
	// Forced because RefreshAll's contract is every channel replays, and the epsilon dedup would
	// otherwise report the badge unchanged to a module that has heard nothing yet.
	UpdateCoverMeState(/*bForce=*/ true);

	UCompanionCommandComponent* CommandComponent = CachedCommandComponent.Get();
	if (!IsValid(CommandComponent))
	{
		// A module built after the command component was already gone has heard nothing -- give it
		// the unambiguous "no prompt" / "no candidate" state rather than leaving it on the WBP default.
		PushCompanionPrompt(ECompanionCommand::None, /*bForce=*/ true);
		OnPingCandidateChangedBP(false, FVector::ZeroVector);
		return;
	}

	OnCompanionModeChangedBP(CommandComponent->GetCompanionMode());
	OnCompanionMenuOpenChangedBP(CommandComponent->IsModeMenuOpen());
	PushCompanionPrompt(CommandComponent->GetPendingCommand(), /*bForce=*/ true);
	OnPingCandidateChangedBP(CommandComponent->HasPingCandidate(), CommandComponent->GetPingCandidateLocation());
}

void UExtractionHudBridgeComponent::UpdateCoverMeState(bool bForce)
{
	UCompanionCommandComponent* CommandComponent = CachedCommandComponent.Get();
	ACompanionCharacter* Companion = CachedCompanion.Get();

	bool bAvailable = false;
	float CooldownRemaining = 0.f;
	float CooldownDuration = 0.f;
	if (IsValid(CommandComponent) && IsValid(Companion))
	{
		bAvailable = CommandComponent->IsCoverMeAvailable();
		CooldownRemaining = Companion->GetCoveringFireCooldownRemaining();
		CooldownDuration = Companion->GetCoveringFireCooldownDuration();
	}

	const bool bAvailabilityFlipped = bAvailable != bLastCoverMeAvailable;
	const bool bRemainingMoved = FMath::Abs(CooldownRemaining - LastCoverMeCooldownRemaining) > CoverMeStateBroadcastEpsilon;
	if (!bForce && bCoverMeStateEverPushed && !bAvailabilityFlipped && !bRemainingMoved) return;

	bLastCoverMeAvailable = bAvailable;
	LastCoverMeCooldownRemaining = CooldownRemaining;
	bCoverMeStateEverPushed = true;

	OnCoverMeStateChangedBP(bAvailable, CooldownRemaining, CooldownDuration);
}

void UExtractionHudBridgeComponent::PushCompanionPrompt(ECompanionCommand Command, bool bForce)
{
	if (!bForce && Command == LastPushedPrompt) return;

	LastPushedPrompt = Command;
	OnCompanionPromptChangedBP(Command);
}

void UExtractionHudBridgeComponent::PushObjectiveList()
{
	// OnObjectivesRebuiltBP below hands ObjectiveScratch to Blueprint by reference. A handler that
	// adds/removes an objective (or calls RefreshAll) re-raises OnObjectivesChanged synchronously,
	// which re-enters here while that same call is still executing -- and Reset()/Reserve() on the
	// same array out from under it would reallocate the buffer the outer frame is still walking.
	// Dropping the nested rebuild rather than letting it run is correct, not just safe: the label and
	// state deltas it would carry already have their own dedicated events (HandleObjectiveLabelChanged
	// / HandleObjectiveStateChanged) that fire independently of this latch, and RefreshObjectives'
	// own PushObjectiveDistances(true) -- both the nested call's and, after this returns, the outer
	// call's -- re-measures every currently-live marker regardless. The one gap a dropped nested pass
	// leaves is a membership change (an objective added or removed mid-callback) not reaching
	// OnObjectivesRebuiltBP until the next trigger -- acceptable here, unlike
	// UObjectiveMarkerLayer::RebuildMarkers, because this bridge only relays to Blueprint and has no
	// widget instances of its own that must reconcile to the new truth.
	if (bPushingObjectives) return;
	bPushingObjectives = true;

	UObjectiveSubsystem* Objectives = CachedObjectiveSubsystem.Get();
	if (!IsValid(Objectives))
	{
		bPushingObjectives = false;
		return;
	}

	FVector Origin;
	const bool bHaveOrigin = GetDistanceOrigin(Origin);

	const TArray<FObjectiveMarker>& Markers = Objectives->GetObjectives();
	ObjectiveScratch.Reset();
	ObjectiveScratch.Reserve(Markers.Num());
	LastPushedDistances.Reset();

	for (const FObjectiveMarker& Marker : Markers)
	{
		FHudObjectiveEntry& Entry = ObjectiveScratch.AddDefaulted_GetRef();
		Entry.Id = Marker.Id;
		Entry.Label = Marker.Label;
		Entry.bOptional = Marker.bOptional;
		Entry.State = Marker.State;
		Entry.DistanceMeters = bHaveOrigin ? MeasureDistanceMeters(Origin, Marker) : NoDistance;

		// Only a measured value becomes the epsilon baseline. Seeding the sentinel would leave the
		// timer comparing a real distance against -1 forever after, which reads as "changed" every
		// single tick -- the exact spam the epsilon exists to stop.
		if (bHaveOrigin)
			LastPushedDistances.Add(Marker.Id, Entry.DistanceMeters);
	}

	OnObjectivesRebuiltBP(ObjectiveScratch);
	bPushingObjectives = false;
}

void UExtractionHudBridgeComponent::PushObjectiveDistances(bool bForce)
{
	UObjectiveSubsystem* Objectives = CachedObjectiveSubsystem.Get();
	if (!IsValid(Objectives)) return;

	FVector Origin;
	// Nothing to measure from is not a distance of zero. Holding the last pushed value beats
	// blanking every line for the frame or two a possession change takes to hand the camera back.
	if (!GetDistanceOrigin(Origin)) return;

	const TArray<FObjectiveMarker>& Markers = Objectives->GetObjectives();

	// Measured into a local, then broadcast: OnObjectiveDistanceChangedBP is a Blueprint call that
	// may add or remove an objective, which would resize the array being walked.
	TArray<TPair<FName, float>, TInlineAllocator<8>> Pending;
	Pending.Reserve(Markers.Num());

	for (const FObjectiveMarker& Marker : Markers)
	{
		const float Distance = MeasureDistanceMeters(Origin, Marker);

		const float* Last = LastPushedDistances.Find(Marker.Id);
		if (!bForce && Last && FMath::Abs(*Last - Distance) <= HudBridgeDistanceBroadcastEpsilon) continue;

		LastPushedDistances.Add(Marker.Id, Distance);
		Pending.Emplace(Marker.Id, Distance);
	}

	for (const TPair<FName, float>& Push : Pending)
		OnObjectiveDistanceChangedBP(Push.Key, Push.Value);
}

bool UExtractionHudBridgeComponent::GetDistanceOrigin(FVector& OutLocation) const
{
	APlayerController* PC = CachedController.Get();
	if (!IsValid(PC)) return false;

	// The view point, not the pawn: the on-screen objective markers measure from here, and a card
	// measuring from the pawn instead would print a different number for the same objective.
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(OutLocation, ViewRotation);
	return true;
}

void UExtractionHudBridgeComponent::PushActiveWeapon(AWeaponBase* Weapon)
{
	if (!IsValid(Weapon))
	{
		// -1 is the "no weapon" sentinel: a live weapon can legitimately read 0/0 when fired dry
		// with no reserve, so zero ammo cannot be used to mean an empty hand.
		OnActiveWeaponChangedBP(FText::GetEmpty(), nullptr, -1, -1);
		return;
	}

	const UWeaponDataAsset* Data = Weapon->GetWeaponData();
	OnActiveWeaponChangedBP(
		Data ? Data->DisplayName : FText::GetEmpty(),
		Data ? Data->HudIcon.Get() : nullptr,
		Weapon->GetCurrentAmmo(),
		Weapon->GetReserveAmmo());
}

void UExtractionHudBridgeComponent::PushStowedWeapon(AWeaponBase* Weapon)
{
	if (!IsValid(Weapon))
	{
		// Same -1 sentinel as the active row, for the same reason: a single-weapon loadout and a
		// stowed weapon put away dry are different things, and only the sentinel separates them.
		OnStowedWeaponChangedBP(FText::GetEmpty(), nullptr, -1, -1);
		return;
	}

	const UWeaponDataAsset* Data = Weapon->GetWeaponData();
	OnStowedWeaponChangedBP(
		Data ? Data->DisplayName : FText::GetEmpty(),
		Data ? Data->HudIcon.Get() : nullptr,
		Weapon->GetCurrentAmmo(),
		Weapon->GetReserveAmmo());
}

// ---- Delegate handlers ----

void UExtractionHudBridgeComponent::HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	// Full drop before the rebuild: a new pawn missing a component the old one had would
	// otherwise leave that stale subscription live and feeding the HUD the wrong actor's data.
	UnbindPawnSources();

	if (!TryBindAll())
		StartBindRetry();
}

void UExtractionHudBridgeComponent::HandleHealthChanged(float CurrentHealth, float MaxHealth)
{
	OnHealthChangedBP(CurrentHealth, MaxHealth);
}

void UExtractionHudBridgeComponent::HandleShieldChanged(float CurrentShield, float MaxShield)
{
	OnShieldChangedBP(CurrentShield, MaxShield);
}

void UExtractionHudBridgeComponent::HandleActiveWeaponChanged(AWeaponBase* NewWeapon)
{
	RebindActiveWeapon(NewWeapon);
}

void UExtractionHudBridgeComponent::HandleAmmoChanged(int32 CurrentAmmo, int32 ReserveAmmo)
{
	OnAmmoChangedBP(CurrentAmmo, ReserveAmmo);
}

void UExtractionHudBridgeComponent::HandleObjectivesChanged()
{
	// The full refresh, not PushObjectiveList alone: an objective added here seeds its own epsilon
	// baseline, so the next timer tick would report it unchanged and a card wired to the distance
	// channel would sit blank until the player moved. Costs one forced push per add/remove.
	RefreshObjectives();
}

void UExtractionHudBridgeComponent::HandleObjectiveLabelChanged(FName Id, const FText& NewLabel)
{
	OnObjectiveLabelChangedBP(Id, NewLabel);
}

void UExtractionHudBridgeComponent::HandleObjectiveStateChanged(FName Id, EObjectiveState NewState)
{
	OnObjectiveStateChangedBP(Id, NewState);
}

void UExtractionHudBridgeComponent::HandleLootGranted(ELootType Type, int32 Amount, const FText& Label)
{
	// This grant's message is already queued as an alert (the subsystem raised OnLootNotify with
	// the same text a line earlier). The pickup module is about to show it, so drop the alert
	// copy. RemoveSingle semantics on purpose: two identical grants in one frame should still
	// leave one alert queued if only one of them paired.
	const int32 Duplicate = PendingLootNotifies.IndexOfByPredicate(
		[&Label](const FText& Pending) { return Pending.EqualTo(Label); });
	if (Duplicate != INDEX_NONE)
		PendingLootNotifies.RemoveAt(Duplicate);

	OnLootGrantedBP(Type, Amount, Label);
}

void UExtractionHudBridgeComponent::HandleLootNotify(const FText& Message)
{
	PendingLootNotifies.Add(Message);

	if (bLootNotifyFlushScheduled) return;

	const UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		// No world to schedule against -- losing the dedup beats losing the message entirely.
		FlushPendingLootNotifies();
		return;
	}

	bLootNotifyFlushScheduled = true;
	World->GetTimerManager().SetTimerForNextTick(this, &UExtractionHudBridgeComponent::FlushPendingLootNotifies);
}

void UExtractionHudBridgeComponent::FlushPendingLootNotifies()
{
	bLootNotifyFlushScheduled = false;

	// Every one of these is a message, not an acquisition: refusals, door and lift feedback,
	// extraction failure reasons, objective toasts. Info severity -- the emphatic channel is
	// OnToastNotify, which carries its own severity.
	for (const FText& Message : PendingLootNotifies)
		OnToastBP(Message, EToastSeverity::Info);

	PendingLootNotifies.Reset();
}

void UExtractionHudBridgeComponent::HandleToastNotify(const FText& Message, EToastSeverity Severity)
{
	OnToastBP(Message, Severity);
}

void UExtractionHudBridgeComponent::HandleStimCountChanged(int32 NewStimCount)
{
	UConsumableInventoryComponent* Consumables = CachedConsumables.Get();
	OnStimCountChangedBP(NewStimCount, IsValid(Consumables) ? Consumables->GetMaxStims() : 0);
}

void UExtractionHudBridgeComponent::HandlePromptStateChanged(EHudPromptKind Kind, const FText& Prompt, float HoldDuration)
{
	OnPromptChangedBP(Kind, Prompt, HoldDuration);
}

void UExtractionHudBridgeComponent::HandlePromptHoldStarted(float Duration)
{
	OnPromptHoldStartedBP(Duration);
}

void UExtractionHudBridgeComponent::HandlePromptHoldEnded(bool bCompleted)
{
	OnPromptHoldEndedBP(bCompleted);
}

void UExtractionHudBridgeComponent::HandleCompanionModeChanged(ECompanionMode NewMode)
{
	OnCompanionModeChangedBP(NewMode);
}

void UExtractionHudBridgeComponent::HandleModeMenuChanged(bool bOpen)
{
	OnCompanionMenuOpenChangedBP(bOpen);
}

void UExtractionHudBridgeComponent::HandleCoveringFireTick(float Remaining, bool bPaused)
{
	OnCoveringFireTickBP(Remaining, bPaused);

	// Forced, not left to the next 10 Hz poll: ClearCoveringFire stamps the cooldown start and
	// raises this same delegate in one call, so an un-forced push would let up to a whole poll
	// interval pass with the active-window channel already at 0 and the cooldown channel not yet
	// telling the badge a lockout has begun -- a shorter version of the flash this channel exists to
	// close. The delegate is already throttled to 10 Hz on the companion side, so this costs nothing.
	UpdateCoverMeState(/*bForce=*/ true);
}

void UExtractionHudBridgeComponent::HandleCompanionPromptChanged(ECompanionCommand PendingCommand, AActor* PingedTarget)
{
	// PingedTarget deliberately dropped -- the HUD only needs the command kind to pick a panel/verb,
	// never which actor it points at. Don't re-add it.
	PushCompanionPrompt(PendingCommand, /*bForce=*/ false);
}

void UExtractionHudBridgeComponent::HandlePingCandidateChanged(bool bHasCandidate, FVector WorldLocation)
{
	OnPingCandidateChangedBP(bHasCandidate, WorldLocation);
}
