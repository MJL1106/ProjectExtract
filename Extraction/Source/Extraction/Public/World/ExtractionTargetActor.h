// AExtractionTargetActor -- placeable NPC/object that the player interacts with to trigger
// a Director wave, then performs a completion action (unlock exit, complete level, or
// broadcast). Implements IWorldInteractable so TryWorldInteract picks it up via the generic
// interaction path.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldInteractable.h"
#include "DirectorWaveTypes.h"
#include "ExtractionTargetActor.generated.h"

class UBoxComponent;
class USkeletalMeshComponent;
class ALevelCompletionLiftGate;
class UEnemyDirectorSubsystem;
class UObjectiveSubsystem;

DECLARE_LOG_CATEGORY_EXTERN(LogExtractionTarget, Log, All);

/** Broadcast when the extraction wave completes (any CompletionAction). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnExtractionTargetCompleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnExtractionTargetWaveStarted);

UCLASS()
class EXTRACTION_API AExtractionTargetActor : public AActor, public IWorldInteractable
{
	GENERATED_BODY()

public:
	AExtractionTargetActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- IWorldInteractable ---
	virtual bool CanWorldInteract_Implementation(AActor* Interactor) const override;
	virtual void WorldInteract_Implementation(AActor* Interactor) override;
	virtual FText GetWorldInteractionPrompt_Implementation(AActor* Interactor) const override;

	// --- Delegates ---

	UPROPERTY(BlueprintAssignable, Category = "ExtractionTarget|Events")
	FOnExtractionTargetCompleted OnExtractionTargetCompleted;

	UPROPERTY(BlueprintAssignable, Category = "ExtractionTarget|Events")
	FOnExtractionTargetWaveStarted OnExtractionTargetWaveStarted;

	/** Enables interaction and the reach objective. Idempotent and authority-only. */
	UFUNCTION(BlueprintCallable, Category = "ExtractionTarget|Objectives")
	void ActivateTarget();

	/** Level-flow mode keeps the singular primary objective under ALevelObjectiveFlow ownership. */
	UFUNCTION(BlueprintCallable, Category = "ExtractionTarget|Objectives")
	void SetObjectiveManagedExternally(bool bManagedExternally);

	/** Starts the extraction wave through the same guarded path as the player interact.
	 *  Used when another actor owns the trigger beat (the extractee rescue). Authority-only,
	 *  idempotent -- requires ActivateTarget to have run (flow step entry does). Returns true
	 *  once the wave is running (this call or an earlier one) -- false means StartWave refused
	 *  and the caller must retry, because the rescue interact is one-shot. */
	UFUNCTION(BlueprintCallable, Category = "ExtractionTarget|Objectives")
	bool BeginExtractionExternal();

	/** See bExternalTriggerOnly. Read by the objective flow's wiring audit. */
	UFUNCTION(BlueprintPure, Category = "ExtractionTarget|Interaction")
	bool IsExternalTriggerOnly() const { return bExternalTriggerOnly; }

protected:
	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> SkeletalMesh;

	/** Interaction box -- blocks ECC_Visibility so the player's interact trace can hit it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> InteractionBox;

	// --- Objective Config ---

	/** Objective id registered on BeginPlay (reach objective). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ExtractionTarget|Objectives")
	FName ReachObjectiveId = TEXT("Objective_Reach");

	/** HUD label for the reach objective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ExtractionTarget|Objectives")
	FText ReachObjectiveLabel = NSLOCTEXT("ExtractionTarget", "ReachDefault", "Reach the Extraction Point");

	/** Objective id registered after activation (defence objective). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ExtractionTarget|Objectives")
	FName DefenceObjectiveId = TEXT("Objective_Defend");

	/** HUD label for the defence objective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ExtractionTarget|Objectives")
	FText DefenceObjectiveLabel = NSLOCTEXT("ExtractionTarget", "DefendDefault", "Defend the Position");

	// --- Wave Config ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ExtractionTarget|Wave")
	FDirectorWaveRequest WaveRequest;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ExtractionTarget|Wave")
	EWaveCompletionAction CompletionAction = EWaveCompletionAction::UnlockExit;

	/** Target gate for EWaveCompletionAction::UnlockExit. Null is a validation warning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ExtractionTarget|Wave")
	TObjectPtr<ALevelCompletionLiftGate> LiftGateTarget;

	// --- Interaction ---

	/** HUD prompt shown when the player looks at this actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ExtractionTarget|Interaction")
	FText InteractionPrompt = NSLOCTEXT("ExtractionTarget", "PromptDefault", "Begin Extraction");

	/** Pure wave controller mode: mesh hidden, no interaction -- the wave starts only via
	 *  BeginExtractionExternal (extractee-rescue flow). Set on the placed actor when an
	 *  AExtracteeCompanion owns the trigger beat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ExtractionTarget|Interaction")
	bool bExternalTriggerOnly = false;

private:
	// --- Replicated state ---

	UPROPERTY(ReplicatedUsing = OnRep_bActivated)
	bool bActivated = false;

	UPROPERTY(ReplicatedUsing = OnRep_bCompleted)
	bool bCompleted = false;

	UPROPERTY(ReplicatedUsing = OnRep_bAvailable)
	bool bAvailable = false;

	bool bObjectiveManagedExternally = false;

	UFUNCTION()
	void OnRep_bActivated();

	UFUNCTION()
	void OnRep_bCompleted();

	UFUNCTION()
	void OnRep_bAvailable();

	// --- Director event handlers ---

	UFUNCTION()
	void HandleWaveCompleted(FName WaveId);

	UFUNCTION()
	void HandleWaveBlocked(FName WaveId, FText Reason);

	// --- Internal helpers ---

	void BindDirectorDelegates();
	void UnbindDirectorDelegates();
	void RegisterReachObjective();
	void RemoveReachObjective();
	void RegisterDefenceObjective();
	void RemoveDefenceObjective();
	void PerformCompletionAction();

	UObjectiveSubsystem* GetObjectiveSubsystem() const;
	UEnemyDirectorSubsystem* GetDirectorSubsystem() const;

#if WITH_EDITOR
	void ValidateConfig() const;
#endif
};
