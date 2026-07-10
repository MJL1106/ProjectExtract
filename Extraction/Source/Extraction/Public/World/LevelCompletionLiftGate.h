// ALevelCompletionLiftGate -- replicated interaction gate placed near the marketplace lift.
// The extraction target calls UnlockExit() after wave completion (EWaveCompletionAction::UnlockExit).
// Before unlock, interaction tells the player to clear enemies. After unlock, interaction
// triggers AExtractionGameMode::CompleteLevel().

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldInteractable.h"
#include "LevelCompletionLiftGate.generated.h"

class UBoxComponent;
class UObjectiveSubsystem;

DECLARE_LOG_CATEGORY_EXTERN(LogLiftGate, Log, All);

UCLASS()
class EXTRACTION_API ALevelCompletionLiftGate : public AActor, public IWorldInteractable
{
	GENERATED_BODY()

public:
	ALevelCompletionLiftGate();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- IWorldInteractable ---
	virtual bool CanWorldInteract_Implementation(AActor* Interactor) const override;
	virtual void WorldInteract_Implementation(AActor* Interactor) override;
	virtual FText GetWorldInteractionPrompt_Implementation(AActor* Interactor) const override;

	/** Unlock the exit gate. Called by AExtractionTargetActor on wave completion.
	 *  Authority-only, idempotent. Registers the "use the lift" objective. */
	UFUNCTION(BlueprintCallable, Category = "LevelCompletion")
	void UnlockExit();

protected:
	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Interaction box. Kept tight against the lift wall/button so it does not eat bullets
	 *  or AI sight traces over a wide area (blocks ECC_Visibility for the interact trace). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> InteractionBox;

	// --- Designer Config ---

	/** Marketplace lift actor. Used ONLY for the objective marker location (never modified).
	 *  When null the gate falls back to its own location with a Warning log. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "LevelCompletion")
	TObjectPtr<AActor> LiftActor;

	/** HUD prompt shown while locked (enemies remain). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LevelCompletion|Prompts")
	FText LockedPrompt = NSLOCTEXT("LiftGate", "LockedPrompt", "Lift (Locked)");

	/** HUD prompt shown after unlock (ready to leave). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LevelCompletion|Prompts")
	FText UnlockedPrompt = NSLOCTEXT("LiftGate", "UnlockedPrompt", "Use Lift");

	/** Toast message broadcast when the player interacts while locked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LevelCompletion|Prompts")
	FText LockedInteractionMessage = NSLOCTEXT("LiftGate", "LockedMsg", "Eliminate remaining enemies");

	/** Objective id registered on unlock ("Use the lift"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LevelCompletion|Objectives")
	FName UseLiftObjectiveId = TEXT("Objective_UseLift");

	/** HUD label for the use-lift objective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LevelCompletion|Objectives")
	FText UseLiftObjectiveLabel = NSLOCTEXT("LiftGate", "UseLift", "Use the Lift");

private:
	UPROPERTY(ReplicatedUsing = OnRep_bUnlocked)
	bool bUnlocked = false;

	/** Set once the objective has been registered, to guard EndPlay cleanup. */
	bool bObjectiveRegistered = false;

	UFUNCTION()
	void OnRep_bUnlocked();

	/** Shared helper: registers (or re-registers) the use-lift objective. */
	void RegisterUseLiftObjective();

	/** Removes the use-lift objective if it was registered. */
	void RemoveUseLiftObjective();

	FVector GetObjectiveLocation() const;
	UObjectiveSubsystem* GetObjectiveSubsystem() const;
};
