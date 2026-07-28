// AObjectiveStep — one placed objective beat. A designer builds a mission by dropping these into
// the level, picking a completion condition from the dropdown, wiring the one or two level actors
// that condition needs, and pointing NextStep at the following beat. The chain of placed actors IS
// the mission script: no per-beat C++, no central flow actor.
//
// Each step owns its HUD objective line and world marker (registered with UObjectiveSubsystem under
// its StepId), an optional list of side effects fired on activation or completion, and an optional
// checkpoint record. Non-ticking — everything is delegate- or timer-driven.
//
// Checkpoint resume is derived, not authored: each step already declares what "done" means through
// its condition, so a fast-forward replays that end-state (door opened, enemies gone, keycard back
// in the mission inventory) without a second body of restore code to keep in sync.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Companion/CompanionTypes.h"
#include "Enemy/Director/DirectorWaveTypes.h"
#include "ObjectiveStep.generated.h"

class ACompanionCharacter;
class ACompanionRoute;
class ADoorBase;
class AEnemyCharacter;
class AExtracteeCompanion;
class ALevelCompletionLiftGate;
class ALootContainer;
class AObjectiveStep;
class UObjectiveSubsystem;
class USphereComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogObjectiveStep, Log, All);

/** What finishes this beat. Each entry has its own payload fields below, shown only when picked. */
UENUM(BlueprintType)
enum class EObjectiveCondition : uint8
{
	/** Player enters CompletionRadius. */
	ReachLocation,
	/** The mission inventory records RequiredKeycardId. */
	AcquireKeycard,
	/** Every entry in TrackedEnemies is dead or destroyed. */
	EnemiesDead,
	/** TrackedDoor reaches its fully-open state. */
	DoorOpened,
	/** TrackedContainers are emptied (all, or any one — see bRequiresAllContainers). */
	ContainerLooted,
	/** TrackedRoute finishes without being aborted. */
	RouteCompleted,
	/** The player interacts with TrackedInteractable. */
	Interacted,
	/** TrackedExtractee is rescued and armed. */
	ExtracteeRescued,
	/** The Enemy Director completes the wave named WatchedWaveId. */
	WaveCompleted,
	/** Nothing watches — Blueprint or level script calls CompleteStep(). */
	Manual,
};

UENUM(BlueprintType)
enum class EObjectiveSideEffectWhen : uint8
{
	OnActivate,
	OnComplete,
};

UENUM(BlueprintType)
enum class EObjectiveSideEffectType : uint8
{
	/** Push the Enemy Director into a mission phase. */
	SetMissionPhase,
	/** Ask the Director for a finite wave. Refusals are retried, never dropped. */
	StartDirectorWave,
	/** Unlock a level-completion lift gate. */
	UnlockGate,
	/** Force the primary companion into a mode. */
	SetCompanionMode,
	/** End the level through the game mode. */
	CompleteLevel,
	/** Turn something on: ActivateTarget on an extraction target, Activate on another step. */
	ActivateActor,
	/** Open or shut the extractee's rescue gate, so the VIP cannot be freed ahead of his beat. */
	SetExtracteeRescuable,
	/** Send the primary companion off along a placed route. */
	CommandCompanionRoute,
};

/** One scripted consequence of a step activating or completing. */
USTRUCT(BlueprintType)
struct EXTRACTION_API FObjectiveSideEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect")
	EObjectiveSideEffectWhen When = EObjectiveSideEffectWhen::OnComplete;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect")
	EObjectiveSideEffectType Type = EObjectiveSideEffectType::SetMissionPhase;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::SetMissionPhase", EditConditionHides))
	EMissionPhase MissionPhase = EMissionPhase::Objective;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::StartDirectorWave", EditConditionHides))
	FDirectorWaveRequest WaveRequest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::UnlockGate", EditConditionHides))
	TObjectPtr<ALevelCompletionLiftGate> GateTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::SetCompanionMode", EditConditionHides))
	ECompanionMode CompanionMode = ECompanionMode::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::ActivateActor", EditConditionHides))
	TObjectPtr<AActor> ActivateTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::SetExtracteeRescuable", EditConditionHides))
	TObjectPtr<AExtracteeCompanion> ExtracteeTarget;

	/** SetExtracteeRescuable: true opens the rescue, false shuts it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::SetExtracteeRescuable", EditConditionHides))
	bool bRescuable = true;

	/** CommandCompanionRoute: the route the primary companion is sent down. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::CommandCompanionRoute", EditConditionHides))
	TObjectPtr<ACompanionRoute> RouteTarget;

	/** Whether a checkpoint fast-forward re-runs this effect for a beat the player already finished.
	 *  On for the idempotent types (phase, gate unlock, companion mode, rescue gate, activation) —
	 *  re-applying them just re-asserts the world the player left behind. Turn it off for anything
	 *  the resume must not repeat. Hidden for the three types a fast-forward NEVER replays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type != EObjectiveSideEffectType::StartDirectorWave && Type != EObjectiveSideEffectType::CompleteLevel && Type != EObjectiveSideEffectType::CommandCompanionRoute",
			EditConditionHides))
	bool bReplayOnResume = true;

	/** A wave the player already beat must not be re-started at level load; a CompleteLevel on any
	 *  earlier beat would end the level the instant the player resumes; and a route command would
	 *  march the companion off from wherever the checkpoint teleport just dropped him, walking a
	 *  cinematic the player already watched. None is ever a replay, so none is offered as one. */
	bool ShouldReplayOnResume() const
	{
		if (Type == EObjectiveSideEffectType::StartDirectorWave) return false;
		if (Type == EObjectiveSideEffectType::CompleteLevel) return false;
		if (Type == EObjectiveSideEffectType::CommandCompanionRoute) return false;
		return bReplayOnResume;
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnObjectiveStepActivated, AObjectiveStep*, Step);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnObjectiveStepCompleted, AObjectiveStep*, Step);

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking, Collision, Rendering))
class EXTRACTION_API AObjectiveStep : public AActor
{
	GENERATED_BODY()

public:
	AObjectiveStep();

	/** Go live: bind the condition, register the marker, run OnActivate side effects. Idempotent —
	 *  a second call while active, or any call after completion, is a no-op. Always a LIVE activation:
	 *  the checkpoint fast-forward's filtered variant is native-only and never reachable from a graph,
	 *  because a Blueprint that ticked that flag by accident would silently drop wave starts, level
	 *  completions and route commands with no log to say so. */
	UFUNCTION(BlueprintCallable, Category = "Objective Step")
	void Activate();

	/** Stand down without completing: unbind, drop the marker, stop wave retries. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Objective Step")
	void Deactivate();

	/** Finish the beat: OnComplete side effects, toast, then activate NextStep. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Objective Step")
	void CompleteStep();

	/** Marker/checkpoint identity. Falls back to the actor's own FName when StepId is left None. */
	UFUNCTION(BlueprintPure, Category = "Objective Step")
	FName GetEffectiveStepId() const { return StepId.IsNone() ? GetFName() : StepId; }

	UFUNCTION(BlueprintPure, Category = "Objective Step")
	AObjectiveStep* GetNextStep() const { return IsValid(NextStep) ? NextStep.Get() : nullptr; }

	UFUNCTION(BlueprintPure, Category = "Objective Step")
	bool IsStepActive() const { return bActive; }

	UFUNCTION(BlueprintPure, Category = "Objective Step")
	bool IsStepCompleted() const { return bCompleted; }

	UFUNCTION(BlueprintPure, Category = "Objective Step")
	EObjectiveCondition GetCondition() const { return Condition; }

	UPROPERTY(BlueprintAssignable, Category = "Objective Step|Events")
	FOnObjectiveStepActivated OnStepActivated;

	UPROPERTY(BlueprintAssignable, Category = "Objective Step|Events")
	FOnObjectiveStepCompleted OnStepCompleted;

#if WITH_DEV_AUTOMATION_TESTS
	void TestSetStepId(FName InStepId) { StepId = InStepId; }
	void TestSetCondition(EObjectiveCondition InCondition) { Condition = InCondition; }
	void TestSetNextStep(AObjectiveStep* InNextStep) { NextStep = InNextStep; }
	void TestSetEntryStep(bool bInEntry) { bIsEntryStep = bInEntry; }
	void TestSetRequiresAllContainers(bool bInRequiresAll) { bRequiresAllContainers = bInRequiresAll; }
	void TestSetTrackedContainers(const TArray<ALootContainer*>& InContainers);
	void TestSetTrackedInteractable(AActor* InInteractable) { TrackedInteractable = InInteractable; }
	void TestSetAnyInteractorCounts(bool bInAnyCounts) { bAnyInteractorCounts = bInAnyCounts; }
	void TestSetMarkerTarget(AActor* InMarkerTarget) { MarkerTarget = InMarkerTarget; }
	void TestSetFreezeMarkerAtActivation(bool bInFreeze) { bFreezeMarkerAtActivation = bInFreeze; }
	void TestSetCheckpoint(bool bInCheckpoint) { bIsCheckpoint = bInCheckpoint; }
	void TestSetWatchedWaveId(FName InWaveId) { WatchedWaveId = InWaveId; }
	void TestSetRequiredKeycardId(FName InKeycardId) { RequiredKeycardId = InKeycardId; }
	void TestAddSideEffect(const FObjectiveSideEffect& InEffect) { SideEffects.Add(InEffect); }
	static int32 TestAuditWaveOrphans(const TArray<AObjectiveStep*>& Order) { return AuditWaveOrphans(Order); }
	void TestApplyCompletedWorldState() { ApplyCompletedWorldState(); }
	/** The fast-forward walk itself. The recorded-id lookup and the checkpoint teleport around it are
	 *  the two halves a headless test cannot stand up; the replay in between is the part with the
	 *  filtering rules, so it is the part worth exercising directly. */
	void TestReplayPrefix(const TArray<AObjectiveStep*>& Earlier, AObjectiveStep* Resume)
	{
		ReplayEarlierSteps(Earlier, Resume);
	}
	void TestEvaluateCondition() { EvaluateCondition(); }
	AActor* TestResolveMarkerTarget() const { return ResolveMarkerTarget(); }
	FVector TestResolveStaticMarkerLocation() const { return ResolveStaticMarkerLocation(); }
#endif

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Player-overlap trigger for ReachLocation. Radius = CompletionRadius; collision is enabled
	 *  only while the step is active and the radius is above zero. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Objective Step")
	TObjectPtr<USphereComponent> CompletionSphere;

	// --- Identity / chain ---

	/** Unique id for the HUD marker and the checkpoint record. None = the actor's own FName. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step")
	FName StepId = NAME_None;

	/** The HUD objective line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step")
	FText Label;

	/** This step activates on BeginPlay and owns the checkpoint-resume walk for its chain.
	 *  Exactly one step per chain should have it set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step")
	bool bIsEntryStep = false;

	/** Activated when this beat completes. Null ends the chain. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Step")
	TObjectPtr<AObjectiveStep> NextStep;

	// --- Completion condition ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Completion")
	EObjectiveCondition Condition = EObjectiveCondition::Manual;

	/** ReachLocation: the player inside this distance (cm) completes the beat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::ReachLocation", EditConditionHides, ClampMin = "0.0"))
	float CompletionRadius = 0.f;

	/** AcquireKeycard: the card id the mission inventory must record. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::AcquireKeycard", EditConditionHides))
	FName RequiredKeycardId = NAME_None;

	/** EnemiesDead: every one of these must be dead or destroyed. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::EnemiesDead", EditConditionHides))
	TArray<TObjectPtr<AEnemyCharacter>> TrackedEnemies;

	/** DoorOpened: the door whose first fully-open state finishes the beat. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::DoorOpened", EditConditionHides))
	TObjectPtr<ADoorBase> TrackedDoor;

	/** ContainerLooted: the containers this beat watches. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::ContainerLooted", EditConditionHides))
	TArray<TObjectPtr<ALootContainer>> TrackedContainers;

	/** True = every container must be emptied; false = any one finishes the beat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::ContainerLooted", EditConditionHides))
	bool bRequiresAllContainers = true;

	/** RouteCompleted: an aborted route does NOT complete the beat. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::RouteCompleted", EditConditionHides))
	TObjectPtr<ACompanionRoute> TrackedRoute;

	/** Interacted: any actor that raises UInteractionEventSubsystem::NotifyWorldInteract from its own
	 *  success branch. Implementing IWorldInteractable alone is not enough — a Blueprint interactable
	 *  must call Notify World Interact itself. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::Interacted", EditConditionHides))
	TObjectPtr<AActor> TrackedInteractable;

	/** Interacted: false = only the PLAYER's own interaction ticks this beat off. The companion loots
	 *  through the same containers and a script drives the extraction target with no interactor at
	 *  all, so an ungated beat completes itself off work the player never did. Turn it on for a beat
	 *  that is deliberately about somebody else doing the interacting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::Interacted", EditConditionHides))
	bool bAnyInteractorCounts = false;

	/** ExtracteeRescued: completes when the VIP is freed and armed. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::ExtracteeRescued", EditConditionHides))
	TObjectPtr<AExtracteeCompanion> TrackedExtractee;

	/** WaveCompleted: the Director wave id to watch. Must match the request that started it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::WaveCompleted", EditConditionHides))
	FName WatchedWaveId = NAME_None;

	// --- Marker presentation ---

	/** The marker follows this actor. Null falls back, in order, to: the condition's derived anchor
	 *  (EnemiesDead area centroid, nearest unlooted container), then the condition's own payload
	 *  actor (the tracked door / interactable / extractee), then this step actor's own location.
	 *  Only wire it when the beat's marker belongs somewhere other than what it watches. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Step|Marker")
	TObjectPtr<AActor> MarkerTarget;

	/** Resolve the marker ONCE, at activation, and pin it to that spot — later movement of whatever
	 *  it resolved from is ignored. This is what "hold this ground" is made of: a Defend beat's own
	 *  squadmate walks away from the patch of floor the player was told to hold, so a marker that
	 *  follows him points at nothing. Saves wiring a dummy TargetPoint to stand in for the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Marker")
	bool bFreezeMarkerAtActivation = false;

	/** Additive nudge on the resolved marker position. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Marker")
	FVector MarkerOffset = FVector::ZeroVector;

	/** Height above the target's bounds base, so a floor crate, a door and an enemy all read at
	 *  the same marker height. Target-based markers only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Marker")
	float MarkerHeightAboveBase = 170.f;

	/** False = text-only on the HUD objective panel: no world billboard, no edge indicator.
	 *  This is what an optional objective looks like. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Marker")
	bool bShowWorldMarker = true;

	/** Raise an "Objective complete: <Label>" HUD toast on completion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Marker")
	bool bToastOnComplete = true;

	/** EnemiesDead only: force the area anchor instead of using the spawn-time centroid.
	 *  Zero = auto-centroid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Marker",
		meta = (EditCondition = "Condition == EObjectiveCondition::EnemiesDead", EditConditionHides))
	FVector AreaAnchorOverride = FVector::ZeroVector;

	// --- Side effects ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Side Effects",
		meta = (TitleProperty = "Type"))
	TArray<FObjectiveSideEffect> SideEffects;

	// --- Checkpoints ---

	/** Activating this step records it as the resume point and grants the companion a full heal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Checkpoint")
	bool bIsCheckpoint = false;

	/** Where the player lands on a checkpoint restart (the companions land beside them).
	 *  Wire a TargetPoint on every checkpoint step. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Objective Step|Checkpoint",
		meta = (EditCondition = "bIsCheckpoint", EditConditionHides))
	TObjectPtr<AActor> CheckpointSpawn;

private:
	// --- Tuning constants ---

	/** Seconds between Director retries after a refused wave start. */
	static constexpr float WaveRetrySeconds = 2.f;
	/** Poll cadence while the companion is still in combat and the checkpoint heal is held. */
	static constexpr float CheckpointHealPollSeconds = 1.f;
	/** Poll cadence and cap while waiting for the player pawn to exist on a checkpoint resume. */
	static constexpr float CheckpointSpawnPollSeconds = 0.25f;
	static constexpr int32 MaxCheckpointSpawnRetries = 40;
	/** Lateral offset (cm) each companion lands from the player on a checkpoint teleport. */
	static constexpr float CompanionSpawnSideOffset = 150.f;
	/** Lift applied to derived area anchors — they are sampled at capsule centre, so without it
	 *  they read lower than target-based markers resolved from a bounds base. */
	static constexpr float AreaMarkerLift = 80.f;

	// --- Lifecycle (native) ---

	/** Activate carrying the checkpoint fast-forward's filter. bResumeReplay filters this step's own
	 *  OnActivate effects exactly as the replaying beat's were filtered, and stays set across the one
	 *  synchronous span an activation owns: arming a ReachLocation sphere over a pawn already standing
	 *  in it, and the late-entry condition re-read, can both reach CompleteStep before this returns.
	 *  A beat whose condition the fast-forward itself satisfied is finishing AS PART of the resume —
	 *  its OnComplete effects must not re-start a beaten wave or end the level at level load.
	 *
	 *  The ask is confined to the replay prefix here rather than at the call sites: a target AT or
	 *  AFTER the resume point is a beat the player has never played, so it drops the filter, and the
	 *  resume point itself is held back entirely for the post-teleport activation that owns it. */
	void ActivateInternal(bool bResumeReplay);

	// --- Runtime state ---

	bool bActive = false;
	bool bCompleted = false;

	/** Set only while an activation that is itself part of a fast-forward is on the stack, and cleared
	 *  before that call returns. Never persistent: the player finishing this beat for real, minutes
	 *  later, must run every effect. */
	bool bCompletingUnderResume = false;

	/** Enemies still alive at activation, held weakly so a destroyed corpse reads unambiguously as
	 *  gone rather than as an untracked slot. */
	TArray<TWeakObjectPtr<AEnemyCharacter>> TrackedEnemySnapshot;

	/** TrackedEnemies.Num() at activation. The gap between this and the snapshot is the enemies the
	 *  player had already dealt with before the beat went live — they count as down. */
	int32 AuthoredEnemyCount = 0;

	/** Containers that existed at activation, held weakly so one blown up mid-mission reads as gone
	 *  rather than as an unwired slot. */
	TArray<TWeakObjectPtr<ALootContainer>> TrackedContainerSnapshot;

	/** TrackedContainers.Num() at activation. The gap between this and the snapshot is authored-null
	 *  slots — a mis-wire, which must stall the beat rather than satisfy it. */
	int32 AuthoredContainerCount = 0;

	/** Spawn-time centroid of TrackedEnemies. The EnemiesDead marker pins here so it holds the room
	 *  instead of chasing the last survivor around it. */
	FVector CapturedAreaAnchor = FVector::ZeroVector;

	/** bFreezeMarkerAtActivation: the pinned point, resolved on the first marker update of an
	 *  activation. Cleared on deactivation so a beat that goes live twice pins twice. */
	FVector FrozenMarkerLocation = FVector::ZeroVector;
	bool bMarkerFrozen = false;

	/** Wave requests the Director has not accepted yet. Drained by the retry timer. Reflected: the
	 *  request holds a UDirectorConfigData override the GC would otherwise never see. */
	UPROPERTY()
	TArray<FDirectorWaveRequest> PendingWaveRequests;

	FTimerHandle WaveRetryHandle;
	FTimerHandle CheckpointHealPollHandle;
	FTimerHandle CheckpointSpawnRetryHandle;
	int32 CheckpointSpawnRetries = 0;

	/** Lazily-resolved level companion for the checkpoint heal. */
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;

	// --- Condition plumbing ---

	void BindConditionDelegates();
	void UnbindConditionDelegates();

	/** Re-checks the condition and completes when satisfied. Also the late-entry catch-up path: a
	 *  door breached before its step went live never re-broadcasts, so entry must re-read state. */
	void EvaluateCondition();
	bool IsConditionSatisfied() const;

	/** Snapshots valid tracked enemies and the area centroid. Activation-time only. */
	void CaptureEnemySnapshot();

	/** Snapshots the containers that actually exist. Activation-time only — it is what separates a
	 *  crate destroyed during the mission (satisfied) from an authored-null slot (never satisfied). */
	void CaptureContainerSnapshot();

	int32 CountDownedEnemies() const;
	int32 CountSatisfiedContainers() const;
	ALootContainer* FindNearestUnlootedContainer() const;

	// --- Marker ---

	void UpdateMarker();
	void RemoveMarker();
	AActor* ResolveMarkerTarget() const;
	FVector ResolveStaticMarkerLocation() const;

	/** Where bFreezeMarkerAtActivation pins. A live target is sampled through the same bounds-base
	 *  rule the following marker uses, so the pin lands exactly where the marker read on the frame
	 *  it froze rather than at the target's capsule centre. */
	FVector CaptureFrozenMarkerLocation() const;
	UObjectiveSubsystem* GetObjectiveSubsystem() const;
	void RaiseCompletionToast() const;

	// --- Side effects ---

	/** Filters out anything a fast-forward must not repeat (see
	 *  FObjectiveSideEffect::ShouldReplayOnResume). A live completion always runs every effect.
	 *  bResumeReplay is the caller's half of that decision; a replay in progress is the other half,
	 *  and either one arms the filter. */
	void RunSideEffects(EObjectiveSideEffectWhen When, bool bResumeReplay = false);

	/** bResumeReplay is carried through rather than re-derived: an ActivateActor effect running under
	 *  a resume must hand the same filter to the step it activates, or the side-chain runs unfiltered. */
	void ApplySideEffect(const FObjectiveSideEffect& Effect, bool bResumeReplay);

	/** True when one of this step's side effects asks the Director for WaveId. */
	bool StartsDirectorWave(FName WaveId) const;

	/** Hands the route to the primary companion's AI controller, exactly as a placed route trigger
	 *  does. The controller owns the empty-route refusal and the already-running guard. */
	void CommandCompanionRoute(ACompanionRoute* Route);

	/** True when a fast-forward over this step would still replay at least one of its effects. */
	bool HasReplayableSideEffects() const;

	/** Queues a wave and starts it, or arms the retry. StartWave can refuse (another wave still
	 *  running, Director not ready) and the beat that asked for it is one-shot, so a refusal must
	 *  keep knocking rather than soft-lock the mission. */
	void QueueDirectorWave(const FDirectorWaveRequest& Request);
	void TryStartPendingWaves();

	// --- Checkpoints ---

	/** The level's primary companion, memoised into CachedCompanion. The one lookup path for every
	 *  companion-facing effect and the checkpoint heal — GetPrimaryCompanion walks the actor list,
	 *  and a second path would be a second thing to keep in sync. */
	ACompanionCharacter* ResolvePrimaryCompanion();

	void RecordCheckpoint();
	void TryApplyCompanionCheckpointHeal();

	/** Entry-step only: fast-forward the chain to the recorded step and resume there. True when a
	 *  checkpoint was consumed, so the caller skips the normal entry activation. */
	bool TryResumeFromCheckpoint();

	/** The replay itself: every earlier beat's end-state, applied under ONE fast-forward scope. The
	 *  scope spans the whole walk rather than a single activation because a beat an earlier prefix
	 *  beat stood up through ActivateActor is routinely completed by a LATER prefix beat's end-state —
	 *  a keycard re-granted, the enemies it watched destroyed — with nothing left on the stack to say
	 *  the completion belongs to a level load. Resume is excluded from the walk on purpose: it is the
	 *  caller's to stand up, after the checkpoint teleport and with no filter. */
	void ReplayEarlierSteps(const TArray<AObjectiveStep*>& Earlier, AObjectiveStep* Resume);

	/** This step's completed end-state, derived from its own condition, plus the replayable half of
	 *  its OnActivate and OnComplete side effects. Used only by the fast-forward — no delegates fire
	 *  and the chain does not advance. */
	void ApplyCompletedWorldState();
	void ApplyLootedContainers();

	/** Retires any companion-mode gate whose door this fast-forward opened. BeginPlay order between
	 *  gate and step is undefined, so tell the gate directly AND rely on its own skip-when-unlocked. */
	void RetireModeGatesForDoor(const ADoorBase* Door) const;

	void BeginCheckpointSpawn();
	void TryApplyCheckpointSpawn();

	// --- Validation ---

	/** Non-fatal audit at activation: unset payloads, self-reference, cycles, missing spawn point. */
	void AuditStepWiring();

	/** Level-wide audit run once, from the first placed step: entry-step count, duplicate ids. */
	void AuditLevelWiring();

	/** Checkpoint steps only: name every earlier beat a resume would leave unapplied. */
	void WarnOnUnappliablePredecessors();

	/** Names every checkpoint that sits between the beat starting a wave and the beat watching for it.
	 *  StartDirectorWave is one of the three effects a fast-forward never replays, so a resume there
	 *  activates a WaveCompleted beat with no wave running and no way to start one — a soft-lock the
	 *  predecessor audit cannot see, because a rescue beat that starts the wave DOES derive world
	 *  state. Returns the number of findings; logs one warning each. */
	static int32 AuditWaveOrphans(const TArray<AObjectiveStep*>& Order);

	/** True when this step's condition implies a world change a fast-forward can replay. */
	bool DerivesWorldState() const;

	/** The entry step of THIS step's chain — the one whose walk reaches here. Null when no entry
	 *  step reaches this beat, which is itself the answer for the predecessor warning. */
	AObjectiveStep* FindEntryStep() const;

#if WITH_EDITOR
	void ValidateConfig() const;
#endif

	// --- Delegate handlers ---

	UFUNCTION()
	void OnCompletionSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleKeycardRecorded(FName KeycardId);

	UFUNCTION()
	void HandleTrackedEnemyDeath();

	UFUNCTION()
	void HandleTrackedEnemyDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void HandleDoorOpened(AActor* Door);

	UFUNCTION()
	void HandleLootCompleted(ALootContainer* Container, AActor* Looter);

	UFUNCTION()
	void HandleContainerDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void HandleRouteCompleted(bool bAborted);

	UFUNCTION()
	void HandleWorldInteract(AActor* Target, AActor* Interactor);

	UFUNCTION()
	void HandleExtracteeRescued();

	UFUNCTION()
	void HandleDirectorWaveCompleted(FName WaveId);

	UFUNCTION()
	void HandleDirectorWaveBlocked(FName WaveId, FText Reason);
};
