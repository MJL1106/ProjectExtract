// UExtractionHudBridgeComponent -- the single gameplay -> HUD seam.
//
// Lives on the HUD actor. Subscribes to every gameplay channel the HUD draws from (health,
// shield, carried weapons, ammo, objectives, loot, toasts, stims, hold prompts, companion mode)
// and re-raises each one as a BlueprintImplementableEvent, so the HUD Blueprint implements
// events instead of hunting for pawns and components itself. No gameplay logic lives here and
// no asset path does either -- every widget/module reference stays on the BP.
//
// Two timing facts shape the whole class:
//   * The HUD actor can exist before the pawn, and the companion spawns later still, so binding
//     is retried on a timer and re-run on every possession change rather than once at BeginPlay.
//   * An edge-only channel has nothing to replay. RefreshAll() re-pushes the CURRENT value of
//     every channel and is what the HUD calls once its own framework is initialized (and after
//     any teardown/rebuild) -- without it, everything raised before that point is lost.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Character/ExtractionPlayer.h"
#include "Companion/CompanionTypes.h"
#include "Game/MissionInventorySubsystem.h"
#include "Game/ObjectiveSubsystem.h"
#include "World/LootTypes.h"
#include "ExtractionHudBridgeComponent.generated.h"

class ACompanionCharacter;
class APlayerController;
class AWeaponBase;
class UCompanionCommandComponent;
class UConsumableInventoryComponent;
class UHealthComponent;
class UTexture2D;
class UWeaponComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogHudBridge, Log, All);

/** One objective line, flattened for the HUD. FObjectiveMarker carries world-marker plumbing
 *  (target actor, offsets, resolve height) the HUD panel has no use for. */
USTRUCT(BlueprintType)
struct EXTRACTION_API FHudObjectiveEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Objectives")
	FName Id = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Objectives")
	FText Label;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Objectives")
	bool bOptional = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Objectives")
	EObjectiveState State = EObjectiveState::Tracked;
};

/** Blueprintable: the HUD events below are BlueprintImplementableEvents, which only a Blueprint
 *  deriving from this component can implement — the owning HUD Blueprint cannot. */
UCLASS(ClassGroup = "UI", Blueprintable, meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UExtractionHudBridgeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UExtractionHudBridgeComponent();

	// ---- HUD-facing API ----

	/** Re-pushes the current value of every channel through the events below. Call on HUD
	 *  framework init and after any context switch that rebuilds modules -- a module created
	 *  after the last edge fired is otherwise blank until the next gameplay change. Also
	 *  re-runs binding, so a pawn that arrived late is picked up here too. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Bridge")
	void RefreshAll();

	/** Inbound from the kit BP, which owns the throwable count. Mirrors it into the HUD and
	 *  caches it so RefreshAll can replay it like every other channel. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Bridge")
	void NotifyGrenadeCountChanged(int32 NewCount);

	/** Asks the HUD Blueprint to hide or show its entire module tree, with an optional fade.
	 *  Routed through the Blueprint rather than driven from C++ because the module manager and its
	 *  fade animations are Blueprint-only -- C++ cannot reach them without naming an asset.
	 *  Idempotent: a repeat request for the state already held raises nothing. The requested state
	 *  is remembered and re-asserted by RefreshAll, so modules rebuilt by a context switch while
	 *  the HUD is hidden do not pop back into view. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Bridge")
	void SetHudHidden(bool bHidden, float FadeDuration = 0.f);

	/** True while the HUD tree is hidden by a SetHudHidden request. */
	UFUNCTION(BlueprintPure, Category = "HUD|Bridge")
	bool IsHudHidden() const { return bHudHidden; }

	// ---- Events implemented by the HUD Blueprint ----

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnHealthChangedBP(float Current, float Max);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnShieldChangedBP(float Current, float Max);

	/** Icon and DisplayName come off the weapon's data asset; both may be unset while a weapon
	 *  is still un-authored. Null weapon (hand empty / throwable out) raises empty text, a null
	 *  icon and CurrentAmmo == -1 so the HUD can clear the readout from one unambiguous test —
	 *  zero ammo cannot mean this, since a live weapon fired dry with no reserve reads 0/0. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnActiveWeaponChangedBP(const FText& DisplayName, UTexture2D* Icon, int32 CurrentAmmo, int32 ReserveAmmo);

	/** The carried weapon that is NOT in hand -- the HUD's second row. Same contract as the active
	 *  event: no stowed weapon (single-weapon loadout, empty second slot) raises empty text, a null
	 *  icon and CurrentAmmo == -1, since a stowed weapon put away dry reads a genuine 0/0. Always
	 *  raised alongside OnActiveWeaponChangedBP -- a swap does not change one weapon, it moves two,
	 *  and a second row updated a frame later reads as the HUD showing the same gun twice. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnStowedWeaponChangedBP(const FText& DisplayName, UTexture2D* Icon, int32 CurrentAmmo, int32 ReserveAmmo);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnAmmoChangedBP(int32 Current, int32 Reserve);

	/** Full objective list, raised on any add/remove/clear. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnObjectivesRebuiltBP(const TArray<FHudObjectiveEntry>& Objectives);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnObjectiveLabelChangedBP(FName Id, const FText& Label);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnObjectiveStateChangedBP(FName Id, EObjectiveState State);

	/** Success-only acquisitions -- the pickup display. Refusals arrive on OnToastBP instead. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnLootGrantedBP(ELootType Type, int32 Amount, const FText& Label);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnToastBP(const FText& Message, EToastSeverity Severity);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnStimCountChangedBP(int32 Count, int32 MaxCount);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnGrenadeCountChangedBP(int32 Count);

	/** Prompt text is empty for the Revive kind -- a downed teammate carries no per-target verb,
	 *  so the HUD supplies that label. HoldDuration is 0 for a press-only prompt. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnPromptChangedBP(EHudPromptKind Kind, const FText& Prompt, float HoldDuration);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnPromptHoldStartedBP(float Duration);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnPromptHoldEndedBP(bool bCompleted);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnCompanionModeChangedBP(ECompanionMode Mode);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnCompanionMenuOpenChangedBP(bool bOpen);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnCoveringFireTickBP(float Remaining, bool bPaused);

	/** Implement with the module manager's HideOrShowEntireHud(bHidden, FadeDuration). The only
	 *  event on this component that flows HUD-ward as a command rather than as gameplay data. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnHudVisibilityRequestedBP(bool bHidden, float FadeDuration);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** How often binding is retried while any source is still missing. The companion in
	 *  particular can spawn well after the HUD. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Bridge", meta = (ClampMin = "0.05"))
	float BindRetryInterval = 0.25f;

private:
	// ---- Binding ----

	/** Owning controller: the HUD's own, or the owner's instigator when this is hosted on
	 *  something that is not an AHUD. Cached on success. */
	APlayerController* ResolveOwningController();

	/** One binding pass over every source. Returns true once nothing is left to bind, which is
	 *  what retires the retry timer. Safe to re-run at any time -- every bind is idempotent and
	 *  every one drops its previous subscription before taking a new one. */
	bool TryBindAll();

	bool BindPawnSources(APlayerController& PC);
	bool BindObjectiveSubsystem();
	bool BindInventorySubsystem();
	bool BindHealth(APawn& Pawn);
	bool BindWeapon(APawn& Pawn);
	bool BindConsumables(APawn& Pawn);
	bool BindPrompts(APawn& Pawn);
	bool BindCompanionCommand(APawn& Pawn);

	/** The companion can spawn long after the HUD, so this is the source the retry timer usually
	 *  outlives everything else for. */
	bool BindCompanion();

	/** Swaps the ammo subscription onto NewWeapon, dropping the previous one first, and pushes
	 *  the new weapon's readout. No-op when the weapon has not actually changed.
	 *  bForcePush overrides that dedup for the one case it cannot see: a pawn swap where the new
	 *  pawn's weapon is null while the HUD still shows the old pawn's. Deliberately not the
	 *  default -- BindWeapon re-runs every retry tick on a companion-less map, and an
	 *  unconditional push there would spam the Blueprint event. */
	void RebindActiveWeapon(AWeaponBase* NewWeapon, bool bForcePush = false);

	/** Everything hanging off the pawn (and the companion reached through it). Run on every
	 *  possession change so a new pawn is never mixed with the old one's components. */
	void UnbindPawnSources();

	void UnbindAll();

	/** (Re)arms the retry timer unless it is already running. */
	void StartBindRetry();

	void RetryBind();

	/** SetTimer never fires on a rate of zero -- floor the retry interval above it. */
	static constexpr float MinBindRetryInterval = 0.05f;

	// ---- Refresh helpers (one per channel; RefreshAll fans out) ----

	void RefreshHealth();
	void RefreshWeapon();
	void RefreshObjectives();
	void RefreshConsumables();
	void RefreshPrompt();
	void RefreshCompanion();

	/** Reads the objective subsystem into FHudObjectiveEntry and raises OnObjectivesRebuiltBP. */
	void PushObjectiveList();

	/** Reads name/icon/ammo off Weapon (null-safe) and raises OnActiveWeaponChangedBP. */
	void PushActiveWeapon(AWeaponBase* Weapon);

	/** Reads name/icon/ammo off Weapon (null-safe) and raises OnStowedWeaponChangedBP. The stowed
	 *  slot is read from the weapon component at every call site rather than cached -- nothing here
	 *  subscribes to it, so the component is the only thing that knows the slot emptied. */
	void PushStowedWeapon(AWeaponBase* Weapon);

	/** Raises OnToastBP for every loot message still queued at the end of the frame.
	 *
	 *  The dedup this exists for: the subsystem's three paired sites (stim, ammo, keycard grant)
	 *  raise OnLootNotify FIRST and OnLootGranted immediately after, with the SAME FText. Relaying
	 *  the notify on arrival would therefore always beat the grant that identifies it as a
	 *  duplicate, and the message would render twice -- once as a pickup, once as an alert.
	 *  Holding the queue until next tick lets HandleLootGranted cancel its own copy first, and
	 *  costs one frame of latency on a toast. Ordering-independent by construction, so a future
	 *  edit that swaps the two broadcasts cannot silently re-introduce the double render. */
	void FlushPendingLootNotifies();

	// ---- Delegate handlers ----

	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void HandleHealthChanged(float CurrentHealth, float MaxHealth);

	UFUNCTION()
	void HandleShieldChanged(float CurrentShield, float MaxShield);

	UFUNCTION()
	void HandleActiveWeaponChanged(AWeaponBase* NewWeapon);

	UFUNCTION()
	void HandleAmmoChanged(int32 CurrentAmmo, int32 ReserveAmmo);

	UFUNCTION()
	void HandleObjectivesChanged();

	UFUNCTION()
	void HandleObjectiveLabelChanged(FName Id, const FText& NewLabel);

	UFUNCTION()
	void HandleObjectiveStateChanged(FName Id, EObjectiveState NewState);

	UFUNCTION()
	void HandleLootGranted(ELootType Type, int32 Amount, const FText& Label);

	/** OnLootNotify carries every acquisition MESSAGE -- refusals ("Stims full", "no compatible
	 *  weapon"), door/lift/extraction feedback and objective toasts -- and three of its messages
	 *  are also raised on OnLootGranted. Queued rather than relayed on the spot; see
	 *  FlushPendingLootNotifies for why an immediate relay cannot dedup them. */
	UFUNCTION()
	void HandleLootNotify(const FText& Message);

	UFUNCTION()
	void HandleToastNotify(const FText& Message, EToastSeverity Severity);

	UFUNCTION()
	void HandleStimCountChanged(int32 NewStimCount);

	UFUNCTION()
	void HandlePromptStateChanged(EHudPromptKind Kind, const FText& Prompt, float HoldDuration);

	UFUNCTION()
	void HandlePromptHoldStarted(float Duration);

	UFUNCTION()
	void HandlePromptHoldEnded(bool bCompleted);

	UFUNCTION()
	void HandleCompanionModeChanged(ECompanionMode NewMode);

	UFUNCTION()
	void HandleModeMenuChanged(bool bOpen);

	UFUNCTION()
	void HandleCoveringFireTick(float Remaining, bool bPaused);

	// ---- Cached sources ----
	// All weak: the bridge owns none of these, and a pawn respawn or companion death must read
	// back as "gone" rather than as a dangling bind target.

	TWeakObjectPtr<APlayerController> CachedController;
	TWeakObjectPtr<UHealthComponent> CachedHealth;
	TWeakObjectPtr<UWeaponComponent> CachedWeaponComponent;
	TWeakObjectPtr<AWeaponBase> CachedWeapon;
	TWeakObjectPtr<UConsumableInventoryComponent> CachedConsumables;
	TWeakObjectPtr<UCompanionCommandComponent> CachedCommandComponent;
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;
	TWeakObjectPtr<AExtractionPlayer> CachedPlayer;
	TWeakObjectPtr<UObjectiveSubsystem> CachedObjectiveSubsystem;
	TWeakObjectPtr<UMissionInventorySubsystem> CachedInventorySubsystem;

	/** Cleared in EndPlay. Retired as soon as TryBindAll reports everything bound. */
	FTimerHandle BindRetryTimerHandle;

	/** The kit pushes grenade counts in; nothing to read them back from, so the last value is
	 *  kept here purely so RefreshAll can replay it. */
	int32 LastGrenadeCount = 0;

	/** Rebuilt in place on every objective change -- the list is small and this runs on a
	 *  designer-driven event, but the allocation is still worth keeping. */
	TArray<FHudObjectiveEntry> ObjectiveScratch;

	/** Loot messages raised this frame, awaiting the end-of-frame flush. A queue rather than one
	 *  slot because looting a container grants several items in a single call and each raises its
	 *  own message. Drained by FlushPendingLootNotifies; cleared with the timer in EndPlay. */
	TArray<FText> PendingLootNotifies;

	/** True while a flush is already scheduled, so N messages in one frame cost one next-tick
	 *  timer rather than N. */
	bool bLootNotifyFlushScheduled = false;

	/** Last state requested through SetHudHidden. Re-asserted by RefreshAll. */
	bool bHudHidden = false;

	/** Fade last requested alongside bHudHidden, replayed with it. */
	float HudHiddenFadeDuration = 0.f;
};
