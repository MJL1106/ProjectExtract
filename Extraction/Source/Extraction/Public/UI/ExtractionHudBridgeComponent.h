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
#include "Companion/CompanionCommandTypes.h"
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

	/** Metres from the player's view point to the objective's resolved marker location. -1 means no
	 *  distance is available -- no marker, or a target-following marker whose target is gone -- so
	 *  the HUD can collapse the line from one unambiguous test. Zero cannot carry that meaning:
	 *  standing on an objective is a real distance. Only a snapshot; live movement arrives on
	 *  OnObjectiveDistanceChangedBP rather than by re-raising the whole list. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Objectives")
	float DistanceMeters = -1.f;
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
	 *  zero ammo cannot mean this, since a live weapon fired dry with no reserve reads 0/0.
	 *  SlotNumber is 1 for the primary slot, 2 for the secondary, 0 paired with the no-weapon
	 *  sentinel above -- the dual-weapon HUD prints this as the panel's own keycap digit, and it is
	 *  NOT simply 1 here: the player can be holding the secondary, so this is resolved by comparing
	 *  the weapon against UWeaponComponent's slot pointers, never assumed from which event fired. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnActiveWeaponChangedBP(const FText& DisplayName, UTexture2D* Icon, int32 CurrentAmmo, int32 ReserveAmmo, int32 SlotNumber);

	/** The carried weapon that is NOT in hand -- the HUD's second row. Same contract as the active
	 *  event: no stowed weapon (single-weapon loadout, empty second slot) raises empty text, a null
	 *  icon and CurrentAmmo == -1, since a stowed weapon put away dry reads a genuine 0/0. Always
	 *  raised alongside OnActiveWeaponChangedBP -- a swap does not change one weapon, it moves two,
	 *  and a second row updated a frame later reads as the HUD showing the same gun twice.
	 *  SlotNumber follows the same 1/2/0 contract as OnActiveWeaponChangedBP's. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnStowedWeaponChangedBP(const FText& DisplayName, UTexture2D* Icon, int32 CurrentAmmo, int32 ReserveAmmo, int32 SlotNumber);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnAmmoChangedBP(int32 Current, int32 Reserve);

	/** Full objective list, raised on any add/remove/clear. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnObjectivesRebuiltBP(const TArray<FHudObjectiveEntry>& Objectives);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnObjectiveLabelChangedBP(FName Id, const FText& Label);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnObjectiveStateChangedBP(FName Id, EObjectiveState State);

	/** Live distance for one objective. Keyed by Id, never by index into the last rebuilt list --
	 *  the list can be rebuilt between two pushes, and an index contract would then quietly hand a
	 *  distance to the wrong line. -1 carries the same "no distance" meaning as
	 *  FHudObjectiveEntry::DistanceMeters. Its own channel rather than a re-raise of
	 *  OnObjectivesRebuiltBP because that event re-textures the card and recomputes its text wrap
	 *  width, which is far too much work to repeat at walking pace. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnObjectiveDistanceChangedBP(FName Id, float DistanceMeters);

	/** Success-only acquisitions -- the pickup display. Refusals arrive on OnToastBP instead.
	 *  Deliberately still 3-param: the new pickup toast stack reads FOnLootGranted directly from
	 *  the subsystem rather than through this event, so widening this signature to carry the
	 *  delegate's new trailing AmmoCategory would break the HUD Blueprint's existing event node
	 *  for no consumer. */
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

	/** WBP_PickupFocus's data source -- the 420x88 card anchored near a focused pickup. Sourced from
	 *  the player's Interact kind/prompt (EHudPromptKind::Interact), not from
	 *  UAttachmentStatPreviewWidget: that widget is fed raw kit bytes with no actor or location
	 *  attached, and its focus lifecycle is driven by the kit's own BP-side detection, not anything
	 *  this bridge can see. That also means this fires for ANY focused IWorldInteractable (a door, a
	 *  terminal), not only item pickups -- there is no generic "this is a pickup" signal in C++.
	 *  Category and ActionVerb have no source at all -- IWorldInteractable exposes one opaque prompt
	 *  string, not a structured breakdown -- and ship empty. Icon likewise has no source (nothing on
	 *  the interact path carries one) and ships null. ItemName carries that one opaque prompt string
	 *  as the closest real signal available, not a true isolated name. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnPickupFocusChangedBP(bool bFocused, const FText& Category, const FText& ItemName,
		const FText& ActionVerb, UTexture2D* Icon, FVector WorldLocation);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnCompanionModeChangedBP(ECompanionMode Mode);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnCompanionMenuOpenChangedBP(bool bOpen);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnCoveringFireTickBP(float Remaining, bool bPaused);

	/** Cover-me readiness. bAvailable is the full command gate (UCompanionCommandComponent::
	 *  IsCoverMeAvailable), not merely "cooldown expired". CooldownRemaining is seconds left on the
	 *  post-use lockout, 0 when none is running; CooldownDuration is its configured length so the HUD
	 *  can normalise a bar without duplicating the tuning value. Distinct from OnCoveringFireTickBP,
	 *  which counts down the ACTIVE window -- the badge needs both and they never overlap. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnCoverMeStateChangedBP(bool bAvailable, float CooldownRemaining, float CooldownDuration);

	/** The companion command the player's current ping is offering, or None when there is no live
	 *  prompt. The HUD renders one compact panel per command -- Takedown is the one command that
	 *  offers TWO actions and gets its own dual-choice panel, which is why the kind is relayed rather
	 *  than a pre-baked string. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnCompanionPromptChangedBP(ECompanionCommand PendingCommand);

	/** "Would a ping land on something right now, and where" -- the aiming state, distinct from
	 *  OnCompanionPromptChangedBP which only fires once a ping is committed. WorldLocation is
	 *  meaningless while bHasCandidate is false. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Events")
	void OnPingCandidateChangedBP(bool bHasCandidate, FVector WorldLocation);

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

	/** How often objective distances are re-measured and pushed. Every pass resolves every marker,
	 *  and a target-following marker costs a bounds query to resolve -- the clamp floor is what
	 *  keeps that off the per-frame budget. 5Hz is under the rate at which a rounded metre readout
	 *  visibly stutters and well over the rate a walking player outruns. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Bridge", meta = (ClampMin = "0.1"))
	float ObjectiveDistanceInterval = 0.2f;

	/** How often cover-me readiness is re-evaluated. Two cheap getter calls per pass, and the badge
	 *  only needs to be right to the tenth of a second it renders. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Bridge", meta = (ClampMin = "0.05"))
	float CoverMeStateInterval = 0.1f;

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
	 *  bForcePush overrides that dedup for two cases it cannot see on its own: a pawn swap where
	 *  the new pawn's weapon is null while the HUD still shows the old pawn's, and
	 *  HandleActiveWeaponChanged's forced call for a stowed-slot-only replace, where NewWeapon is
	 *  unchanged but the STOWED weapon underneath it is not -- see that handler's comment.
	 *  Deliberately not the default -- BindWeapon re-runs every retry tick on a companion-less map,
	 *  and an unconditional push there would spam the Blueprint event. */
	void RebindActiveWeapon(AWeaponBase* NewWeapon, bool bForcePush = false);

	/** Everything hanging off the pawn (and the companion reached through it). Run on every
	 *  possession change so a new pawn is never mixed with the old one's components. */
	void UnbindPawnSources();

	void UnbindAll();

	/** (Re)arms the retry timer unless it is already running. */
	void StartBindRetry();

	void RetryBind();

	/** (Re)arms the distance timer unless it is already running. */
	void StartObjectiveDistanceUpdates();

	void UpdateObjectiveDistances();

	/** (Re)arms the cover-me readiness timer unless it is already running. */
	void StartCoverMeStateUpdates();

	/** No-arg timer callback; forwards to UpdateCoverMeState(false) since SetTimer needs an exact
	 *  signature match and RefreshCompanion is the only other caller, which needs bForce. */
	void TickCoverMeState();

	/** Resolves CachedCompanion/CachedCommandComponent and raises OnCoverMeStateChangedBP when the
	 *  gate flipped or the remaining cooldown moved past the broadcast epsilon. Either source gone
	 *  pushes the unambiguous "not available, no cooldown" state once rather than freezing the badge
	 *  on the last value a since-dead companion left behind. */
	void UpdateCoverMeState(bool bForce);

	/** SetTimer never fires on a rate of zero -- floor the retry interval above it. */
	static constexpr float MinBindRetryInterval = 0.05f;

	/** ClampMin only constrains the editor field; a value set from code still needs the floor. */
	static constexpr float MinObjectiveDistanceInterval = 0.1f;

	/** ClampMin only constrains the editor field; a value set from code still needs the floor. */
	static constexpr float MinCoverMeStateInterval = 0.05f;

	/** Below this, a countdown's last digit hasn't moved enough for the badge (which renders
	 *  Ceil of the remaining seconds) to show a different number. */
	static constexpr float CoverMeStateBroadcastEpsilon = 0.05f;

	// ---- Refresh helpers (one per channel; RefreshAll fans out) ----

	void RefreshHealth();
	void RefreshWeapon();
	void RefreshObjectives();
	void RefreshConsumables();
	void RefreshPrompt();
	void RefreshCompanion();

	/** Derives OnPickupFocusChangedBP from an EHudPromptKind/Prompt pair and pushes it when the
	 *  derived (bFocused, ItemName) differs from the last push, or unconditionally when bForce.
	 *  Shared by HandlePromptStateChanged (live) and RefreshPrompt (replay/clear) for the same
	 *  reason PushCompanionPrompt is shared between its two call sites. */
	void PushPickupFocus(EHudPromptKind Kind, const FText& Prompt, bool bForce);

	/** Pushes OnCompanionPromptChangedBP when Command differs from the last value pushed, or
	 *  unconditionally when bForce. Backs both the live OnPingChanged relay and RefreshCompanion's
	 *  forced replay/clear so the two paths can't drift into a different dedup rule. */
	void PushCompanionPrompt(ECompanionCommand Command, bool bForce);

	/** Reads the objective subsystem into FHudObjectiveEntry and raises OnObjectivesRebuiltBP. */
	void PushObjectiveList();

	/** Re-measures every objective and raises OnObjectiveDistanceChangedBP for the ones that moved
	 *  by more than the broadcast epsilon. bForce pushes all of them regardless -- RefreshAll needs
	 *  that, since a module built after the last edge has heard nothing and the epsilon test would
	 *  otherwise report every line as unchanged. */
	void PushObjectiveDistances(bool bForce);

	/** The point distances are measured FROM: the player view point, which is what the on-screen
	 *  objective markers use. False when there is no controller to ask -- the caller then reports
	 *  the no-distance sentinel rather than measuring from the world origin. */
	bool GetDistanceOrigin(FVector& OutLocation) const;

	/** Reads name/icon/ammo off Weapon (null-safe) and raises OnActiveWeaponChangedBP. */
	void PushActiveWeapon(AWeaponBase* Weapon);

	/** Reads name/icon/ammo off Weapon (null-safe) and raises OnStowedWeaponChangedBP. The stowed
	 *  slot is read from the weapon component at every call site rather than cached -- nothing here
	 *  subscribes to it, so the component is the only thing that knows the slot emptied. */
	void PushStowedWeapon(AWeaponBase* Weapon);

	/** 1 when Weapon is CachedWeaponComponent's primary slot, 2 when it's the secondary, 0 for null
	 *  or a weapon that matches neither (component gone, or a stale pointer mid-teardown). Shared by
	 *  PushActiveWeapon/PushStowedWeapon rather than each assuming its own slot, since the active
	 *  weapon is not always the primary -- the player can be holding the secondary. */
	int32 ResolveWeaponSlotNumber(const AWeaponBase* Weapon) const;

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

	/** Independent of RebindActiveWeapon's own swap-triggered pushes: UMissionInventorySubsystem::
	 *  GrantAmmo resolves through UWeaponComponent::FindWeaponByAmmoCategory, whose contract is held
	 *  weapon first, THEN the stowed slot, so an ammo pickup routinely tops up the holstered gun with
	 *  no swap involved at all. This is the only signal that catches that case and refreshes the
	 *  stowed row instead of leaving it stale until the next swap. */
	UFUNCTION()
	void HandleStowedAmmoChanged(int32 CurrentAmmo, int32 ReserveAmmo);

	UFUNCTION()
	void HandleObjectivesChanged();

	UFUNCTION()
	void HandleObjectiveLabelChanged(FName Id, const FText& NewLabel);

	UFUNCTION()
	void HandleObjectiveStateChanged(FName Id, EObjectiveState NewState);

	/** AmmoCategory is accepted only to match the now-4-param FOnLootGranted signature and is not
	 *  read here -- OnLootGrantedBP stays 3-param, see its declaration comment. */
	UFUNCTION()
	void HandleLootGranted(ELootType Type, int32 Amount, const FText& Label, EEnemyWeaponAnimType AmmoCategory);

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

	UFUNCTION()
	void HandleCompanionPromptChanged(ECompanionCommand PendingCommand, AActor* PingedTarget);

	UFUNCTION()
	void HandlePingCandidateChanged(bool bHasCandidate, FVector WorldLocation);

	// ---- Cached sources ----
	// All weak: the bridge owns none of these, and a pawn respawn or companion death must read
	// back as "gone" rather than as a dangling bind target.

	TWeakObjectPtr<APlayerController> CachedController;
	TWeakObjectPtr<UHealthComponent> CachedHealth;
	TWeakObjectPtr<UWeaponComponent> CachedWeaponComponent;
	TWeakObjectPtr<AWeaponBase> CachedWeapon;

	/** The carried weapon NOT in hand -- see HandleStowedAmmoChanged for why this needs its own
	 *  ammo subscription independent of CachedWeapon's. */
	TWeakObjectPtr<AWeaponBase> CachedStowedWeapon;

	TWeakObjectPtr<UConsumableInventoryComponent> CachedConsumables;
	TWeakObjectPtr<UCompanionCommandComponent> CachedCommandComponent;
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;
	TWeakObjectPtr<AExtractionPlayer> CachedPlayer;
	TWeakObjectPtr<UObjectiveSubsystem> CachedObjectiveSubsystem;
	TWeakObjectPtr<UMissionInventorySubsystem> CachedInventorySubsystem;

	/** Cleared in EndPlay. Retired as soon as TryBindAll reports everything bound. */
	FTimerHandle BindRetryTimerHandle;

	/** Cleared in EndPlay. Runs for the component's whole life once the objective subsystem is
	 *  bound -- unlike the retry timer there is no terminal state to retire it on. */
	FTimerHandle ObjectiveDistanceTimerHandle;

	/** Cleared in EndPlay. Runs for the component's whole life once the companion command component
	 *  is bound -- readiness can flip from either the active window or the cooldown timing out, and
	 *  neither raises a delegate this component could subscribe to instead. */
	FTimerHandle CoverMeStateTimerHandle;

	/** Baseline the cover-me dedup is measured against. bCoverMeStateEverPushed starts false so the
	 *  very first pass always pushes -- a false/0.f baseline would otherwise match a fresh companion
	 *  read as "not available, 0 remaining" and swallow the initial state entirely. */
	bool bLastCoverMeAvailable = false;
	float LastCoverMeCooldownRemaining = 0.f;
	bool bCoverMeStateEverPushed = false;

	/** Dedup baseline for OnCompanionPromptChangedBP -- OnPingChanged itself re-broadcasts on every
	 *  press even when the command didn't change (re-pinging the same door), so this lives on the
	 *  bridge rather than relying on the source to have already deduped. */
	ECompanionCommand LastPushedPrompt = ECompanionCommand::None;

	/** Dedup baseline for OnPickupFocusChangedBP. bPickupFocusEverPushed starts false so the very
	 *  first pass always pushes, same reasoning as bCoverMeStateEverPushed above. */
	bool bLastPickupFocused = false;
	FText LastPickupItemName;
	bool bPickupFocusEverPushed = false;

	/** Set at the top of EndPlay, before UnbindAll. Every other channel's teardown is silent;
	 *  UnbindPawnSources' prompt/candidate clear is the one pair of pushes that would otherwise call
	 *  into the HUD Blueprint while the world is being torn down (every PIE stop, every level
	 *  transition). A flag checked at the push site, not a defaulted bool parameter on
	 *  UnbindPawnSources -- EndPlay is the only caller that needs it suppressed, and a flag can't be
	 *  got wrong at a call site the way a stray true/false argument could. */
	bool bTearingDown = false;

	/** The kit pushes grenade counts in; nothing to read them back from, so the last value is
	 *  kept here purely so RefreshAll can replay it. */
	int32 LastGrenadeCount = 0;

	/** Latches PushObjectiveList against re-entry -- see the comment at its early return. Without it,
	 *  a BP handler of OnObjectivesRebuiltBP that mutates the objective list would re-enter while the
	 *  outer call is still passing ObjectiveScratch to that same event by reference, and the nested
	 *  Reset()/Reserve() can reallocate the buffer the outer frame is still reading. */
	bool bPushingObjectives = false;

	/** Rebuilt in place on every objective change -- the list is small and this runs on a
	 *  designer-driven event, but the allocation is still worth keeping. */
	TArray<FHudObjectiveEntry> ObjectiveScratch;

	/** Last distance actually pushed per objective id -- the baseline the broadcast epsilon is
	 *  measured against, so a stationary player costs nothing. Rebuilt wholesale by
	 *  PushObjectiveList rather than pruned: a retired id must not leave a baseline behind that
	 *  would swallow the first push if that id is ever registered again. */
	TMap<FName, float> LastPushedDistances;

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
