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
class UInputAction;
class UObjectiveSubsystem;
class USphereComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogObjectiveStep, Log, All);

/** What finishes this beat. Each entry has its own payload fields below, shown only when picked.
 *
 *  New entries APPEND. A placed step serialises its condition by value, not by name, so inserting one
 *  mid-list silently re-points every step in every level authored past it. */
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
	/** DefendSeconds elapse with the beat live. The clock is the whole condition — nothing about the
	 *  fight is checked, because what makes a defend beat readable is that it ends when it says it
	 *  will. Pair it with a TripAlarm side effect: the Director's ambient pressure is what fills the
	 *  time, and that is gated on the alarm being up. */
	SurviveDuration,
	/** The primary companion is switched into TargetCompanionMode. */
	CompanionModeSet,
};

UENUM(BlueprintType)
enum class EObjectiveSideEffectWhen : uint8
{
	OnActivate,
	OnComplete,
};

/** New entries APPEND, for the same reason the condition list does — a placed step's side effects
 *  serialise by value. */
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
	/** Turn something on: ActivateTarget on an extraction target, the interaction on an interaction
	 *  volume, Activate on another step. */
	ActivateActor,
	/** Open or shut the extractee's rescue gate, so the VIP cannot be freed ahead of his beat. */
	SetExtracteeRescuable,
	/** Send the primary companion off along a placed route. */
	CommandCompanionRoute,
	/** Force the global alert to Loud. The Director's ambient (non-wave) spawning is hard-gated on
	 *  it, so a floor the player cleared quietly answers a defend beat with an empty room — this is
	 *  what turns the pressure back on without scripting a finite wave. The alert ladder is a
	 *  ratchet, so applying it twice is applying it once. */
	TripAlarm,
	/** Move the whole squad at once — the player pawn to PlayerDestination, each companion to its
	 *  slot in CompanionDestinations. The endgame lift ride, without a level transition. */
	TeleportSquad,
	/** Enable or disable the Director's ambient (non-wave) spawning. Scripted waves still run.
	 *  The defend timer's OnComplete side effect uses this to halt reinforcements while letting
	 *  surviving enemies fight on. Idempotent — re-applying the same state is a no-op. */
	SetDirectorSpawning,
	/** Lock or unlock placed doors via their external gate. The defend beat locks the exits on
	 *  activation so the player cannot walk out early, then unlocks them on completion. Idempotent —
	 *  a checkpoint resume before the defend re-locks the doors, and one after it re-unlocks them,
	 *  restoring the world the player left behind. */
	SetDoorsLocked,
	/** Stand down another objective step: call Deactivate() on the ActivateTarget. The beat drops its
	 *  marker and unbinds, but is NOT completed and the chain does NOT advance — a deactivated step
	 *  can be reactivated later if needed. Idempotent (Deactivate already is), and safe on a step
	 *  that was never activated or is already completed.
	 *
	 *  Intended for OFF-CHAIN optional beats (NextStep = None). Aimed at a live main-chain step it is
	 *  a permanent soft-lock: Deactivate clears bActive but never bCompleted, so CompleteStep refuses
	 *  on the deactivated step and the chain stalls. A warning logs when the target is active and has
	 *  a non-null NextStep, which is the mis-wire signature.
	 *
	 *  Deactivate() also tears down wave retries and pending wave requests ABOVE its bActive guard, so
	 *  pointing this at a step that queued a wave from its own OnComplete would silently kill that wave.
	 *  Not the intended use case, but worth knowing.
	 *
	 *  The motivating shape: an optional tutorial beat sits off the main chain, is stood up by an
	 *  ActivateActor on the parent step, and must be stood back down when the parent completes —
	 *  otherwise the player stares at a stale objective for a beat that is no longer relevant. The
	 *  parent's OnComplete carries both: ActivateActor for the next main-chain beat, and
	 *  DeactivateActor for the optional beat it stood up on activation. */
	DeactivateActor,
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

	/** ActivateActor / DeactivateActor: the actor to turn on or off. For ActivateActor this dispatches
	 *  on what it was handed (extraction target, interaction volume, objective step); for
	 *  DeactivateActor only an objective step is meaningful — anything else logs a warning and is
	 *  skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::ActivateActor || Type == EObjectiveSideEffectType::DeactivateActor", EditConditionHides))
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

	/** TeleportSquad: where the player lands. Its yaw becomes the player's control rotation, so point
	 *  the marker at whatever the squad is meant to be looking at on arrival. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::TeleportSquad", EditConditionHides))
	TObjectPtr<AActor> PlayerDestination;

	/** TeleportSquad: one landing spot per companion, slot 0 = the primary and the rest in level
	 *  order. A short list is legitimate — a companion with no slot simply stays where it is. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::TeleportSquad", EditConditionHides))
	TArray<TObjectPtr<AActor>> CompanionDestinations;

	/** SetDirectorSpawning: true re-enables ambient spawning, false disables it. Default false because
	 *  the typical use is "stop spawning after the defend timer", paired with a re-enable on a later beat
	 *  if the mission needs pressure again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::SetDirectorSpawning", EditConditionHides))
	bool bDirectorSpawningEnabled = false;

	/** SetDoorsLocked: level-placed doors whose external gate is toggled by this effect. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::SetDoorsLocked", EditConditionHides))
	TArray<TObjectPtr<ADoorBase>> DoorTargets;

	/** SetDoorsLocked: true locks the doors (blocks player open), false unlocks them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type == EObjectiveSideEffectType::SetDoorsLocked", EditConditionHides))
	bool bDoorsLocked = true;

	/** Whether a checkpoint fast-forward re-runs this effect for a beat the player already finished.
	 *  On for the idempotent types (phase, gate unlock, companion mode, rescue gate, activation,
	 *  alarm) — re-applying them just re-asserts the world the player left behind. Turn it off for
	 *  anything the resume must not repeat. Hidden for the four types a fast-forward NEVER replays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Effect",
		meta = (EditCondition = "Type != EObjectiveSideEffectType::StartDirectorWave && Type != EObjectiveSideEffectType::CompleteLevel && Type != EObjectiveSideEffectType::CommandCompanionRoute && Type != EObjectiveSideEffectType::TeleportSquad",
			EditConditionHides))
	bool bReplayOnResume = true;

	/** A wave the player already beat must not be re-started at level load; a CompleteLevel on any
	 *  earlier beat would end the level the instant the player resumes; a route command would march
	 *  the companion off from wherever the checkpoint teleport just dropped him, walking a cinematic
	 *  the player already watched; and a squad teleport would yank the party clean across the level
	 *  at load, straight past the checkpoint teleport that owns where the player starts. None is ever
	 *  a replay, so none is offered as one. */
	bool ShouldReplayOnResume() const
	{
		if (Type == EObjectiveSideEffectType::StartDirectorWave) return false;
		if (Type == EObjectiveSideEffectType::CompleteLevel) return false;
		if (Type == EObjectiveSideEffectType::CommandCompanionRoute) return false;
		if (Type == EObjectiveSideEffectType::TeleportSquad) return false;
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

	/** Debug fast-forward to TargetId from wherever the chain currently is: stands down whatever is
	 *  live, applies every earlier beat's end-state, teleports the party to the target's
	 *  CheckpointSpawn, then activates it. Same machinery as a checkpoint resume, so there is no
	 *  second body of skip code to keep in sync with the chain. False when TargetId is not on a chain.
	 *  Console entry point is ObjSkip on AExtractionPlayer. */
	static bool DebugSkipToStep(const UObject* WorldContext, FName TargetId);

	/** Every step id on every chain, in walk order, with a flag for the one that is live right now.
	 *  Backs the ObjList console command. */
	static void DebugCollectChain(const UObject* WorldContext, TArray<FName>& OutIds, TArray<bool>& OutActive,
		TArray<bool>& OutCompleted);

#if WITH_DEV_AUTOMATION_TESTS
	void TestSetStepId(FName InStepId) { StepId = InStepId; }
	/** Refreshes the token flag exactly as PostInitializeComponents does — a test sets the label after
	 *  the actor is already initialised. */
	void TestSetLabel(const FText& InLabel) { Label = InLabel; RefreshLabelHintTokenFlag(); }
	void TestSetPromptAction(const UInputAction* InAction);
	void TestSetSecondaryPromptAction(const UInputAction* InAction);
	FText TestBuildDisplayLabel() const { return BuildDisplayLabel(); }
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

	/** The HUD objective line. Authored ONCE and stored with its key-hint tokens intact — see
	 *  PromptAction. A resolved key is never written back here, so a saved label can never carry a
	 *  key the player has since rebound. A literal '{' in the text must be escaped as '{{' when
	 *  PromptAction is set, or FText::Format will consume it as a token. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step")
	FText Label;

	/** Optional key hint for this beat's HUD line. Assign the input action the player has to press and
	 *  put "{PromptKey}" in Label where the key name belongs; the key is resolved from the live mapping
	 *  every time the line is pushed, and again whenever Enhanced Input rebuilds its mappings.
	 *
	 *  Designer-authored on purpose, and defaulting to none. Deriving "this is a breach beat" from the
	 *  tracked door instead would advertise a breach hint on EVERY unlocked breachable-door objective,
	 *  including beats meant to be opened by hand — a false positive only visible by reading the line
	 *  in PIE. The capability check on the door cannot help either: it takes no interactor, so it
	 *  cannot know whether there is a companion alive to do the breaching. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step")
	TObjectPtr<const UInputAction> PromptAction;

	/** Second key for a two-press prompt — mark the target, then confirm the order. Token:
	 *  "{SecondaryPromptKey}". Leave unset for a one-key hint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step")
	TObjectPtr<const UInputAction> SecondaryPromptAction;

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

	/** SurviveDuration: how long the beat holds, in seconds. Counted down on the HUD line, and a
	 *  fresh full clock every time the beat goes live — elapsed time is never carried across a
	 *  checkpoint resume, because a player who reloads into six seconds of a ninety-second hold has
	 *  been handed the beat rather than made to play it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::SurviveDuration", EditConditionHides, ClampMin = "1.0"))
	float DefendSeconds = 90.f;

	/** Mode the companion must be switched into for the CompanionModeSet condition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Completion",
		meta = (EditCondition = "Condition == EObjectiveCondition::CompanionModeSet", EditConditionHides))
	ECompanionMode TargetCompanionMode = ECompanionMode::Stealth;

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
	 *  Independent of bOptional below — a mandatory beat can be text-only, and an optional one can
	 *  still show a billboard. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Marker")
	bool bShowWorldMarker = true;

	/** True marks this beat a side objective: the HUD renders it as the untracked/secondary line
	 *  next to the mandatory one. Presentation only — an optional step still completes, still
	 *  advances NextStep on completion, and still fires every one of its side effects exactly like
	 *  any other step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective Step|Marker")
	bool bOptional = false;

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
	/** How often a live SurviveDuration beat rewrites its HUD clock. One second is the resolution the
	 *  label is formatted at, so anything faster is churn nobody can read. */
	static constexpr float DefendCountdownSeconds = 1.f;
	/** Clock formatting only. */
	static constexpr int32 SecondsPerMinute = 60;
	/** Horizontal arrival test (squared, cm^2). TeleportToLocation projects to the navmesh and then
	 *  TeleportTo resolves encroachment, settling the capsule at resting contact — a successful move
	 *  can land ~88cm above a floor-level destination (capsule half-height). Splitting the axes
	 *  keeps the horizontal budget clean: 100cm catches a refused encroachment while tolerating
	 *  navmesh snap. */
	static constexpr float CompanionArrivalToleranceSq = 100.f * 100.f;
	/** Vertical arrival tolerance (cm). One capsule height: generous enough for the capsule-lift
	 *  that every successful move produces, tight enough to catch a wrong-storey landing — storey
	 *  spacing in the lift shaft is well inside the controller's 500cm projection extent, so a
	 *  wider band would accept a companion teleported to the wrong floor. */
	static constexpr float CompanionArrivalHeightTolerance = 180.f;

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

	/** Whether Label carries a key-hint token. Answered once from authored data rather than scanned on
	 *  every push, and cached so the substitution still runs for a label that authored a token with no
	 *  action wired — otherwise a mis-wire reaches the HUD as a literal brace. */
	bool bLabelHasHintToken = false;

	/** Whether this step is listening for Enhanced Input mapping rebuilds. Guards the bind/unbind pair
	 *  so the teardown is safe to call on a step that never bound. */
	bool bMappingRebuildBound = false;

	/** Wave requests the Director has not accepted yet. Drained by the retry timer. Reflected: the
	 *  request holds a UDirectorConfigData override the GC would otherwise never see. */
	UPROPERTY()
	TArray<FDirectorWaveRequest> PendingWaveRequests;

	FTimerHandle WaveRetryHandle;
	FTimerHandle CheckpointHealPollHandle;
	FTimerHandle CheckpointSpawnRetryHandle;
	int32 CheckpointSpawnRetries = 0;

	/** SurviveDuration: the one-shot that completes the beat, and the 1Hz clock that writes the HUD
	 *  line under it. The remaining time is read back off the first rather than tracked separately,
	 *  so there is no second copy of "how long is left" to drift or to survive a pause. */
	FTimerHandle DefendTimerHandle;
	FTimerHandle DefendCountdownHandle;

	/** TeleportSquad: held while the player pawn retry polls. Companions move immediately in
	 *  TeleportSquad; only the player is deferred when the pawn is not yet spawned. Cleared in
	 *  EndPlay. */
	FTimerHandle SquadTeleportRetryHandle;
	int32 SquadTeleportRetries = 0;

	/** CompanionModeSet: poll cadence/cap mirrors CheckpointSpawnPollSeconds / MaxCheckpointSpawnRetries
	 *  above — the companion is the same BeginPlay-order race as the player pawn, just with no
	 *  checkpoint step around to trigger a wait for it. */
	FTimerHandle CompanionModeBindRetryHandle;
	int32 CompanionModeBindRetries = 0;

	/** The side effect kept alive for the deferred player-only teleport. Companions already moved
	 *  by the time the deferral begins — only the player destination is still needed. Reflected so
	 *  the reference collector sees PlayerDestination — without UPROPERTY the TObjectPtr is not
	 *  nulled on collect and the retry reads freed memory. */
	UPROPERTY()
	FObjectiveSideEffect PendingSquadTeleport;

	/** Lazily-resolved level companion for the checkpoint heal. */
	TWeakObjectPtr<ACompanionCharacter> CachedCompanion;

	// --- Condition plumbing ---

	void BindConditionDelegates();
	void UnbindConditionDelegates();

	/** CompanionModeSet: binds Companion->OnModeChanged once the primary companion exists. Retries
	 *  on CompanionModeBindRetryHandle when it does not exist yet — the companion can spawn after
	 *  this step has already gone live. Re-evaluates the condition once a LATE bind succeeds, since
	 *  by then the once-per-activation late-entry check in ActivateInternal has already run and
	 *  missed a companion that was not there to see. */
	void TryBindCompanionModeCondition();

	/** Re-checks the condition and completes when satisfied. Also the late-entry catch-up path: a
	 *  door breached before its step went live never re-broadcasts, so entry must re-read state. */
	void EvaluateCondition();
	bool IsConditionSatisfied() const;

	/** SurviveDuration: arm the completion one-shot and the HUD clock under it. Always a fresh full
	 *  DefendSeconds — see the property comment on why a resume does not inherit elapsed time. */
	void StartDefendCountdown();

	/** Drops both defend timers. Reached from Deactivate and EndPlay, and safe on a beat that never
	 *  started one. */
	void StopDefendCountdown();

	/** Rewrites the HUD line to "<Label> — M:SS" off the completion timer's own remaining time. */
	void UpdateDefendCountdownLabel();

	/** The completion one-shot's target. */
	void HandleDefendElapsed();

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

	/** Where bFreezeMarkerAtActivation pins. A live target is sampled through
	 *  FObjectiveMarker::ResolveTargetBase — the same anchor rule the following marker uses
	 *  (capsule base for characters, bounds base otherwise) — so the pin lands exactly where the
	 *  marker read on the frame it froze rather than at the target's capsule centre. */
	FVector CaptureFrozenMarkerLocation() const;
	UObjectiveSubsystem* GetObjectiveSubsystem() const;
	void RaiseCompletionToast() const;

	// --- Label presentation ---

	/** The authored Label with its key-hint tokens resolved against the CURRENT mapping. Every path
	 *  that shows this beat's text goes through here — the marker push, the defend countdown and the
	 *  completion toast — so there is one substitution and the toast can never carry a raw token.
	 *
	 *  Returns Label untouched when there is nothing to substitute, which is the case for every beat
	 *  with no hint: a label that legitimately contains a brace is then never handed to FText::Format. */
	FText BuildDisplayLabel() const;

	/** Re-reads Label for hint tokens. Called wherever CompletionSphere's radius is re-derived, for the
	 *  same reason: authored data, answered on both the editor path and every runtime load path. */
	void RefreshLabelHintTokenFlag();

	/** Subscribes to the local player's mapping-rebuild event so a hinted line follows a rebind, and —
	 *  the case that actually bites — is correct on the first frame at all. AddMappingContext defers
	 *  the mapping rebuild, so a step activating from BeginPlay queries an empty table and would
	 *  otherwise show "[unbound]" for the whole mission. No-op for a beat with no hint. */
	void BindMappingRebuildListener();
	void UnbindMappingRebuildListener();

	/** Re-pushes the HUD line with freshly resolved keys. Cheap and idempotent: UpdateObjectiveLabel
	 *  is silent when the id is not registered or the text is unchanged. */
	UFUNCTION()
	void HandleControlMappingsRebuilt();

	// --- Side effects ---

	/** Filters out anything a fast-forward must not repeat (see
	 *  FObjectiveSideEffect::ShouldReplayOnResume). A live completion always runs every effect.
	 *  bResumeReplay is the caller's half of that decision; a replay in progress is the other half,
	 *  and either one arms the filter. */
	void RunSideEffects(EObjectiveSideEffectWhen When, bool bResumeReplay = false);

	/** bResumeReplay is carried through rather than re-derived: an ActivateActor effect running under
	 *  a resume must hand the same filter to the step it activates, or the side-chain runs unfiltered. */
	void ApplySideEffect(const FObjectiveSideEffect& Effect, bool bResumeReplay);

	/** ActivateActor: the "turn something on" branch, lifted out because it is the only effect that
	 *  has to dispatch on what it was handed. */
	void ApplyActivateActor(const FObjectiveSideEffect& Effect, bool bResumeReplay);

	/** TeleportSquad: player first, then each companion to its slot. */
	void TeleportSquad(const FObjectiveSideEffect& Effect);

	/** Companions in the slot order CompanionDestinations is authored against: the primary always
	 *  first, then everything else in level-actor order. Deterministic by construction — a list whose
	 *  slot 0 meant a different squadmate between runs would be unauthorable. */
	void GatherSquadCompanions(TArray<ACompanionCharacter*>& OutCompanions);

	/** One companion moved, through its AI controller where it has one — the controller's teleport is
	 *  what cancels the move order that would otherwise walk it straight back. */
	void TeleportCompanionToDestination(ACompanionCharacter* Companion, const AActor* Destination) const;

	/** Player pawn not yet available — poll on the same cadence and budget as the checkpoint spawn
	 *  retry, which faces the same actor-ordering hazard. */
	void DeferTeleportSquad(const FObjectiveSideEffect& Effect);
	void TryApplyDeferredSquadTeleport();

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

	/** TeleportSquad's share of that audit. Runs at activation, so it can count the companions that
	 *  actually exist rather than guess from the wiring. */
	void AuditTeleportSquad(const FObjectiveSideEffect& Effect);

	/** Key-hint wiring: a token with no action, or an action with no token. Neither breaks the beat, and
	 *  neither is visible anywhere except by reading the objective line in PIE — which is exactly the
	 *  class of defect this audit exists for. */
	void AuditPromptKeyHint() const;

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

	UFUNCTION()
	void HandleCompanionModeChanged(ECompanionMode NewMode);
};
