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
class UInputMappingContext;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPingChanged, ECompanionCommand, PendingCommand, AActor*, PingedTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCompanionModeChangedRelay, ECompanionMode, NewMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnModeMenuChanged, bool, bOpen);

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

	/** Seconds the picker stays open with no input before auto-closing. <= 0 disables the timeout. */
	UPROPERTY(EditAnywhere, Category = "Companion|Mode")
	float ModeMenuTimeout = 3.0f;

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

	/** Returns the resolved companion, or null if not yet spawned. */
	UFUNCTION(BlueprintPure, Category = "Companion|Command")
	ACompanionCharacter* GetCompanion() { return ResolveCompanion(); }

	// ---- Delegates ----

	/** Broadcast whenever the pending target or command changes (including clears). */
	UPROPERTY(BlueprintAssignable, Category = "Companion|Command")
	FOnPingChanged OnPingChanged;

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

	/** Lazily resolved on first use; valid for the lifetime of the level. */
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;

	/** Lazily resolved on first ping; avoids FindComponentByClass every press. */
	TWeakObjectPtr<UCameraComponent> CachedCamera;

	ACompanionAIController* GetCompanionController();
	ACompanionCharacter* ResolveCompanion();

	/** True while the companion walks an authored route (BB_RouteActive) — breach is suppressed
	 *  so a command can't yank it off the scripted leg. */
	bool IsCompanionRouteActive();

	void ConfirmTakedown(ETakedownMethod Method);
	void ClearPending();

	/** Audible ping feedback — confirm blip on an accepted ping, fail blip on a dead one.
	 *  Only IssuePing calls this; ClearPending stays silent (it also runs on command completion). */
	void PlayPingFeedback(bool bAccepted) const;

	UFUNCTION()
	void HandleCompanionModeChanged(ECompanionMode NewMode);

	/** Adds/removes TakedownPromptContext on the local player. Idempotent. */
	void SetPromptContextRegistered(bool bRegister);

	/** Adds/removes ModeSelectContext on the local player. Idempotent. */
	void SetModeSelectContextRegistered(bool bRegister);

	/** Auto-close timer callback — closes the picker after ModeMenuTimeout of no input. */
	void OnModeMenuTimeout();

	bool bPromptContextRegistered = false;
	bool bModeSelectContextRegistered = false;
	bool bModeMenuOpen = false;
	FTimerHandle ModeMenuTimeoutHandle;
};
