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
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
		// SetTimerForNextTick hands back no handle, so the pending-notify flush can only be
		// cancelled object-wide.
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	bLootNotifyFlushScheduled = false;
	PendingLootNotifies.Reset();

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
	}

	CachedCommandComponent = CommandComponent;
	CommandComponent->OnCompanionModeChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleCompanionModeChanged);
	CommandComponent->OnModeMenuChanged.AddUniqueDynamic(this, &UExtractionHudBridgeComponent::HandleModeMenuChanged);
	RefreshCompanion();
	return true;
}

bool UExtractionHudBridgeComponent::BindCompanion()
{
	UCompanionCommandComponent* CommandComponent = CachedCommandComponent.Get();
	if (!IsValid(CommandComponent)) return false;

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
	UCompanionCommandComponent* CommandComponent = CachedCommandComponent.Get();
	if (!IsValid(CommandComponent)) return;

	OnCompanionModeChangedBP(CommandComponent->GetCompanionMode());
	OnCompanionMenuOpenChangedBP(CommandComponent->IsModeMenuOpen());
}

void UExtractionHudBridgeComponent::PushObjectiveList()
{
	UObjectiveSubsystem* Objectives = CachedObjectiveSubsystem.Get();
	if (!IsValid(Objectives)) return;

	const TArray<FObjectiveMarker>& Markers = Objectives->GetObjectives();
	ObjectiveScratch.Reset();
	ObjectiveScratch.Reserve(Markers.Num());

	for (const FObjectiveMarker& Marker : Markers)
	{
		FHudObjectiveEntry& Entry = ObjectiveScratch.AddDefaulted_GetRef();
		Entry.Id = Marker.Id;
		Entry.Label = Marker.Label;
		Entry.bOptional = Marker.bOptional;
		Entry.State = Marker.State;
	}

	OnObjectivesRebuiltBP(ObjectiveScratch);
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
	PushObjectiveList();
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
}
