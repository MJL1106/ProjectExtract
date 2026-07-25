#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Enemy/Director/DirectorWaveTypes.h"
#include "World/ExtractionTargetActor.h"
#include "LevelObjectiveFlow.generated.h"

class ACompanionCharacter;
class ACompanionRoute;
class ADoorBase;
class AEnemyCharacter;
class AExtracteeCompanion;
class AExtractionTargetActor;
class ALevelCompletionLiftGate;
class ALootContainer;

UENUM(BlueprintType)
enum class ELevelObjectiveStep : uint8
{
	Inactive,
	BreachRoofDoor,
	FollowCompanionDownstairs,
	BreachStairwellDoor,
	ClearRoom1,
	FindOfficeKeycard,
	UnlockStairwellDoor,
	SwitchCompanionToStealth,
	FirstDoubleTakedown,
	SecondDoubleTakedown,
	ReachExtractionTarget,
	DefendPosition,
	UseLift,
	Completed,
};

enum class ELevelObjectiveEvent : uint8
{
	RoofDoorOpened,
	RouteCompleted,
	StairwellDoorOpened,
	Room1Cleared,
	KeycardLooted,
	ExitDoorOpened,
	Room2DoorOpened,
	FirstPairCleared,
	SecondPairCleared,
	ExtractionStarted,
	ExtractionCompleted,
	LiftCompleted,
};

/** Pure transition core shared by the placed flow actor and automation coverage. */
struct EXTRACTION_API FLevelObjectiveStateMachine
{
	bool Activate();
	bool Advance(ELevelObjectiveEvent Event);
	static ELevelObjectiveStep GetNextStep(ELevelObjectiveStep Step, ELevelObjectiveEvent Event);
	ELevelObjectiveStep GetCurrentStep() const { return CurrentStep; }

private:
	ELevelObjectiveStep CurrentStep = ELevelObjectiveStep::Inactive;
};

/** DemoMap's explicit, non-ticking, server-authoritative objective sequence. */
UCLASS(Blueprintable)
class EXTRACTION_API ALevelObjectiveFlow : public AActor
{
	GENERATED_BODY()

public:
	ALevelObjectiveFlow();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Objective Flow")
	bool ActivateFlow();

	UFUNCTION(BlueprintPure, Category = "Objective Flow")
	ELevelObjectiveStep GetCurrentStep() const { return CurrentStep; }

	UFUNCTION(BlueprintCallable, Category = "Objective Flow")
	void NotifyLiftCompleted();
#if WITH_DEV_AUTOMATION_TESTS
	void TestSetCurrentStep(ELevelObjectiveStep Step) { CurrentStep = Step; }
	void TestEvaluateCurrentEnemyStep() { RefreshRecordedEnemyDeaths(); EvaluateCurrentEnemyStep(); }
	void TestActivateOptionalSupplies() { ActivateOptionalSupplies(); }
	void TestRefreshOptionalSupplies() { UpdateOptionalSupplies(); }
	bool TestValidateReferences() const { return ValidateReferences(); }
	bool TestIsOptionalSuppliesActive() const { return bOptionalSuppliesActive; }
	ALootContainer* TestGetOptionalTarget() const { return CurrentOptionalTarget; }
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Flow")
	bool bAutoActivate = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Roof")
	TObjectPtr<ADoorBase> RoofDoor;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Roof")
	TObjectPtr<ACompanionRoute> DownstairsRoute;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Roof")
	TObjectPtr<ADoorBase> StairwellBreachDoor;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 1")
	TArray<TObjectPtr<AEnemyCharacter>> Room1Enemies;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 1")
	TObjectPtr<ALootContainer> KeycardContainer;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 1")
	TObjectPtr<ADoorBase> Room1ExitDoor;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 2")
	TObjectPtr<ADoorBase> Room2EntryDoor;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 2")
	TArray<TObjectPtr<AEnemyCharacter>> FirstTakedownPair;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 2")
	TArray<TObjectPtr<AEnemyCharacter>> SecondTakedownPair;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Room 2")
	TArray<TObjectPtr<ALootContainer>> SupplyCrates;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Extraction")
	TObjectPtr<AExtractionTargetActor> ExtractionTarget;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Extraction")
	TObjectPtr<ALevelCompletionLiftGate> LiftGate;

	/** The armed extraction VIP. When set: the Reach step targets it, its rescue starts the wave,
	 *  and checkpoint fast-forwards past the rescue arm it directly.
	 *  Either this or ExtractionTarget must be set — the VIP is the modern owner of the beat, the
	 *  target actor the legacy placeholder. With no ExtractionTarget the flow runs the wave itself
	 *  from ExtractionWave/ExtractionCompletionAction below. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Extraction")
	TObjectPtr<AExtracteeCompanion> Extractee;

	/** Defence wave started by the rescue. Used ONLY when there is no ExtractionTarget actor to
	 *  own it — with one present its own WaveRequest wins, so the config never lives in two places. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Flow|Extraction")
	FDirectorWaveRequest ExtractionWave;

	/** What finishing ExtractionWave does. UnlockExit unlocks LiftGate above. Ignored when an
	 *  ExtractionTarget actor owns the wave. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Flow|Extraction")
	EWaveCompletionAction ExtractionCompletionAction = EWaveCompletionAction::UnlockExit;

	/** Steps that count as checkpoints — advancing INTO one grants the companion a full heal,
	 *  applied once it is out of combat (immediately if already calm), and records the step to
	 *  the GameInstance so a level restart resumes here instead of at the start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Flow|Checkpoints")
	TArray<ELevelObjectiveStep> CheckpointSteps;

	/** Resume point per checkpoint step — player teleports here on a checkpoint restart (the
	 *  companion lands beside it). Wire a TargetPoint for every CheckpointSteps entry. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Flow|Checkpoints")
	TMap<ELevelObjectiveStep, TObjectPtr<AActor>> CheckpointSpawns;

private:
	static const FName PrimaryObjectiveId;
	static const FName OptionalSuppliesObjectiveId;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentStep)
	ELevelObjectiveStep CurrentStep = ELevelObjectiveStep::Inactive;
	UPROPERTY(ReplicatedUsing = OnRep_OptionalSuppliesActive)
	bool bOptionalSuppliesActive = false;
	UPROPERTY(ReplicatedUsing = OnRep_PresentationRevision)
	TObjectPtr<AActor> CurrentPrimaryTarget;
	UPROPERTY(ReplicatedUsing = OnRep_PresentationRevision)
	TObjectPtr<ALootContainer> CurrentOptionalTarget;
	UPROPERTY(ReplicatedUsing = OnRep_PresentationRevision)
	uint16 PresentationRevision = 0;

	/** Spawn-time centroid of Room1Enemies — the ClearRoom1 marker pins to this fixed area
	 *  instead of following living enemies around the room. Zero = fall back to enemy-follow.
	 *  Auto-computed at flow activation when left Zero; set by hand to override the anchor. */
	UPROPERTY(EditAnywhere, Replicated, Category = "Objective Flow|Room 1")
	FVector Room1AreaLocation = FVector::ZeroVector;

	/** Where the rescue happened — the ground the player is told to hold. Captured when the wave
	 *  starts, because the VIP walks away from it the moment he can follow, and a "Defend the
	 *  position" marker that trails your own squadmate around the room points at nothing. */
	UPROPERTY(Replicated)
	FVector DefendAreaLocation = FVector::ZeroVector;

	TSet<TWeakObjectPtr<ALootContainer>> CompletedSupplyCrates;
	TSet<int32> Room1DeadIndices;
	TSet<int32> FirstPairDeadIndices;
	TSet<int32> SecondPairDeadIndices;

	/** Lazily-resolved level companion for checkpoint heals. */
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;
	/** Poll while the companion is in combat/DBNO — the heal applies once it calms. */
	FTimerHandle CheckpointHealPollHandle;

	void TryApplyCompanionCheckpointHeal();

	// --- Checkpoint restart (GameInstance-recorded; resume applied at flow activation) ---

	/** If the GameInstance holds a checkpoint for this level, fast-forward the flow + world to it
	 *  and start the spawn-teleport attempt. Called once from ActivateFlow. */
	void TryResumeFromCheckpoint();

	/** Applies every completed step's end-state (doors opened, cleared enemy groups removed,
	 *  keycard re-granted, entry side effects) and sets CurrentStep to TargetStep. */
	void FastForwardToStep(ELevelObjectiveStep TargetStep);

	/** Teleports player + companion to the checkpoint spawn once both exist (short poll — player
	 *  spawn order vs level BeginPlay isn't guaranteed). */
	void TryApplyCheckpointSpawn();

	FTimerHandle CheckpointSpawnRetryHandle;
	int32 CheckpointSpawnRetries = 0;

	bool ValidateReferences() const;

	/** Non-fatal wiring audit run at activation: the flow still starts, but a mis-wired level
	 *  instance (EditInstanceOnly refs left null) fails silently in play, so name it in the log. */
	void WarnOnSuspectWiring() const;

	/** Stable actor to hang the primary marker on when a step's own target resolves to null.
	 *  Null = the step genuinely has nothing to point at and the marker is dropped. */
	const AActor* ResolveMarkerFallbackAnchor() const;
	void BindDelegates();
	void UnbindDelegates();
	void Advance(ELevelObjectiveEvent Event);
	void UpdatePrimaryObjective();
	void EvaluateCurrentEnemyStep();
	void RefreshRecordedEnemyDeaths();
	void ActivateOptionalSupplies();
	void UpdateOptionalSupplies();
	bool IsEnemyGroupDead(const TArray<TObjectPtr<AEnemyCharacter>>& Enemies, const TSet<int32>& DeadIndices) const;
	AActor* FindFirstLivingEnemy(const TArray<TObjectPtr<AEnemyCharacter>>& Enemies) const;
	ALootContainer* FindNearestUnlootedSupply(const FVector& Origin) const;

	UFUNCTION()
	void HandleDoorOpened(AActor* Door);
	UFUNCTION()
	void HandleRouteCompleted(bool bAborted);
	UFUNCTION()
	void HandleTrackedEnemyDeath();
	UFUNCTION()
	void HandleTrackedEnemyDestroyed(AActor* DestroyedActor);
	UFUNCTION()
	void HandleLootCompleted(ALootContainer* Container, AActor* Looter);
	UFUNCTION()
	void HandleSupplyDestroyed(AActor* DestroyedActor);
	UFUNCTION()
	void HandleExtracteeRescued();

	// Direct Director hookup, used only on the no-ExtractionTarget path (the VIP owns the beat and
	// there is no target actor listening for the Director's completion).
	UFUNCTION()
	void HandleDirectorWaveCompleted(FName WaveId);
	UFUNCTION()
	void HandleDirectorWaveBlocked(FName WaveId, FText Reason);

	/** True when this flow, not an ExtractionTarget actor, owns the defence wave. */
	bool OwnsExtractionWave() const { return !IsValid(ExtractionTarget); }

	/** ExtractionCompletionAction, applied when the flow owns the wave. */
	void PerformExtractionCompletionAction();

	/** Starts (or retries) the extraction wave after the one-shot rescue. StartWave can refuse
	 *  (another wave active, bad config) and the rescue interact can't be repeated, so a refusal
	 *  arms a short looping retry instead of soft-locking the flow. */
	void TryStartExtractionWave();

	FTimerHandle ExtractionWaveRetryHandle;
	UFUNCTION()
	void HandleExtractionStarted();
	UFUNCTION()
	void HandleExtractionCompleted();
	UFUNCTION()
	void OnRep_CurrentStep();
	UFUNCTION()
	void OnRep_OptionalSuppliesActive();
	UFUNCTION()
	void OnRep_PresentationRevision();
};