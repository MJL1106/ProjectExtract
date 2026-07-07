// Player-owned component that handles camera-trace pinging and command confirmation
// for the companion. Bridges player input to ACompanionAIController::IssueCommand.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Companion/CompanionCommandTypes.h"
#include "Companion/CompanionTypes.h"
#include "CompanionCommandComponent.generated.h"

class ACompanionCharacter;
class ACompanionAIController;
class AEnemyCharacter;
class UCameraComponent;
class UInputMappingContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPingChanged, ECompanionCommand, PendingCommand, AActor*, PingedTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCompanionModeChangedRelay, ECompanionMode, NewMode);

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

	/** Cycle the companion's mode Normal -> Combat -> Stealth -> Normal. Call once per press of IA_CompanionModeToggle. */
	UFUNCTION(BlueprintCallable, Category = "Companion|Mode")
	void CycleCompanionMode();

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

private:
	ECompanionCommand PendingCommand = ECompanionCommand::None;

	TWeakObjectPtr<AActor> PendingTarget;

	/** Lazily resolved on first use; valid for the lifetime of the level. */
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;

	/** Lazily resolved on first ping; avoids FindComponentByClass every press. */
	TWeakObjectPtr<UCameraComponent> CachedCamera;

	ACompanionAIController* GetCompanionController();
	ACompanionCharacter* ResolveCompanion();

	void ConfirmTakedown(ETakedownMethod Method);
	void ClearPending();

	UFUNCTION()
	void HandleCompanionModeChanged(ECompanionMode NewMode);

	/** Adds/removes TakedownPromptContext on the local player. Idempotent. */
	void SetPromptContextRegistered(bool bRegister);

	bool bPromptContextRegistered = false;
};
