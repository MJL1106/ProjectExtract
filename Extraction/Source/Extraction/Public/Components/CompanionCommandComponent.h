// Player-owned component that handles camera-trace pinging and command confirmation
// for the companion. Bridges player input to ACompanionAIController::IssueCommand.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Companion/CompanionCommandTypes.h"
#include "CompanionCommandComponent.generated.h"

class ACompanionCharacter;
class ACompanionAIController;
class AEnemyCharacter;
class UCameraComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPingChanged, ECompanionCommand, PendingCommand, AActor*, PingedTarget);

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

	// ---- Getters ----

	UFUNCTION(BlueprintPure, Category = "Companion|Command")
	AActor* GetPingedTarget() const { return PendingTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "Companion|Command")
	ECompanionCommand GetPendingCommand() const { return PendingCommand; }

	// ---- Delegates ----

	/** Broadcast whenever the pending target or command changes (including clears). */
	UPROPERTY(BlueprintAssignable, Category = "Companion|Command")
	FOnPingChanged OnPingChanged;

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
};
