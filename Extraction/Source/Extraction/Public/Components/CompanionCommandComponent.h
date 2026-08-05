// Player-owned component that handles camera-trace pinging and command confirmation
// for the companion. Bridges player input to ACompanionAIController::IssueCommand.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Companion/CompanionCommandTypes.h"
#include "Companion/CompanionTypes.h"
#include "CompanionCommandComponent.generated.h"

class ACompanionCharacter;
class ACompanionAIController;
class AEnemyCharacter;
class UCameraComponent;
class UHealthComponent;
class UInputMappingContext;
class USoundBase;
struct FHitResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPingChanged, ECompanionCommand, PendingCommand, AActor*, PingedTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCompanionModeChangedRelay, ECompanionMode, NewMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnModeMenuChanged, bool, bOpen);

/** Edge-only: "would a ping land on something right now, and where" -- the aiming-state probe.
 *  Distinct from OnPingChanged, which only fires once a ping is actually committed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPingCandidateChanged, bool, bHasCandidate, FVector, WorldLocation);

DECLARE_LOG_CATEGORY_EXTERN(LogCompanionCommand, Log, All);

UCLASS(ClassGroup = "Companion", meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UCompanionCommandComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCompanionCommandComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ---- Tuning ----

	/** Maximum distance (cm) for the ping camera trace. */
	UPROPERTY(EditAnywhere, Category = "Companion|Command")
	float PingTraceRange = 6000.f;

	/** Search radius (cm) around the ping impact for cover-point candidates. */
	UPROPERTY(EditAnywhere, Category = "Companion|Command")
	float CoverPingRadius = 400.f;

	/** How often the "can ping" probe re-traces while idle. Matches UExtractionHudBridgeComponent's
	 *  timer idiom: EditDefaultsOnly interval + a static constexpr floor, since SetTimer never fires
	 *  at 0. ~10Hz is enough for the aiming-state ring/tag to feel responsive without tracing every
	 *  frame. */
	UPROPERTY(EditAnywhere, Category = "Companion|Command", meta = (ClampMin = "0.02"))
	float PingCandidateInterval = 0.1f;

	/** Duration (seconds) of the covering-fire sustained-peek window. */
	UPROPERTY(EditAnywhere, Category = "Companion|CoveringFire", meta = (ClampMin = "1.0"))
	float CoveringFireDuration = 7.f;

	/** Seconds after the companion last held a combat target that Cover Me still counts as
	 *  "in combat". The blackboard target only exists while the companion currently SEES an
	 *  enemy, which is far stricter than the player's sense of being in a firefight. */
	UPROPERTY(EditAnywhere, Category = "Companion|CoveringFire", meta = (ClampMin = "0.0"))
	float CoverMeCombatRecencyWindow = 10.f;

	/** Registered at TakedownPromptContextPriority ONLY while a takedown ping is pending. Maps the
	 *  G/V confirm keys so they consume — the kit's grenade (G) and melee (V) can't fire while the
	 *  takedown prompt is on screen. Assign IMC_CompanionTakedownPrompt in BP. */
	UPROPERTY(EditAnywhere, Category = "Companion|Command")
	TObjectPtr<UInputMappingContext> TakedownPromptContext;

	/** Must exceed the gameplay contexts' priority (0) for the consume to block them. */
	UPROPERTY(EditAnywhere, Category = "Companion|Command")
	int32 TakedownPromptContextPriority = 10;

	/** Played 2D when a ping lands on a commandable target (breach/search/loot/takedown). */
	UPROPERTY(EditAnywhere, Category = "Companion|Command")
	TObjectPtr<USoundBase> PingConfirmSound;

	/** Played 2D when a ping press finds nothing commandable (miss, dead target, suppressed). */
	UPROPERTY(EditAnywhere, Category = "Companion|Command")
	TObjectPtr<USoundBase> PingFailSound;

	/** Registered at ModeSelectContextPriority ONLY while the mode picker is open. Maps 1/2/3 to the
	 *  three mode-select InputActions and consumes them, so number-key weapon switching can't fire
	 *  while the picker is up. Assign IMC_CompanionModeSelect in BP. */
	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	TObjectPtr<UInputMappingContext> ModeSelectContext;

	/** Must exceed the gameplay contexts' priority (0) so the picker's consuming number binds win. */
	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	int32 ModeSelectContextPriority = 10;

	/** Seconds the picker stays open with no input before auto-closing. <= 0 disables the timeout.
	 *  Kept generous: the timer is one-shot and never reset by input, so a player who opens the
	 *  picker mid-firefight and hesitates gets a completely silent no-op on the follow-up press. */
	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	float ModeMenuTimeout = 6.0f;

	// ---- Actions ----

	/** Camera-trace ping. Call once per press of IA_CompanionPing. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Command")
	void IssuePing();

	/** Confirm a queued Takedown command with Knife method. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Command")
	void ConfirmTakedownKnife();

	/** Confirm a queued Takedown command with Shoot method. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Command")
	void ConfirmTakedownShoot();

	/** Confirm a queued Breach command. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Command")
	void ConfirmBreach();

	/** Confirm a queued Loot command (rides the same confirm key as Breach). */
	UFUNCTION(BlueprintCallable, Category = "Companion|Command")
	void ConfirmLoot();

	/** Confirm a queued Explore command (rides the same confirm key as Breach). */
	UFUNCTION(BlueprintCallable, Category = "Companion|Command")
	void ConfirmExplore();

	/** Confirm a queued TakeCover command (rides the same confirm key as Breach). */
	UFUNCTION(BlueprintCallable, Category = "Companion|Command")
	void ConfirmTakeCover();

	/** Cycle the companion's mode Normal -> Combat -> Stealth -> Normal. Retained for gamepad/debug use;
	 *  X now opens the picker instead. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Mode")
	void CycleCompanionMode();

	/** Toggle the mode picker open/closed. Bound to X (IA_CompanionModeToggle). */
	UFUNCTION(BlueprintCallable, Category = "Companion|Mode")
	void ToggleModeMenu();

	/** Open the mode picker: registers ModeSelectContext + starts the auto-close timer. No-op if
	 *  already open or no companion in level. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Mode")
	void OpenModeMenu();

	/** Close the mode picker: removes ModeSelectContext + clears the timer. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Mode")
	void CloseModeMenu();

	/** Set the companion's mode directly, then close the picker. Ignored unless the picker is open. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Mode")
	void SelectCompanionMode(ECompanionMode Mode);

	/** Trigger a covering-fire window. Requires the picker to be open and a live combat target.
	 *  Closes the menu WITHOUT changing the persistent ECompanionMode. */
	UFUNCTION(BlueprintCallable, Category = "Companion|CoveringFire")
	void TriggerCoverMe();

	/** True while the companion has a live combat target and covering fire can be activated. */
	UFUNCTION(BlueprintPure, Category = "Companion|CoveringFire")
	bool IsCoverMeAvailable();

	/** True while the mode picker is open. */
	UFUNCTION(BlueprintPure, Category = "Companion|Mode")
	bool IsModeMenuOpen() const { return bModeMenuOpen; }

	/** Current companion mode. Resolves the companion lazily; Normal if not yet spawned. */
	UFUNCTION(BlueprintPure, Category = "Companion|Mode")
	ECompanionMode GetCompanionMode();

	// ---- Getters ----

	UFUNCTION(BlueprintPure, Category = "Companion|Command")
	AActor* GetPingedTarget() const { return PendingTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "Companion|Command")
	ECompanionCommand GetPendingCommand() const { return PendingCommand; }

	/** World location of the pending cover point (TakeCover only). Used by the ping marker
	 *  when there is no target actor to track. Valid while PendingCommand == TakeCover. */
	UFUNCTION(BlueprintPure, Category = "Companion|Command")
	FVector GetPendingCoverLocation() const { return PendingCoverLocation; }

	/** Resolves the world location the ping marker should track this frame. False when no ping is
	 *  live. TakeCover resolves to the cover location; every other command resolves through the
	 *  ping anchor captured at ping time (actor + local offset, or a bare world point). */
	UFUNCTION(BlueprintPure, Category = "Companion|Command")
	bool GetPingWorldLocation(FVector& OutLocation) const;

	/** Returns the resolved companion, or null if not yet spawned. */
	UFUNCTION(BlueprintPure, Category = "Companion|Command")
	ACompanionCharacter* GetCompanion() { return ResolveCompanion(); }

	/** Cached result of the last "can ping" probe tick. Lets a late binder (the HUD bridge's
	 *  RefreshAll) replay the live aiming state instead of waiting for the next edge, since
	 *  OnPingCandidateChanged only fires on change. */
	UFUNCTION(BlueprintPure, Category = "Companion|Command")
	bool HasPingCandidate() const { return bLastPingCandidate; }

	UFUNCTION(BlueprintPure, Category = "Companion|Command")
	FVector GetPingCandidateLocation() const { return LastPingCandidateLocation; }

	// ---- Delegates ----

	/** Broadcast whenever the pending target or command changes (including clears). */
	UPROPERTY(BlueprintAssignable, Category = "Companion|Command")
	FOnPingChanged OnPingChanged;

	/** Broadcast on change only -- see FOnPingCandidateChanged. */
	UPROPERTY(BlueprintAssignable, Category = "Companion|Command")
	FOnPingCandidateChanged OnPingCandidateChanged;

	/** Relay of the companion's OnModeChanged — HUD widgets subscribe here (player-side, same
	 *  pattern as OnPingChanged) instead of hunting for the companion actor themselves. */
	UPROPERTY(BlueprintAssignable, Category = "Companion|Mode")
	FOnCompanionModeChangedRelay OnCompanionModeChanged;

	/** Broadcast when the mode picker opens (true) or closes (false). The HUD chip subscribes to
	 *  morph into / out of the numbered list. */
	UPROPERTY(BlueprintAssignable, Category = "Companion|Mode")
	FOnModeMenuChanged OnModeMenuChanged;

private:
	ECompanionCommand PendingCommand = ECompanionCommand::None;

	TWeakObjectPtr<AActor> PendingTarget;

	/** Cached ping impact for TakeCover re-resolve at confirm time. */
	FVector PendingCoverPingImpact = FVector::ZeroVector;
	FVector PendingCoverPingNormal = FVector::ZeroVector;

	/** Resolved cover point location for the ping marker (TakeCover). */
	FVector PendingCoverLocation = FVector::ZeroVector;

	/** Ping impact anchor for the marker, captured at ping time: local-space offset from
	 *  PendingTarget (via CapturePingAnchor) when a target exists, or the raw world point when
	 *  it doesn't. Resolved back to world space by GetPingWorldLocation. */
	FVector PendingPingOffset = FVector::ZeroVector;
	bool bPendingPingOffsetValid = false;

	/** Lazily resolved on first use; valid for the lifetime of the level. */
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;

	/** Lazily resolved on first ping; avoids FindComponentByClass every press. */
	TWeakObjectPtr<UCameraComponent> CachedCamera;

	/** Lazily resolved; avoids FindComponentByClass per EvaluateCoverMe call (runs every tick
	 *  while the picker is open). Used for the player-damage recency combat signal. */
	TWeakObjectPtr<UHealthComponent> CachedOwnerHealth;

	ACompanionAIController* GetCompanionController();
	ACompanionCharacter* ResolveCompanion();

	/** True while the companion walks an authored route (BB_RouteActive) — breach is suppressed
	 *  so a command can't yank it off the scripted leg. */
	bool IsCompanionRouteActive();

	void ConfirmTakedown(ETakedownMethod Method);
	void ClearPending();

	/** Captures the ping impact as the marker anchor: local-space offset from TargetActor when
	 *  valid, raw world point otherwise. Called once from each ping-accept branch in IssuePing. */
	void CapturePingAnchor(AActor* TargetActor, const FVector& ImpactPoint);

	/** Factored out of IssuePing: the two-channel (Visibility, then Interact) trace that resolves
	 *  what the crosshair is over, picking the nearer hit. Returns false when nothing valid was hit.
	 *  Shared with the "can ping" probe so it can never answer a different question than a real ping
	 *  press would -- only IssuePing's command-priority resolution stays out of this function. */
	bool ResolvePingHit(FHitResult& OutHit);

	/** Whether a ping landing on this actor would resolve to any command at all. Mirrors the
	 *  eligibility half of IssuePing's priority branches and nothing else: the commit half fuses
	 *  pending-state mutation, the prompt input-context registration, OnPingChanged and the audio
	 *  cue into each branch, so a probe cannot share that path without a rewrite of the command
	 *  pipeline. Kept deliberately coarse -- it answers "pingable", not "which command". IssuePing
	 *  itself does not call this and stays exactly as verified; it could adopt this predicate later
	 *  if the pipeline is ever split into resolve/commit phases, so the duplication of the
	 *  eligibility half is visible here rather than silently drifting apart. */
	bool IsActorPingable(const AActor* Actor) const;

	/** True (with OutLocation set to the impact point) when a ping pressed right now would find
	 *  something to consider. False while a ping is already pending -- the marker is already in its
	 *  placed state and the probe must not contest it -- or before the companion has spawned, since
	 *  a candidate with no command to send is not an aiming state worth showing. */
	bool EvaluatePingCandidate(FVector& OutLocation);

	/** (Re)arms the ping-candidate probe timer unless it is already running. */
	void StartPingCandidateUpdates();

	void UpdatePingCandidate();

	/** Audible ping feedback — confirm blip on an accepted ping, fail blip on a dead one.
	 *  Only IssuePing calls this; ClearPending stays silent (it also runs on command completion). */
	void PlayPingFeedback(bool bAccepted) const;

	UFUNCTION()
	void HandleCompanionModeChanged(ECompanionMode NewMode);

	/** Clears commanded cover hold and any pending cover reissue timer. Called before
	 *  IssueCommand for commands that break cover (Breach, Explore, Loot, Takedown) so
	 *  BTService_UpdateCompanionState's UnCrouch gate sees the hold cleared. */
	void ClearCommandedCoverIfActive();

	/** Adds/removes TakedownPromptContext on the local player. Idempotent. */
	void SetPromptContextRegistered(bool bRegister);

	/** Adds/removes ModeSelectContext on the local player. Idempotent. */
	void SetModeSelectContextRegistered(bool bRegister);

	/** Auto-close timer callback — closes the picker after ModeMenuTimeout of no input. */
	void OnModeMenuTimeout();

	/** Single gate for Cover Me so IsCoverMeAvailable and TriggerCoverMe can never disagree. */
	enum class ECoverMeGate : uint8 { Ready, NoCompanion, NoCombat, AlreadyActive, OnCooldown };
	ECoverMeGate EvaluateCoverMe(AActor** OutCombatTarget = nullptr, ACompanionCharacter** OutCompanion = nullptr);

	/** Test if the companion's current cover can peek-shoot a given target. */
	bool CanPeekShootTarget(ACompanionCharacter* Companion, AActor* Target);

	bool bPromptContextRegistered = false;
	bool bModeSelectContextRegistered = false;
	bool bModeMenuOpen = false;
	FTimerHandle ModeMenuTimeoutHandle;

	/** Deferred re-issue timer for re-pinging cover while a hold is active. The clear (frame N)
	 *  and the re-issue (frame N+1) must be in separate frames so the BT decorator sees a
	 *  genuine None->TakeCover edge. Cleared in EndPlay. */
	FTimerHandle CoverReissueTimerHandle;

	/** Ping-candidate probe state -- see EvaluatePingCandidate/UpdatePingCandidate. Starts at
	 *  "no candidate": nothing is under the crosshair until the first probe tick runs. */
	bool bLastPingCandidate = false;
	FVector LastPingCandidateLocation = FVector::ZeroVector;

	/** Cleared in EndPlay. Runs for the component's whole life -- there is no terminal state to
	 *  retire it on, unlike the bind-retry timers elsewhere in this module. */
	FTimerHandle PingCandidateTimerHandle;

	/** SetTimer never fires on a rate of zero -- floor the configured interval above it. */
	static constexpr float MinPingCandidateInterval = 0.02f;

	/** Below this, the trace hit moved by less than aim jitter -- the probe's trace is otherwise
	 *  deterministic for a stationary aim, so this exists purely to absorb float noise, not to
	 *  throttle a genuinely moving reticle. */
	static constexpr float PingCandidateLocationEpsilon = 2.f;
};
