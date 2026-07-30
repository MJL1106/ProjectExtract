// AObjectiveStep implementation.

#include "World/ObjectiveStep.h"

#include "AI/CompanionAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/ExtractionPlayer.h"
#include "Companion/CompanionCharacter.h"
#include "Companion/CompanionRoute.h"
#include "Components/HealthComponent.h"
#include "Components/SphereComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Enemy/EnemyCharacter.h"
#include "Enemy/EnemyDirectorSubsystem.h"
#include "Extractee/ExtracteeCharacter.h"
#include "Extractee/ExtracteeCompanion.h"
#include "Game/ExtractionGameInstance.h"
#include "Game/ExtractionGameMode.h"
#include "Game/MissionInventorySubsystem.h"
#include "Game/ObjectiveSubsystem.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/KeybindHintLibrary.h"
#include "World/CompanionModeDoorGate.h"
#include "World/DoorBase.h"
#include "World/ExtractionTargetActor.h"
#include "World/InteractionEventSubsystem.h"
#include "World/InteractionVolume.h"
#include "World/LevelCompletionLiftGate.h"
#include "World/LootContainer.h"
#include "World/ObjectiveChainWalker.h"

DEFINE_LOG_CATEGORY(LogObjectiveStep);

namespace
{
	/** Chain accessors handed to FObjectiveChainWalker — free functions so the walker stays a pure
	 *  template with no knowledge of the actor. */
	AObjectiveStep* NextOf(AObjectiveStep* Step) { return Step ? Step->GetNextStep() : nullptr; }
	FName IdOf(const AObjectiveStep* Step) { return Step ? Step->GetEffectiveStepId() : NAME_None; }

	/** The checkpoint fast-forward currently on the stack, or nothing outside one. */
	struct FResumeWalk
	{
		/** The beat the player is resuming AT. Stood up by TryResumeFromCheckpoint after the teleport,
		 *  never from inside the walk. */
		TWeakObjectPtr<AObjectiveStep> ResumePoint;

		/** The resume point and every beat past it on the chain — the part of the mission the player
		 *  has NOT played. */
		TSet<TWeakObjectPtr<AObjectiveStep>> AtOrAfterResume;
	};

	/** A file static rather than actor state because the beats that have to answer "am I part of this
	 *  replay?" are not the beat that owns the walk: a side chain stood up mid-replay asks it three
	 *  activations deep. Identity only, never dereferenced, and pointing at a stack frame that
	 *  outlives every call it is visible to — a resume is one synchronous span. */
	const FResumeWalk* ActiveResumeWalk = nullptr;

	/** Publishes a walk for its duration and restores whatever was there before. */
	struct FScopedResumeWalk
	{
		explicit FScopedResumeWalk(const FResumeWalk& Walk)
			: Previous(ActiveResumeWalk) { ActiveResumeWalk = &Walk; }
		~FScopedResumeWalk() { ActiveResumeWalk = Previous; }

		const FResumeWalk* Previous = nullptr;
	};

	/** True while a fast-forward is holding this beat back for its caller. */
	bool IsDeferredResumePoint(const AObjectiveStep* Step)
	{
		return ActiveResumeWalk && ActiveResumeWalk->ResumePoint.Get() == Step;
	}

	/** False for the resume point and everything past it: those beats are ahead of the player, so an
	 *  activation that reaches one early is a LIVE activation and keeps every effect. */
	bool CarriesResumeFilter(AObjectiveStep* Step)
	{
		return !ActiveResumeWalk || !ActiveResumeWalk->AtOrAfterResume.Contains(Step);
	}

	/** Label tokens the display formatter substitutes. NAMED rather than positional so a designer's own
	 *  "{0}" in a label can never collide with a key hint, and so the token reads as documentation in
	 *  the details panel. */
	const FString PromptKeyToken = TEXT("PromptKey");
	const FString SecondaryPromptKeyToken = TEXT("SecondaryPromptKey");

	FString BracedToken(const FString& Token) { return FString::Printf(TEXT("{%s}"), *Token); }
}

AObjectiveStep::AObjectiveStep()
{
	PrimaryActorTick.bCanEverTick = false;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	CompletionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CompletionSphere"));
	CompletionSphere->SetupAttachment(GetRootComponent());
	CompletionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CompletionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CompletionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CompletionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CompletionSphere->SetCanEverAffectNavigation(false);
}

void AObjectiveStep::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Keep the viewport sphere honest about CompletionRadius while the designer drags it.
	CompletionSphere->SetSphereRadius(FMath::Max(CompletionRadius, 0.f));
	RefreshLabelHintTokenFlag();
}

void AObjectiveStep::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// The one runtime sizing. OnConstruction covers the editor viewport while the designer drags
	// the value; this covers every load path that never re-runs a construction script.
	CompletionSphere->SetSphereRadius(FMath::Max(CompletionRadius, 0.f));
	RefreshLabelHintTokenFlag();

	// Bound HERE, not in BeginPlay: a checkpoint resume activates its target step from the ENTRY
	// step's BeginPlay, and an ActivateActor effect can activate any step at any time. Actor
	// BeginPlay order is unspecified, so a bind that late can arm the sphere with nothing listening —
	// and a pawn already standing inside never gets a retroactive overlap.
	CompletionSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &AObjectiveStep::OnCompletionSphereOverlap);
}

void AObjectiveStep::BeginPlay()
{
	Super::BeginPlay();

#if WITH_EDITOR
	ValidateConfig();
#endif
	AuditLevelWiring();

	if (!bIsEntryStep) return;
	if (TryResumeFromCheckpoint()) return;
	Activate();
}

void AObjectiveStep::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WaveRetryHandle);
		World->GetTimerManager().ClearTimer(CheckpointHealPollHandle);
		World->GetTimerManager().ClearTimer(CheckpointSpawnRetryHandle);
		World->GetTimerManager().ClearTimer(SquadTeleportRetryHandle);
	}
	StopDefendCountdown();
	Deactivate();
	Super::EndPlay(EndPlayReason);
}

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------

void AObjectiveStep::Activate()
{
	// Graph-reachable entry point is always a LIVE activation — see the header on why the filtered
	// variant is native-only.
	ActivateInternal(false);
}

void AObjectiveStep::ActivateInternal(bool bResumeReplay)
{
	// The resume point belongs to TryResumeFromCheckpoint, which stands it up AFTER the checkpoint
	// teleport and with no filter. A prefix beat that reaches it from inside the walk — through an
	// ActivateActor naming it, or by completing straight into it — would resolve its marker and arm
	// its ReachLocation sphere at the level-start position, record the checkpoint out of order, and
	// leave the teleported activation that should have done all three to no-op.
	if (IsDeferredResumePoint(this))
	{
		UE_LOG(LogObjectiveStep, Verbose,
			TEXT("Step '%s' is the resume point — held back for the post-teleport activation"),
			*GetEffectiveStepId().ToString());
		return;
	}

	if (bActive || bCompleted) return;
	bActive = true;

	// The filter is confined to the replay prefix. A beat AT or AFTER the resume point is one the
	// player has never played, so standing it up early is a live activation: filtering it would
	// silently drop the wave start or level completion the designer authored on it.
	const bool bReplaying = bResumeReplay && CarriesResumeFilter(this);

	// Held for the whole activation, not just the condition re-read: arming a ReachLocation sphere
	// over a pawn already standing in it, an OnActivate effect, and the late-entry re-read below can
	// each reach CompleteStep before this returns. A beat whose condition the fast-forward itself
	// satisfied is finishing AS PART of the resume, and must not re-start a beaten wave, end the
	// level or march the companion off at level load.
	bCompletingUnderResume = bReplaying;

	AuditStepWiring();
	CaptureEnemySnapshot();
	CaptureContainerSnapshot();
	BindConditionDelegates();

	// The filter travels with the activation. An ActivateActor effect is the one place a resume
	// reaches a step it is not itself replaying, and that step's own OnActivate effects are just as
	// capable of re-starting a beaten wave or ending the level as the ones the flag already blocks.
	RunSideEffects(EObjectiveSideEffectWhen::OnActivate, bReplaying);

	if (bIsCheckpoint)
	{
		RecordCheckpoint();
		WarnOnUnappliablePredecessors();
		TryApplyCompanionCheckpointHeal();
	}

	// Before the push, so the line is already listening by the time the first mapping rebuild lands at
	// the end of this frame — that rebuild is what turns a BeginPlay-time "[unbound]" into the real key.
	BindMappingRebuildListener();

	UpdateMarker();
	UE_LOG(LogObjectiveStep, Log, TEXT("Step '%s' activated"), *GetEffectiveStepId().ToString());
	OnStepActivated.Broadcast(this);

	// Arming the sphere can fire an overlap synchronously, so it lands after the activation
	// broadcast — a listener must never see the completion before the activation.
	if (Condition == EObjectiveCondition::ReachLocation && CompletionRadius > 0.f)
		CompletionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	// Late entry: a door breached, a container emptied or a card taken before this beat went live
	// never re-broadcasts, so re-read the world rather than wait for an event that already fired.
	EvaluateCondition();

	// The countdown timer fires after its first interval, and the objective is not registered until
	// UpdateMarker (210). Writing the clock label here — after both the marker registration and the
	// late-entry re-read — so the HUD line shows "Label — M:SS" from the first frame rather than
	// the bare Label for a full second. Placed after EvaluateCondition because a late-entry
	// completion above would have already deactivated this beat (stopping the countdown), so a
	// label write for a beat that is already done is harmless but pointless.
	if (Condition == EObjectiveCondition::SurviveDuration && bActive)
		UpdateDefendCountdownLabel();

	// Never persistent. The player finishing this beat for real, minutes after the resume, is a live
	// completion and runs every effect.
	bCompletingUnderResume = false;
}

void AObjectiveStep::Deactivate()
{
	// Always drop these, even on an inactive step: a companion-heal poll held open by a firefight
	// outlives the beat that asked for it, and a step stood down mid-retry must stop knocking for a
	// wave nobody is waiting on any more.
	//
	// A wave asked for by an OnComplete effect is NOT dropped here, and must not be: CompleteStep
	// deactivates BEFORE running those effects, so the queue this resets is empty by the time they
	// run and the request they add outlives the beat exactly as it should.
	//
	// CheckpointSpawnRetryHandle deliberately does NOT stop here. It is a one-shot, bounded
	// (MaxCheckpointSpawnRetries) wait for the player pawn to exist, and the resume step can
	// complete on its own late-entry catch-up while that wait is still running — dropping it there
	// would strand the player at the level-start position in an already fast-forwarded world.
	// EndPlay owns its teardown.
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WaveRetryHandle);
		World->GetTimerManager().ClearTimer(CheckpointHealPollHandle);
	}
	PendingWaveRequests.Reset();

	// Dropped here rather than alongside the delegate unbinds below, because the unbind is skipped
	// entirely for an already-inactive step — and a countdown that outlives its beat keeps rewriting
	// an objective line that has already been removed from the HUD.
	StopDefendCountdown();

	// Above the guard for the same reason: a rebuild listener that outlives its beat keeps rewriting an
	// objective line that has already been removed from the HUD. Its own flag makes it a no-op for a
	// step that never bound.
	UnbindMappingRebuildListener();

	if (!bActive) return;
	bActive = false;

	UnbindConditionDelegates();
	if (IsValid(CompletionSphere)) CompletionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RemoveMarker();

	// A frozen pin belongs to ONE activation. A beat stood down and re-activated is being held for
	// a second time, and the ground worth holding has almost certainly moved with the fight.
	bMarkerFrozen = false;
}

void AObjectiveStep::CompleteStep()
{
	if (bCompleted) return;
	if (!bActive)
	{
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): CompleteStep called on a step that is not active — ignored"),
			*GetName(), *GetEffectiveStepId().ToString());
		return;
	}
	bCompleted = true;

	// Deactivate BEFORE the broadcast: a handler that registers the next objective under the same
	// id must not have its fresh registration wiped by this step's RemoveObjective.
	Deactivate();

	// A completion the fast-forward itself caused — a beat watching a keycard an earlier prefix beat
	// re-granted, crates an earlier beat emptied, enemies an earlier beat destroyed — is part of the
	// level load, not of play. Its effects are filtered exactly as the replaying beat's were, and the
	// cascade down NextStep carries the same filter rather than going live one beat later.
	RunSideEffects(EObjectiveSideEffectWhen::OnComplete, bCompletingUnderResume);
	if (bToastOnComplete) RaiseCompletionToast();

	UE_LOG(LogObjectiveStep, Log, TEXT("Step '%s' completed"), *GetEffectiveStepId().ToString());
	OnStepCompleted.Broadcast(this);

	if (AObjectiveStep* Next = GetNextStep()) Next->ActivateInternal(bCompletingUnderResume);
}

// ------------------------------------------------------------------
// Condition binding + evaluation
// ------------------------------------------------------------------

void AObjectiveStep::BindConditionDelegates()
{
	UWorld* World = GetWorld();
	if (!World) return;

	auto Unset = [this](const TCHAR* Payload)
	{
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): %s is unset — nothing can complete this beat"),
			*GetName(), *GetEffectiveStepId().ToString(), Payload);
	};

	switch (Condition)
	{
	case EObjectiveCondition::AcquireKeycard:
		if (UMissionInventorySubsystem* Inventory = World->GetSubsystem<UMissionInventorySubsystem>())
			Inventory->OnKeycardRecorded.AddUniqueDynamic(this, &AObjectiveStep::HandleKeycardRecorded);
		break;

	case EObjectiveCondition::EnemiesDead:
		for (AEnemyCharacter* Enemy : TrackedEnemies)
		{
			if (!IsValid(Enemy)) continue;
			if (UHealthComponent* Health = Enemy->GetHealthComponent())
				Health->OnDeath.AddUniqueDynamic(this, &AObjectiveStep::HandleTrackedEnemyDeath);
			Enemy->OnDestroyed.AddUniqueDynamic(this, &AObjectiveStep::HandleTrackedEnemyDestroyed);
		}
		break;

	case EObjectiveCondition::DoorOpened:
		if (IsValid(TrackedDoor))
			TrackedDoor->OnDoorOpened.AddUniqueDynamic(this, &AObjectiveStep::HandleDoorOpened);
		else
			Unset(TEXT("TrackedDoor"));
		break;

	case EObjectiveCondition::ContainerLooted:
		if (TrackedContainers.IsEmpty()) Unset(TEXT("TrackedContainers"));
		for (ALootContainer* Container : TrackedContainers)
		{
			if (!IsValid(Container)) continue;
			Container->OnLootCompleted.AddUniqueDynamic(this, &AObjectiveStep::HandleLootCompleted);
			Container->OnDestroyed.AddUniqueDynamic(this, &AObjectiveStep::HandleContainerDestroyed);
		}
		break;

	case EObjectiveCondition::RouteCompleted:
		if (IsValid(TrackedRoute))
			TrackedRoute->OnRouteCompleted.AddUniqueDynamic(this, &AObjectiveStep::HandleRouteCompleted);
		else
			Unset(TEXT("TrackedRoute"));
		break;

	case EObjectiveCondition::Interacted:
	{
		if (!IsValid(TrackedInteractable))
		{
			Unset(TEXT("TrackedInteractable"));
			break;
		}
		// Implementing IWorldInteractable is NOT what completes this beat — the raise is. Every
		// interactable calls NotifyWorldInteract from its own success branch (WorldInteract has no
		// return value and implementers routinely accept the call and refuse the action), so a
		// Blueprint interactable has to make that call itself.
		const bool bNativeRaiser = TrackedInteractable->IsA<ALootContainer>()
			|| TrackedInteractable->IsA<ALevelCompletionLiftGate>()
			|| TrackedInteractable->IsA<AExtractionTargetActor>()
			|| TrackedInteractable->IsA<AExtracteeCompanion>()
			|| TrackedInteractable->IsA<AExtracteeCharacter>()
			|| TrackedInteractable->IsA<AInteractionVolume>();
		if (!bNativeRaiser)
			UE_LOG(LogObjectiveStep, Warning,
				TEXT("'%s' (step '%s'): TrackedInteractable '%s' is not one of the actors that raise "
					 "NotifyWorldInteract natively — its Blueprint must call Notify World Interact on its "
					 "own success branch or this beat can never complete"),
				*GetName(), *GetEffectiveStepId().ToString(), *GetNameSafe(TrackedInteractable));
		if (UInteractionEventSubsystem* Events = World->GetSubsystem<UInteractionEventSubsystem>())
			Events->OnWorldInteractPerformed.AddUniqueDynamic(this, &AObjectiveStep::HandleWorldInteract);
		break;
	}

	case EObjectiveCondition::ExtracteeRescued:
		if (IsValid(TrackedExtractee))
			TrackedExtractee->OnRescued.AddUniqueDynamic(this, &AObjectiveStep::HandleExtracteeRescued);
		else
			Unset(TEXT("TrackedExtractee"));
		break;

	case EObjectiveCondition::WaveCompleted:
		// No id = no match. Binding anyway would let ANY wave finishing (including one that ends
		// with a None id) tick off a mis-wired beat.
		if (WatchedWaveId.IsNone())
		{
			Unset(TEXT("WatchedWaveId"));
			break;
		}
		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
		{
			Director->OnDirectorWaveCompleted.AddUniqueDynamic(this, &AObjectiveStep::HandleDirectorWaveCompleted);
			// A wave that cannot find spawn room never completes, and this beat is watching for that
			// completion — without the blocked toast the player holds an empty room forever with no
			// idea why the objective will not tick.
			Director->OnDirectorWaveBlocked.AddUniqueDynamic(this, &AObjectiveStep::HandleDirectorWaveBlocked);
		}
		break;

	case EObjectiveCondition::SurviveDuration:
		// The clock is this condition's "delegate" — armed on the same call every other condition
		// binds on, and torn down on the same call every other condition unbinds on.
		StartDefendCountdown();
		break;

	case EObjectiveCondition::ReachLocation:
	case EObjectiveCondition::Manual:
	default:
		break;
	}
}

void AObjectiveStep::UnbindConditionDelegates()
{
	UWorld* World = GetWorld();
	if (!World) return;

	if (UMissionInventorySubsystem* Inventory = World->GetSubsystem<UMissionInventorySubsystem>())
		Inventory->OnKeycardRecorded.RemoveDynamic(this, &AObjectiveStep::HandleKeycardRecorded);

	for (AEnemyCharacter* Enemy : TrackedEnemies)
	{
		if (!IsValid(Enemy)) continue;
		if (UHealthComponent* Health = Enemy->GetHealthComponent())
			Health->OnDeath.RemoveDynamic(this, &AObjectiveStep::HandleTrackedEnemyDeath);
		Enemy->OnDestroyed.RemoveDynamic(this, &AObjectiveStep::HandleTrackedEnemyDestroyed);
	}

	if (IsValid(TrackedDoor))
		TrackedDoor->OnDoorOpened.RemoveDynamic(this, &AObjectiveStep::HandleDoorOpened);

	for (ALootContainer* Container : TrackedContainers)
	{
		if (!IsValid(Container)) continue;
		Container->OnLootCompleted.RemoveDynamic(this, &AObjectiveStep::HandleLootCompleted);
		Container->OnDestroyed.RemoveDynamic(this, &AObjectiveStep::HandleContainerDestroyed);
	}

	if (IsValid(TrackedRoute))
		TrackedRoute->OnRouteCompleted.RemoveDynamic(this, &AObjectiveStep::HandleRouteCompleted);

	if (UInteractionEventSubsystem* Events = World->GetSubsystem<UInteractionEventSubsystem>())
		Events->OnWorldInteractPerformed.RemoveDynamic(this, &AObjectiveStep::HandleWorldInteract);

	if (IsValid(TrackedExtractee))
		TrackedExtractee->OnRescued.RemoveDynamic(this, &AObjectiveStep::HandleExtracteeRescued);

	if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
	{
		Director->OnDirectorWaveCompleted.RemoveDynamic(this, &AObjectiveStep::HandleDirectorWaveCompleted);
		Director->OnDirectorWaveBlocked.RemoveDynamic(this, &AObjectiveStep::HandleDirectorWaveBlocked);
	}

	// The countdown is this condition's timer equivalent of every other condition's delegate — it
	// belongs in the same teardown. Today Deactivate calls StopDefendCountdown separately above the
	// !bActive guard, so this is redundant but future-proof: a caller that unbinds without
	// deactivating must not leave a 1Hz writer running.
	StopDefendCountdown();
}

void AObjectiveStep::EvaluateCondition()
{
	if (!bActive || bCompleted) return;
	if (!IsConditionSatisfied())
	{
		UpdateMarker();
		return;
	}
	CompleteStep();
}

bool AObjectiveStep::IsConditionSatisfied() const
{
	const UWorld* World = GetWorld();

	switch (Condition)
	{
	case EObjectiveCondition::AcquireKeycard:
	{
		const UMissionInventorySubsystem* Inventory = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
		return Inventory && Inventory->HasKeycard(RequiredKeycardId);
	}
	case EObjectiveCondition::EnemiesDead:
		return FObjectiveConditionRules::AreEnemiesSatisfied(AuthoredEnemyCount, CountDownedEnemies());

	case EObjectiveCondition::DoorOpened:
		return IsValid(TrackedDoor) && TrackedDoor->IsOpenForAcoustics();

	case EObjectiveCondition::ContainerLooted:
		return FObjectiveConditionRules::AreContainersSatisfied(
			AuthoredContainerCount, CountSatisfiedContainers(), bRequiresAllContainers);

	case EObjectiveCondition::ExtracteeRescued:
		// Armed, not merely freed: OnRescued — the other way into this beat — fires at the handoff,
		// a beat after the VIP stands up. Polling on captive alone would complete the same beat one
		// stage early depending on which path got there first.
		return IsValid(TrackedExtractee) && !TrackedExtractee->IsCaptive() && TrackedExtractee->IsArmed();

	// ReachLocation, RouteCompleted, Interacted, WaveCompleted, SurviveDuration and Manual have no
	// queryable world state — their one-shot delegate or timer (or an explicit CompleteStep) is the
	// only way in.
	default:
		return false;
	}
}

void AObjectiveStep::StartDefendCountdown()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Clamped, not trusted: ClampMin only holds in the details panel, and a zero here would fire the
	// completion on the next tick and hand the player the beat.
	const float Duration = FMath::Max(DefendSeconds, DefendCountdownSeconds);
	World->GetTimerManager().SetTimer(DefendTimerHandle, this,
		&AObjectiveStep::HandleDefendElapsed, Duration, /*bLoop=*/false);
	World->GetTimerManager().SetTimer(DefendCountdownHandle, this,
		&AObjectiveStep::UpdateDefendCountdownLabel, DefendCountdownSeconds, /*bLoop=*/true);

	UE_LOG(LogObjectiveStep, Log, TEXT("Step '%s': holding for %.0fs"),
		*GetEffectiveStepId().ToString(), Duration);
}

void AObjectiveStep::StopDefendCountdown()
{
	const UWorld* World = GetWorld();
	if (!World) return;

	World->GetTimerManager().ClearTimer(DefendTimerHandle);
	World->GetTimerManager().ClearTimer(DefendCountdownHandle);
}

void AObjectiveStep::UpdateDefendCountdownLabel()
{
	UObjectiveSubsystem* Objectives = GetObjectiveSubsystem();
	const UWorld* World = GetWorld();
	if (!Objectives || !World) return;

	// Read back off the completion timer rather than tracked separately — one clock, so the number on
	// the HUD is the number that ends the beat even after a pause or a time-dilation.
	const float Remaining = FMath::Max(World->GetTimerManager().GetTimerRemaining(DefendTimerHandle), 0.f);
	const int32 WholeSeconds = FMath::CeilToInt(Remaining);
	const FText Clock = FText::FromString(FString::Printf(TEXT("%d:%02d"),
		WholeSeconds / SecondsPerMinute, WholeSeconds % SecondsPerMinute));

	// An unlabelled beat shows the bare clock. Prefixing "Objective" or similar would invent copy the
	// designer never wrote.
	//
	// The key hint is resolved FIRST and the clock formats over the result. Nothing double-substitutes:
	// FText::Format does not recurse into its argument values, so a resolved key inside DisplayLabel is
	// inert here, and the positional {0}/{1} of this pattern cannot collide with the named hint tokens.
	const FText DisplayLabel = BuildDisplayLabel();
	Objectives->UpdateObjectiveLabel(GetEffectiveStepId(), DisplayLabel.IsEmpty()
		? Clock
		: FText::Format(NSLOCTEXT("ObjectiveStep", "DefendCountdown", "{0} — {1}"), DisplayLabel, Clock));
}

void AObjectiveStep::HandleDefendElapsed()
{
	// The countdown goes first: CompleteStep re-registers the HUD line under the next beat's id, and
	// a surviving 1Hz writer would keep stamping this beat's clock over it.
	StopDefendCountdown();
	if (!bActive) return;
	CompleteStep();
}

void AObjectiveStep::CaptureEnemySnapshot()
{
	if (Condition != EObjectiveCondition::EnemiesDead) return;

	AuthoredEnemyCount = TrackedEnemies.Num();
	TrackedEnemySnapshot.Reset();
	TrackedEnemySnapshot.Reserve(AuthoredEnemyCount);

	FVector Sum = FVector::ZeroVector;
	for (AEnemyCharacter* Enemy : TrackedEnemies)
	{
		if (!IsValid(Enemy)) continue;
		TrackedEnemySnapshot.Add(Enemy);
		Sum += Enemy->GetActorLocation();
	}

	// Pin the marker to the ROOM, not to whoever is still standing in it: the anchor is the
	// spawn-time centroid, so a "clear the room" marker stops chasing the last survivor.
	if (!TrackedEnemySnapshot.IsEmpty())
		CapturedAreaAnchor = Sum / TrackedEnemySnapshot.Num();
}

int32 AObjectiveStep::CountDownedEnemies() const
{
	// Slots that were already gone at activation (killed before this beat went live) count as down —
	// otherwise a late-entry step deadlocks on enemies the player has already dealt with.
	int32 Downed = AuthoredEnemyCount - TrackedEnemySnapshot.Num();

	for (const TWeakObjectPtr<AEnemyCharacter>& Tracked : TrackedEnemySnapshot)
	{
		const AEnemyCharacter* Enemy = Tracked.Get();
		// IsActorBeingDestroyed as well as IsValid: HandleTrackedEnemyDestroyed evaluates from inside
		// the enemy's own OnDestroyed broadcast, where the actor is not garbage yet.
		if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed())
		{
			++Downed;
			continue;
		}
		const UHealthComponent* Health = Enemy->GetHealthComponent();
		if (IsValid(Health) && Health->IsDead()) ++Downed;
	}
	return Downed;
}

void AObjectiveStep::CaptureContainerSnapshot()
{
	if (Condition != EObjectiveCondition::ContainerLooted) return;

	AuthoredContainerCount = TrackedContainers.Num();
	TrackedContainerSnapshot.Reset();
	TrackedContainerSnapshot.Reserve(AuthoredContainerCount);

	for (ALootContainer* Container : TrackedContainers)
		if (IsValid(Container)) TrackedContainerSnapshot.Add(Container);
}

int32 AObjectiveStep::CountSatisfiedContainers() const
{
	// Two different kinds of missing container, and they must not be read the same way. A crate that
	// existed at activation and is gone now was blown up during the mission — its loot went with it,
	// and refusing to count it soft-locks the beat on a container that can never be searched. An
	// authored-null slot never entered the snapshot at all, so a seven-crate beat with six unwired
	// slots still stalls loudly on the mis-wire instead of ticking off on the first real loot.
	//
	// IsActorBeingDestroyed as well as IsValid: this runs from the container's own OnDestroyed
	// broadcast, and an actor is not garbage yet while it is announcing its destruction. Waiting for
	// the weak pointer to go stale means waiting for a re-evaluation that never comes.
	int32 Satisfied = 0;
	for (const TWeakObjectPtr<ALootContainer>& Tracked : TrackedContainerSnapshot)
	{
		const ALootContainer* Container = Tracked.Get();
		if (!IsValid(Container) || Container->IsActorBeingDestroyed() || Container->IsLooted()) ++Satisfied;
	}
	return Satisfied;
}

ALootContainer* AObjectiveStep::FindNearestUnlootedContainer() const
{
	FVector Origin = GetActorLocation();
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
		Origin = PlayerPawn->GetActorLocation();

	ALootContainer* Nearest = nullptr;
	float NearestDistanceSquared = TNumericLimits<float>::Max();
	for (ALootContainer* Container : TrackedContainers)
	{
		// IsActorBeingDestroyed as well as IsValid, for the same reason CountSatisfiedContainers reads
		// it: this resolves from inside a container's own OnDestroyed broadcast, where the actor is not
		// garbage yet. Without it the marker re-points at the dying crate, and on an all-rule beat with
		// an authored-null slot there is no later evaluation to correct it.
		if (!IsValid(Container) || Container->IsActorBeingDestroyed() || Container->IsLooted()) continue;
		const float DistanceSquared = FVector::DistSquared(Origin, Container->GetActorLocation());
		if (DistanceSquared >= NearestDistanceSquared) continue;
		NearestDistanceSquared = DistanceSquared;
		Nearest = Container;
	}
	return Nearest;
}

// ------------------------------------------------------------------
// Marker presentation
// ------------------------------------------------------------------

void AObjectiveStep::UpdateMarker()
{
	UObjectiveSubsystem* Objectives = GetObjectiveSubsystem();
	if (!Objectives || !bActive) return;

	// Resolved here rather than stored: the label the subsystem holds is what the text panel, the marker
	// widget layer and the world billboard all read, and only three of the four consumers are ours.
	const FText DisplayLabel = BuildDisplayLabel();

	if (bFreezeMarkerAtActivation)
	{
		if (!bMarkerFrozen)
		{
			FrozenMarkerLocation = CaptureFrozenMarkerLocation();
			bMarkerFrozen = true;
		}
		// Registered with NO target: handing one over would let the subsystem re-resolve against a
		// mover every frame, which is the whole thing this flag exists to stop.
		Objectives->AddObjective(GetEffectiveStepId(), DisplayLabel, FrozenMarkerLocation, nullptr,
			MarkerOffset, bShowWorldMarker, MarkerHeightAboveBase);
		return;
	}

	AActor* Target = ResolveMarkerTarget();
	const FVector Location = IsValid(Target) ? Target->GetActorLocation() : ResolveStaticMarkerLocation();
	Objectives->AddObjective(GetEffectiveStepId(), DisplayLabel, Location, Target,
		MarkerOffset, bShowWorldMarker, MarkerHeightAboveBase);
}

void AObjectiveStep::RemoveMarker()
{
	if (UObjectiveSubsystem* Objectives = GetObjectiveSubsystem())
		Objectives->RemoveObjective(GetEffectiveStepId());
}

AActor* AObjectiveStep::ResolveMarkerTarget() const
{
	if (IsValid(MarkerTarget)) return MarkerTarget;

	switch (Condition)
	{
	// The area anchor owns this one — see ResolveStaticMarkerLocation.
	case EObjectiveCondition::EnemiesDead:
		return nullptr;

	// Containers empty out from under the marker; re-point at whatever is still worth walking to.
	case EObjectiveCondition::ContainerLooted:
		return FindNearestUnlootedContainer();

	// The thing the condition watches is the thing the player has to walk to, so an unwired
	// MarkerTarget falls through to the payload rather than to this actor's own pin. Saves a wire
	// per beat, and the marker can never drift from what actually completes it.
	case EObjectiveCondition::DoorOpened:
		return IsValid(TrackedDoor) ? static_cast<AActor*>(TrackedDoor.Get()) : nullptr;
	case EObjectiveCondition::Interacted:
		return IsValid(TrackedInteractable) ? TrackedInteractable.Get() : nullptr;
	case EObjectiveCondition::ExtracteeRescued:
		return IsValid(TrackedExtractee) ? static_cast<AActor*>(TrackedExtractee.Get()) : nullptr;

	// ReachLocation / AcquireKeycard / RouteCompleted / WaveCompleted / SurviveDuration / Manual carry
	// no single actor worth pointing at — the step actor's own placed location is the marker. For a
	// defend beat that is exactly right: the marker holds the ground, not a squadmate standing on it.
	default:
		return nullptr;
	}
}

FVector AObjectiveStep::ResolveStaticMarkerLocation() const
{
	if (Condition != EObjectiveCondition::EnemiesDead) return GetActorLocation();

	const FVector Anchor = !AreaAnchorOverride.IsZero() ? AreaAnchorOverride : CapturedAreaAnchor;
	if (Anchor.IsZero()) return GetActorLocation();

	// Both anchors sample capsule centre; lift so the area marker reads at the same height as a
	// target-based marker resolved from a bounds base.
	return Anchor + FVector(0.f, 0.f, AreaMarkerLift);
}

FVector AObjectiveStep::CaptureFrozenMarkerLocation() const
{
	const AActor* Target = ResolveMarkerTarget();
	if (!IsValid(Target)) return ResolveStaticMarkerLocation();

	// Same bounds-base rule FObjectiveMarker applies to a following marker, sampled once. Taking
	// the raw actor location instead would drop the pin at capsule centre and the marker would
	// visibly sink the moment it froze.
	FVector Origin = FVector::ZeroVector;
	FVector Extents = FVector::ZeroVector;
	Target->GetActorBounds(false, Origin, Extents);
	return FVector(Origin.X, Origin.Y, Origin.Z - Extents.Z + MarkerHeightAboveBase);
}

UObjectiveSubsystem* AObjectiveStep::GetObjectiveSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UObjectiveSubsystem>() : nullptr;
}

void AObjectiveStep::RaiseCompletionToast() const
{
	UWorld* World = GetWorld();
	UMissionInventorySubsystem* Inventory = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!Inventory) return;

	// The resolved label, not the authored one: the toast goes out through OnLootNotify as a bare FText
	// with no id and no action attached, so this is the last point that can substitute anything. Pushing
	// Label raw would put "{PromptKey}" on the completion toast.
	const FText DisplayLabel = BuildDisplayLabel();
	Inventory->OnLootNotify.Broadcast(DisplayLabel.IsEmpty()
		? NSLOCTEXT("ObjectiveStep", "CompletedPlain", "Objective complete")
		: FText::Format(NSLOCTEXT("ObjectiveStep", "Completed", "Objective complete: {0}"), DisplayLabel));
}

// ------------------------------------------------------------------
// Label presentation
// ------------------------------------------------------------------

void AObjectiveStep::RefreshLabelHintTokenFlag()
{
	const FString LabelString = Label.ToString();
	bLabelHasHintToken = LabelString.Contains(BracedToken(PromptKeyToken))
		|| LabelString.Contains(BracedToken(SecondaryPromptKeyToken));
}

FText AObjectiveStep::BuildDisplayLabel() const
{
	if (Label.IsEmpty()) return Label;

	// Nothing authored and nothing wired — return the label untouched rather than round-tripping it
	// through FText::Format, so a label that legitimately contains a brace is never reinterpreted.
	const bool bHasAction = IsValid(PromptAction) || IsValid(SecondaryPromptAction);
	if (!bLabelHasHintToken && !bHasAction) return Label;

	// BOTH tokens are always supplied, even when only one action is wired. FText::Format leaves an
	// unmatched token in the output verbatim, so a label carrying "{PromptKey}" with nothing assigned
	// would otherwise put a literal brace on the HUD; "[unbound]" is the honest reading and the audit
	// names the mis-wire in the log.
	FFormatNamedArguments Arguments;
	Arguments.Add(PromptKeyToken, UKeybindHintLibrary::GetActionKeyText(this, PromptAction));
	Arguments.Add(SecondaryPromptKeyToken, UKeybindHintLibrary::GetActionKeyText(this, SecondaryPromptAction));
	return FText::Format(Label, Arguments);
}

void AObjectiveStep::BindMappingRebuildListener()
{
	// An OnActivate side effect can reach CompleteStep synchronously, which Deactivates (clearing
	// bActive) before ActivateInternal's own BindMappingRebuildListener call. Without this guard the
	// bind lands on a completed, inactive step with no matching unbind until EndPlay.
	if (!bActive) return;
	if (bMappingRebuildBound) return;
	if (!bLabelHasHintToken && !IsValid(PromptAction) && !IsValid(SecondaryPromptAction)) return;

	UEnhancedInputLocalPlayerSubsystem* Input = UKeybindHintLibrary::FindLocalPlayerInputSubsystem(this);
	if (!IsValid(Input))
	{
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): no local player input subsystem at activation — the key hint on this "
				 "line is stuck at whatever the mapping table read on this frame"),
			*GetName(), *GetEffectiveStepId().ToString());
		return;
	}

	Input->ControlMappingsRebuiltDelegate.AddUniqueDynamic(this, &AObjectiveStep::HandleControlMappingsRebuilt);
	bMappingRebuildBound = true;
}

void AObjectiveStep::UnbindMappingRebuildListener()
{
	if (!bMappingRebuildBound) return;
	bMappingRebuildBound = false;

	if (UEnhancedInputLocalPlayerSubsystem* Input = UKeybindHintLibrary::FindLocalPlayerInputSubsystem(this))
		Input->ControlMappingsRebuiltDelegate.RemoveDynamic(this, &AObjectiveStep::HandleControlMappingsRebuilt);
}

void AObjectiveStep::HandleControlMappingsRebuilt()
{
	if (!bActive) return;

	// A defend beat's line is owned by its 1Hz clock writer, which would stamp over a plain label write
	// within the second. Route through the clock so the re-resolved key survives.
	if (Condition == EObjectiveCondition::SurviveDuration)
	{
		UpdateDefendCountdownLabel();
		return;
	}

	if (UObjectiveSubsystem* Objectives = GetObjectiveSubsystem())
		Objectives->UpdateObjectiveLabel(GetEffectiveStepId(), BuildDisplayLabel());
}

// ------------------------------------------------------------------
// Side effects
// ------------------------------------------------------------------

void AObjectiveStep::RunSideEffects(EObjectiveSideEffectWhen When, bool bResumeReplay)
{
	for (const FObjectiveSideEffect& Effect : SideEffects)
	{
		if (Effect.When != When) continue;
		if (bResumeReplay && !Effect.ShouldReplayOnResume()) continue;
		ApplySideEffect(Effect, bResumeReplay);
	}
}

bool AObjectiveStep::StartsDirectorWave(FName WaveId) const
{
	if (WaveId.IsNone()) return false;
	return SideEffects.ContainsByPredicate([WaveId](const FObjectiveSideEffect& Effect)
	{
		return Effect.Type == EObjectiveSideEffectType::StartDirectorWave && Effect.WaveRequest.WaveId == WaveId;
	});
}

bool AObjectiveStep::HasReplayableSideEffects() const
{
	return SideEffects.ContainsByPredicate(
		[](const FObjectiveSideEffect& Effect) { return Effect.ShouldReplayOnResume(); });
}

void AObjectiveStep::ApplySideEffect(const FObjectiveSideEffect& Effect, bool bResumeReplay)
{
	UWorld* World = GetWorld();
	if (!World) return;

	auto Unset = [this](const TCHAR* Payload)
	{
		UE_LOG(LogObjectiveStep, Warning, TEXT("'%s' (step '%s'): side effect %s is unset — skipped"),
			*GetName(), *GetEffectiveStepId().ToString(), Payload);
	};

	switch (Effect.Type)
	{
	case EObjectiveSideEffectType::SetMissionPhase:
		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
			Director->SetMissionPhase(Effect.MissionPhase);
		break;

	case EObjectiveSideEffectType::StartDirectorWave:
		QueueDirectorWave(Effect.WaveRequest);
		break;

	case EObjectiveSideEffectType::UnlockGate:
		if (IsValid(Effect.GateTarget)) Effect.GateTarget->UnlockExit();
		else Unset(TEXT("GateTarget"));
		break;

	case EObjectiveSideEffectType::SetCompanionMode:
		if (ACompanionCharacter* Companion = ResolvePrimaryCompanion())
			Companion->SetMode(Effect.CompanionMode);
		else
			UE_LOG(LogObjectiveStep, Warning, TEXT("'%s' (step '%s'): SetCompanionMode with no primary companion"),
				*GetName(), *GetEffectiveStepId().ToString());
		break;

	case EObjectiveSideEffectType::CommandCompanionRoute:
		CommandCompanionRoute(Effect.RouteTarget.Get());
		break;

	case EObjectiveSideEffectType::CompleteLevel:
		if (AExtractionGameMode* GameMode = World->GetAuthGameMode<AExtractionGameMode>())
			GameMode->CompleteLevel();
		else
			UE_LOG(LogObjectiveStep, Warning, TEXT("'%s' (step '%s'): CompleteLevel with no AExtractionGameMode"),
				*GetName(), *GetEffectiveStepId().ToString());
		break;

	case EObjectiveSideEffectType::SetExtracteeRescuable:
		if (IsValid(Effect.ExtracteeTarget)) Effect.ExtracteeTarget->SetRescueEnabled(Effect.bRescuable);
		else Unset(TEXT("ExtracteeTarget"));
		break;

	case EObjectiveSideEffectType::ActivateActor:
		ApplyActivateActor(Effect, bResumeReplay);
		break;

	case EObjectiveSideEffectType::TripAlarm:
		// Ambient (non-wave) Director spawning is hard-gated on the alert being Loud, so a floor the
		// player cleared quietly would answer a defend beat with an empty room. The ladder is a
		// ratchet — this only ever forces it up, which is why it stays replayable on a resume.
		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
			Director->TripAlarm();
		break;

	case EObjectiveSideEffectType::TeleportSquad:
		TeleportSquad(Effect);
		break;

	case EObjectiveSideEffectType::SetDirectorSpawning:
		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
			Director->SetAmbientSpawningEnabled(Effect.bDirectorSpawningEnabled);
		break;

	case EObjectiveSideEffectType::SetDoorsLocked:
		for (const TObjectPtr<ADoorBase>& Door : Effect.DoorTargets)
		{
			if (!IsValid(Door)) continue;
			if (Effect.bDoorsLocked && Door->IsOpenForAcoustics())
			{
				UE_LOG(LogObjectiveStep, Warning,
					TEXT("'%s' (step '%s'): locking door %s that is already open — the lock prevents new opens but does not close the door"),
					*GetName(), *GetEffectiveStepId().ToString(), *Door->GetName());
			}
			Door->SetExternalGateLocked(Effect.bDoorsLocked);
		}
		break;
	}
}

void AObjectiveStep::ApplyActivateActor(const FObjectiveSideEffect& Effect, bool bResumeReplay)
{
	if (!IsValid(Effect.ActivateTarget))
	{
		UE_LOG(LogObjectiveStep, Warning, TEXT("'%s' (step '%s'): side effect ActivateTarget is unset — skipped"),
			*GetName(), *GetEffectiveStepId().ToString());
		return;
	}

	if (AExtractionTargetActor* ExtractionTarget = Cast<AExtractionTargetActor>(Effect.ActivateTarget.Get()))
	{
		// The step owns the HUD line under its own StepId — tell the target to keep its hands off
		// the objective panel first, or the one beat reads as two.
		ExtractionTarget->SetObjectiveManagedExternally(true);
		ExtractionTarget->ActivateTarget();
		return;
	}

	// The endgame shape: a hold-to-interact box that stays dark until the beat before it asks for it,
	// so the player cannot call for extraction ahead of the mission.
	if (AInteractionVolume* Volume = Cast<AInteractionVolume>(Effect.ActivateTarget.Get()))
	{
		Volume->SetInteractionEnabled(true);
		return;
	}

	if (AObjectiveStep* Step = Cast<AObjectiveStep>(Effect.ActivateTarget.Get()))
	{
		// Hand the filter on. This is the one branch where a resume reaches a beat it is not itself
		// replaying, and that beat's OnActivate effects are exactly as dangerous to repeat.
		// ActivateInternal owns the other half — a target at or after the resume point drops the
		// filter, and the resume point itself is held back for the post-teleport activation.
		Step->ActivateInternal(bResumeReplay);
		return;
	}

	UE_LOG(LogObjectiveStep, Warning,
		TEXT("'%s' (step '%s'): ActivateActor target '%s' is not an extraction target, an interaction "
			 "volume or an objective step — nothing to activate"),
		*GetName(), *GetEffectiveStepId().ToString(), *GetNameSafe(Effect.ActivateTarget));
}

void AObjectiveStep::CommandCompanionRoute(ACompanionRoute* Route)
{
	if (!IsValid(Route))
	{
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): CommandCompanionRoute has no RouteTarget — the companion is not sent anywhere"),
			*GetName(), *GetEffectiveStepId().ToString());
		return;
	}

	ACompanionCharacter* Companion = ResolvePrimaryCompanion();
	if (!IsValid(Companion))
	{
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): CommandCompanionRoute with no primary companion — route '%s' not started"),
			*GetName(), *GetEffectiveStepId().ToString(), *GetNameSafe(Route));
		return;
	}

	ACompanionAIController* CompanionController = Companion->GetController<ACompanionAIController>();
	if (!IsValid(CompanionController))
	{
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): the primary companion has no ACompanionAIController — route '%s' not started"),
			*GetName(), *GetEffectiveStepId().ToString(), *GetNameSafe(Route));
		return;
	}

	// The same one call a placed ACompanionRouteTrigger makes. StartRoute owns the empty-route
	// refusal, the already-running guard and every blackboard write, so this stays a hand-off.
	CompanionController->StartRoute(Route);
}

void AObjectiveStep::TeleportSquad(const FObjectiveSideEffect& Effect)
{
	if (!IsValid(Effect.PlayerDestination))
	{
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): TeleportSquad has no PlayerDestination — the squad is not moved"),
			*GetName(), *GetEffectiveStepId().ToString());
		return;
	}

	// Companions FIRST, unconditionally. The player pawn may not exist yet (actor BeginPlay order
	// vs player spawn is unspecified on a resume), but every possessed companion is already in the
	// world and standing on a floor the mission is about to leave behind.
	TArray<ACompanionCharacter*> Companions;
	GatherSquadCompanions(Companions);

	for (int32 Index = 0; Index < Companions.Num(); ++Index)
	{
		const AActor* Destination = Effect.CompanionDestinations.IsValidIndex(Index)
			? Effect.CompanionDestinations[Index].Get() : nullptr;
		if (!IsValid(Destination)) continue;
		TeleportCompanionToDestination(Companions[Index], Destination);
	}

	const FVector PlayerLocation = Effect.PlayerDestination->GetActorLocation();
	// Yaw only: a destination actor left pitched or rolled in the viewport would tip the camera.
	const FRotator PlayerRotation(0.f, Effect.PlayerDestination->GetActorRotation().Yaw, 0.f);

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerPawn))
	{
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): TeleportSquad has no player pawn — deferring player teleport"),
			*GetName(), *GetEffectiveStepId().ToString());
		DeferTeleportSquad(Effect);
		return;
	}

	// TeleportTo reaches UCharacterMovementComponent::OnTeleported(), which saves the base
	// location, sets bJustTeleported and corrects the movement mode when the destination has no
	// walkable floor. SetActorLocationAndRotation skips all three, so a pawn still BASED ON THE
	// LIFT would keep accumulating the old base's delta.
	if (!PlayerPawn->TeleportTo(PlayerLocation, PlayerRotation))
		PlayerPawn->TeleportTo(PlayerLocation, PlayerRotation, /*bIsATest=*/false, /*bNoCheck=*/true);
	if (AController* PlayerController = PlayerPawn->GetController())
		PlayerController->SetControlRotation(PlayerRotation);
}

void AObjectiveStep::GatherSquadCompanions(TArray<ACompanionCharacter*>& OutCompanions)
{
	UWorld* World = GetWorld();
	if (!World) return;

	// A captive extractee has no controller and sits at its staged spot — the checkpoint teleport at
	// TryApplyCheckpointSpawn filters on the same criterion. A dead or DBNO companion is ragdolled;
	// a direct location set leaves its physics body behind, so skipping it is the only safe path.
	auto IsMoveable = [](const ACompanionCharacter* Companion)
	{
		if (!IsValid(Companion) || !Companion->GetController()) return false;
		const UHealthComponent* Health = Companion->GetHealthComponent();
		return IsValid(Health) && Health->IsAlive() && !Companion->GetIsCompanionDBNO();
	};

	TArray<ACompanionCharacter*> InLevel;
	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
		if (IsMoveable(*It)) InLevel.Add(*It);

	OutCompanions.Reserve(InLevel.Num());

	// Slot 0 is the primary, always. Actor iteration order is not the order the designer thinks
	// in — pinning the primary is what makes a hand-authored destination list mean the same thing
	// on every run.
	ACompanionCharacter* Primary = ResolvePrimaryCompanion();
	if (IsValid(Primary) && IsMoveable(Primary)) OutCompanions.Add(Primary);

	for (ACompanionCharacter* Companion : InLevel)
	{
		if (Companion == Primary) continue;
		OutCompanions.Add(Companion);
	}
}

void AObjectiveStep::TeleportCompanionToDestination(ACompanionCharacter* Companion, const AActor* Destination) const
{
	if (!IsValid(Companion) || !IsValid(Destination)) return;

	const FVector Location = Destination->GetActorLocation();
	const FRotator Rotation(0.f, Destination->GetActorRotation().Yaw, 0.f);

	// The controller's own teleport, the same call ATeleportVolume makes: it cancels a traversal in
	// flight, drops the active move order and projects onto the navmesh. Moving the actor directly
	// would leave the order standing and the companion would walk straight back to the old floor.
	ACompanionAIController* CompanionController = Companion->GetController<ACompanionAIController>();
	if (IsValid(CompanionController))
		CompanionController->TeleportToLocation(Location, Rotation);

	// TeleportToLocation returns true on a successful nav projection regardless of whether the
	// underlying TeleportTo actually landed the pawn (encroachment). Verify arrival by position.
	// Split horizontal and vertical: the controller snaps to the navmesh and TeleportTo's
	// encroachment resolution lifts the capsule to resting contact, so a successful move settles
	// ~88cm above a floor-level destination. A single 3D radius would spend most of its budget on
	// that vertical offset and leave no headroom for the horizontal snap.
	const FVector Delta = Companion->GetActorLocation() - Location;
	const bool bArrived = Delta.SizeSquared2D() < CompanionArrivalToleranceSq
		&& FMath::Abs(Delta.Z) < CompanionArrivalHeightTolerance;
	if (!bArrived)
	{
		if (!Companion->TeleportTo(Location, Rotation))
			Companion->TeleportTo(Location, Rotation, /*bIsATest=*/false, /*bNoCheck=*/true);
	}
}

void AObjectiveStep::DeferTeleportSquad(const FObjectiveSideEffect& Effect)
{
	UWorld* World = GetWorld();
	if (!World) return;

	// On a checkpoint resume BeginCheckpointSpawn and the resume step's OnActivate TeleportSquad can
	// both arm pawn-retry timers at the same 0.25s cadence. Both call TeleportTo on the same pawn
	// and the winner is whichever the timer heap pops last — unspecified. There must be exactly one
	// owner of the player's landing spot, so drop the checkpoint's retry and take over.
	World->GetTimerManager().ClearTimer(CheckpointSpawnRetryHandle);

	PendingSquadTeleport = Effect;

	if (!World->GetTimerManager().IsTimerActive(SquadTeleportRetryHandle))
	{
		SquadTeleportRetries = 0;
		World->GetTimerManager().SetTimer(SquadTeleportRetryHandle, this,
			&AObjectiveStep::TryApplyDeferredSquadTeleport, CheckpointSpawnPollSeconds, /*bLoop=*/true);
	}
}

void AObjectiveStep::TryApplyDeferredSquadTeleport()
{
	UWorld* World = GetWorld();
	if (!World) return;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerPawn))
	{
		if (++SquadTeleportRetries <= MaxCheckpointSpawnRetries) return;

		World->GetTimerManager().ClearTimer(SquadTeleportRetryHandle);
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): no player pawn after %d squad-teleport polls — player teleport skipped"),
			*GetName(), *GetEffectiveStepId().ToString(), SquadTeleportRetries);
		return;
	}
	World->GetTimerManager().ClearTimer(SquadTeleportRetryHandle);

	if (!IsValid(PendingSquadTeleport.PlayerDestination))
	{
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): deferred TeleportSquad lost its PlayerDestination — player teleport skipped"),
			*GetName(), *GetEffectiveStepId().ToString());
		return;
	}

	const FVector Location = PendingSquadTeleport.PlayerDestination->GetActorLocation();
	const FRotator Rotation(0.f, PendingSquadTeleport.PlayerDestination->GetActorRotation().Yaw, 0.f);

	if (!PlayerPawn->TeleportTo(Location, Rotation))
		PlayerPawn->TeleportTo(Location, Rotation, /*bIsATest=*/false, /*bNoCheck=*/true);
	if (AController* PlayerController = PlayerPawn->GetController())
		PlayerController->SetControlRotation(Rotation);

	UE_LOG(LogObjectiveStep, Log,
		TEXT("'%s' (step '%s'): deferred squad teleport — player landed after %d poll(s)"),
		*GetName(), *GetEffectiveStepId().ToString(), SquadTeleportRetries);
}

void AObjectiveStep::QueueDirectorWave(const FDirectorWaveRequest& Request)
{
	if (Request.WaveId.IsNone())
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): WaveRequest.WaveId is None — completion matching is by id, so name it"),
			*GetName(), *GetEffectiveStepId().ToString());

	PendingWaveRequests.Add(Request);
	TryStartPendingWaves();
}

void AObjectiveStep::TryStartPendingWaves()
{
	UWorld* World = GetWorld();
	if (!World) return;

	UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>();
	if (IsValid(Director))
	{
		for (int32 Index = PendingWaveRequests.Num() - 1; Index >= 0; --Index)
		{
			const FDirectorWaveRequest& Request = PendingWaveRequests[Index];
			// Idempotent: a retry landing after the wave took hold must not start a second one.
			const bool bAlreadyRunning = !Request.WaveId.IsNone() && Director->GetActiveWaveId() == Request.WaveId;
			if (bAlreadyRunning || Director->StartWave(Request))
				PendingWaveRequests.RemoveAt(Index);
		}
	}

	if (PendingWaveRequests.IsEmpty())
	{
		World->GetTimerManager().ClearTimer(WaveRetryHandle);
		return;
	}
	if (World->GetTimerManager().IsTimerActive(WaveRetryHandle)) return;

	// StartWave refuses while another wave is still running, and the beat that asked for this one
	// is one-shot — keep knocking rather than soft-lock the mission on a silently dropped wave.
	UE_LOG(LogObjectiveStep, Warning,
		TEXT("'%s' (step '%s'): director refused %d wave request(s) — retrying every %.0fs"),
		*GetName(), *GetEffectiveStepId().ToString(), PendingWaveRequests.Num(), WaveRetrySeconds);
	World->GetTimerManager().SetTimer(WaveRetryHandle, this,
		&AObjectiveStep::TryStartPendingWaves, WaveRetrySeconds, /*bLoop=*/true);
}

// ------------------------------------------------------------------
// Checkpoints
// ------------------------------------------------------------------

void AObjectiveStep::RecordCheckpoint()
{
	UExtractionGameInstance* GameInstance = Cast<UExtractionGameInstance>(GetGameInstance());
	if (!GameInstance) return;

	const FName LevelName(*UGameplayStatics::GetCurrentLevelName(this, true));
	GameInstance->SetStepCheckpoint(LevelName, GetEffectiveStepId());
	UE_LOG(LogObjectiveStep, Log, TEXT("Checkpoint recorded at step '%s'"), *GetEffectiveStepId().ToString());
}

ACompanionCharacter* AObjectiveStep::ResolvePrimaryCompanion()
{
	ACompanionCharacter* Companion = CachedCompanion.Get();
	if (IsValid(Companion)) return Companion;

	// Primary only — the extractee joins fully healed at rescue, owns its own recovery, and is
	// never the one taking scripted orders.
	UWorld* World = GetWorld();
	Companion = World ? ACompanionCharacter::GetPrimaryCompanion(World) : nullptr;
	CachedCompanion = Companion;
	return IsValid(Companion) ? Companion : nullptr;
}

void AObjectiveStep::TryApplyCompanionCheckpointHeal()
{
	UWorld* World = GetWorld();
	if (!World) return;

	ACompanionCharacter* Companion = ResolvePrimaryCompanion();
	if (!IsValid(Companion))
	{
		UE_LOG(LogObjectiveStep, Warning, TEXT("Checkpoint heal dropped at step '%s' — no companion in level"),
			*GetEffectiveStepId().ToString());
		World->GetTimerManager().ClearTimer(CheckpointHealPollHandle);
		return;
	}

	// DBNO holds the heal — the revive flow owns recovery; the full heal lands after revive.
	bool bInCombat = Companion->GetIsCompanionDBNO();
	if (!bInCombat)
	{
		if (const AAIController* AICtl = Cast<AAIController>(Companion->GetController()))
			if (const UBlackboardComponent* Blackboard = AICtl->GetBlackboardComponent())
				bInCombat = IsValid(Blackboard->GetValueAsObject(ACompanionAIController::BB_CombatTarget));
	}

	if (bInCombat)
	{
		if (!World->GetTimerManager().IsTimerActive(CheckpointHealPollHandle))
			World->GetTimerManager().SetTimer(CheckpointHealPollHandle, this,
				&AObjectiveStep::TryApplyCompanionCheckpointHeal, CheckpointHealPollSeconds, /*bLoop=*/true);
		return;
	}

	World->GetTimerManager().ClearTimer(CheckpointHealPollHandle);
	UHealthComponent* Health = Companion->GetHealthComponent();
	if (!IsValid(Health) || !Health->IsAlive() || Health->GetCurrentHealth() >= Health->GetMaxHealth()) return;

	Health->Heal(Health->GetMaxHealth());
	UE_LOG(LogObjectiveStep, Log, TEXT("Checkpoint step '%s' reached — companion healed to full"),
		*GetEffectiveStepId().ToString());
}

bool AObjectiveStep::TryResumeFromCheckpoint()
{
	const UExtractionGameInstance* GameInstance = Cast<UExtractionGameInstance>(GetGameInstance());
	if (!GameInstance) return false;

	const FName LevelName(*UGameplayStatics::GetCurrentLevelName(this, true));
	const FName RecordedId = GameInstance->GetStepCheckpointForLevel(LevelName);
	if (RecordedId.IsNone()) return false;

	// The entry step recorded as its own checkpoint still owes the player the teleport. There is
	// nothing to fast-forward, but falling through to the plain entry activation drops the party at
	// PlayerStart instead of at the CheckpointSpawn the designer wired to this beat.
	if (RecordedId == GetEffectiveStepId())
	{
		BeginCheckpointSpawn();
		Activate();
		return true;
	}

	TArray<AObjectiveStep*> Earlier;
	AObjectiveStep* Resume = FObjectiveChainWalker::SplitAtId(this, &NextOf, &IdOf, RecordedId, Earlier);
	if (!IsValid(Resume))
	{
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s': recorded checkpoint '%s' is not on this chain — starting from the entry step"),
			*GetName(), *RecordedId.ToString());
		return false;
	}

	UE_LOG(LogObjectiveStep, Log, TEXT("'%s': resuming at checkpoint step '%s' (%d earlier beat(s) applied)"),
		*GetName(), *RecordedId.ToString(), Earlier.Num());

	// Every earlier beat's end-state, derived from its own condition — there is no second body of
	// restore code to keep in sync with the chain.
	ReplayEarlierSteps(Earlier, Resume);

	// Place the party BEFORE the beat goes live: the resume step's marker (and a ReachLocation
	// sphere) then resolve against where the player actually is, not the level-start position.
	Resume->BeginCheckpointSpawn();
	Resume->Activate();
	return true;
}

void AObjectiveStep::ReplayEarlierSteps(const TArray<AObjectiveStep*>& Earlier, AObjectiveStep* Resume)
{
	FResumeWalk Walk;
	Walk.ResumePoint = Resume;

	// The tail is read once, before anything runs: a beat at or after the resume point is one the
	// player has never reached, so a prefix beat standing it up early must hand it a live activation.
	if (IsValid(Resume))
	{
		TArray<AObjectiveStep*> Tail;
		FObjectiveChainWalker::Walk(Resume, &NextOf, Tail);
		Walk.AtOrAfterResume.Reserve(Tail.Num());
		for (AObjectiveStep* Step : Tail) Walk.AtOrAfterResume.Add(Step);
	}

	// ONE scope for the whole walk, not one per beat: a step an earlier prefix beat stood up through
	// ActivateActor is routinely completed by a LATER prefix beat's end-state — a keycard re-granted,
	// the enemies it watched destroyed — with nothing left on the stack to say the completion belongs
	// to a level load.
	const FScopedResumeWalk Scope(Walk);

	for (AObjectiveStep* Step : Earlier)
		if (IsValid(Step)) Step->ApplyCompletedWorldState();
}

void AObjectiveStep::ApplyCompletedWorldState()
{
	if (bCompleted) return;

	// An earlier beat's ActivateActor effect can have already stood this one up during the same
	// fast-forward. Marking it completed without standing it down again leaves its HUD objective
	// line registered and its condition delegates bound for a beat the resume has just applied —
	// the player restarts staring at an objective for something already done.
	if (bActive) Deactivate();

	bCompleted = true;

	// Same order a live run took: entry effects, then the world the player left behind, then the
	// completion effects. An OnActivate effect is as much part of the beat as its completion — the
	// optional-supplies chain is nothing but an OnActivate ActivateActor, and dropping it here
	// silently deletes an objective from the resumed mission.
	RunSideEffects(EObjectiveSideEffectWhen::OnActivate, /*bResumeReplay=*/true);

	switch (Condition)
	{
	case EObjectiveCondition::DoorOpened:
		if (IsValid(TrackedDoor))
		{
			TrackedDoor->ForceOpenInstant();
			RetireModeGatesForDoor(TrackedDoor.Get());
		}
		break;

	case EObjectiveCondition::EnemiesDead:
		for (AEnemyCharacter* Enemy : TrackedEnemies)
			if (IsValid(Enemy)) Enemy->Destroy();
		break;

	case EObjectiveCondition::ContainerLooted:
		ApplyLootedContainers();
		break;

	case EObjectiveCondition::AcquireKeycard:
		// Keycards are kept, not consumed — re-granting is correct at every later step. Silent:
		// a resume is not an acquisition, so it raises no pickup toast.
		if (!RequiredKeycardId.IsNone())
			if (UMissionInventorySubsystem* Inventory = GetWorld() ? GetWorld()->GetSubsystem<UMissionInventorySubsystem>() : nullptr)
				Inventory->RecordKeycard(RequiredKeycardId, /*bSilent=*/true);
		break;

	case EObjectiveCondition::ExtracteeRescued:
		// Without this a resume past the rescue leaves the VIP kneeling, captive and un-controllered
		// — a hostage the player already freed, still tied up, with no interact left to free him.
		if (IsValid(TrackedExtractee)) TrackedExtractee->ForceRescue();
		break;

	// ReachLocation / Interacted / RouteCompleted / WaveCompleted / Manual leave no world state
	// behind — only their side effects need replaying. SurviveDuration is the same: a hold the player
	// already stood through is a stretch of time, not a change to the level, and the Deactivate above
	// guarantees a fast-forward never leaves its countdown running.
	default:
		break;
	}

	RunSideEffects(EObjectiveSideEffectWhen::OnComplete, /*bResumeReplay=*/true);
}

void AObjectiveStep::ApplyLootedContainers()
{
	// MarkLootedForCheckpoint, never Loot(): this runs from the entry step's BeginPlay, before the
	// player pawn is guaranteed to exist, and the real loot path resolves ammo/stims against that
	// pawn — a resume through Loot() flips bLooted and silently burns everything but the keycard.
	// It is also silent by design: no OnOpened, no SFX, no OnLootCompleted (which the legacy flow
	// still listens on for these very containers).
	// An "any one" beat only ever consumed a single container, so a resume grants a single one and
	// leaves the rest lootable — which is exactly what an optional-supplies beat should look like.
	for (ALootContainer* Container : TrackedContainers)
	{
		if (!IsValid(Container) || Container->IsLooted()) continue;
		Container->MarkLootedForCheckpoint();
		if (!bRequiresAllContainers) return;
	}
}

void AObjectiveStep::RetireModeGatesForDoor(const ADoorBase* Door) const
{
	UWorld* World = GetWorld();
	if (!World || !Door) return;

	for (TActorIterator<ACompanionModeDoorGate> It(World); It; ++It)
		if (It->GetTargetDoor() == Door) It->ForceUnlockForCheckpoint();
}

void AObjectiveStep::BeginCheckpointSpawn()
{
	CheckpointSpawnRetries = 0;
	TryApplyCheckpointSpawn();
}

void AObjectiveStep::TryApplyCheckpointSpawn()
{
	UWorld* World = GetWorld();
	if (!World) return;

	if (!IsValid(CheckpointSpawn))
	{
		// Reached from the retry timer too — leaving it armed re-logs this every 0.25s for the
		// rest of the level.
		World->GetTimerManager().ClearTimer(CheckpointSpawnRetryHandle);
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): no CheckpointSpawn — resuming at the level-start position"),
			*GetName(), *GetEffectiveStepId().ToString());
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerPawn))
	{
		// Player spawn order vs level-actor BeginPlay is not guaranteed — poll briefly.
		if (++CheckpointSpawnRetries <= MaxCheckpointSpawnRetries)
		{
			if (!World->GetTimerManager().IsTimerActive(CheckpointSpawnRetryHandle))
				World->GetTimerManager().SetTimer(CheckpointSpawnRetryHandle, this,
					&AObjectiveStep::TryApplyCheckpointSpawn, CheckpointSpawnPollSeconds, /*bLoop=*/true);
			return;
		}
		World->GetTimerManager().ClearTimer(CheckpointSpawnRetryHandle);
		UE_LOG(LogObjectiveStep, Warning, TEXT("'%s': no player pawn after %d spawn polls — checkpoint teleport skipped"),
			*GetName(), CheckpointSpawnRetries);
		return;
	}
	World->GetTimerManager().ClearTimer(CheckpointSpawnRetryHandle);

	const FVector SpawnLocation = CheckpointSpawn->GetActorLocation();
	const FRotator SpawnRotation(0.f, CheckpointSpawn->GetActorRotation().Yaw, 0.f);

	// TeleportTo fails on encroachment (a misplaced TargetPoint) — force the drop rather than
	// silently leaving the pawn at level start with the world already fast-forwarded.
	if (!PlayerPawn->TeleportTo(SpawnLocation, SpawnRotation))
	{
		UE_LOG(LogObjectiveStep, Warning, TEXT("'%s': checkpoint spawn encroached at %s — forcing player teleport"),
			*GetName(), *SpawnLocation.ToCompactString());
		PlayerPawn->TeleportTo(SpawnLocation, SpawnRotation, /*bIsATest*/ false, /*bNoCheck*/ true);
	}
	if (AController* PlayerController = PlayerPawn->GetController())
		PlayerController->SetControlRotation(SpawnRotation);

	// Every possessed companion lands beside the player (alternating sides); a captive extractee
	// has no controller and stays at its staged spot.
	int32 CompanionsMoved = 0;
	for (TActorIterator<ACompanionCharacter> It(World); It; ++It)
	{
		ACompanionCharacter* Companion = *It;
		if (!IsValid(Companion) || !Companion->GetController()) continue;

		const float Side = (CompanionsMoved % 2 == 0) ? CompanionSpawnSideOffset : -CompanionSpawnSideOffset;
		const FVector CompanionLocation = SpawnLocation + CheckpointSpawn->GetActorRightVector() * Side;
		if (!Companion->TeleportTo(CompanionLocation, SpawnRotation))
			Companion->TeleportTo(SpawnLocation, SpawnRotation, /*bIsATest*/ false, /*bNoCheck*/ true);
		++CompanionsMoved;
	}

	UE_LOG(LogObjectiveStep, Log, TEXT("'%s': checkpoint resume — player + %d companion(s) teleported to step '%s'"),
		*GetName(), CompanionsMoved, *GetEffectiveStepId().ToString());
}

// ------------------------------------------------------------------
// Validation
// ------------------------------------------------------------------

void AObjectiveStep::AuditStepWiring()
{
	auto Missing = [this](const TCHAR* Payload)
	{
		UE_LOG(LogObjectiveStep, Warning, TEXT("'%s' (step '%s'): %s is unset — this beat can never complete"),
			*GetName(), *GetEffectiveStepId().ToString(), Payload);
	};

	switch (Condition)
	{
	case EObjectiveCondition::ReachLocation:
		if (CompletionRadius <= 0.f) Missing(TEXT("CompletionRadius"));
		break;
	case EObjectiveCondition::AcquireKeycard:
		if (RequiredKeycardId.IsNone()) Missing(TEXT("RequiredKeycardId"));
		break;
	case EObjectiveCondition::EnemiesDead:
		if (TrackedEnemies.IsEmpty()) Missing(TEXT("TrackedEnemies"));
		for (int32 Index = 0; Index < TrackedEnemies.Num(); ++Index)
			if (!IsValid(TrackedEnemies[Index]))
				UE_LOG(LogObjectiveStep, Warning,
					TEXT("'%s' (step '%s'): TrackedEnemies[%d] is unset or already gone — counted as already down"),
					*GetName(), *GetEffectiveStepId().ToString(), Index);
		break;
	case EObjectiveCondition::DoorOpened:
		if (!IsValid(TrackedDoor)) Missing(TEXT("TrackedDoor"));
		break;
	case EObjectiveCondition::ContainerLooted:
		if (TrackedContainers.IsEmpty()) Missing(TEXT("TrackedContainers"));
		for (int32 Index = 0; Index < TrackedContainers.Num(); ++Index)
			if (!IsValid(TrackedContainers[Index]))
				UE_LOG(LogObjectiveStep, Warning,
					TEXT("'%s' (step '%s'): TrackedContainers[%d] is unset — an authored-null slot never counts "
						 "as searched (unlike a crate destroyed mid-mission, which does), so an all-containers "
						 "beat can never complete"),
					*GetName(), *GetEffectiveStepId().ToString(), Index);
		break;
	case EObjectiveCondition::RouteCompleted:
		if (!IsValid(TrackedRoute)) Missing(TEXT("TrackedRoute"));
		break;
	case EObjectiveCondition::Interacted:
		if (!IsValid(TrackedInteractable)) Missing(TEXT("TrackedInteractable"));
		break;
	case EObjectiveCondition::ExtracteeRescued:
		if (!IsValid(TrackedExtractee)) Missing(TEXT("TrackedExtractee"));
		break;
	case EObjectiveCondition::WaveCompleted:
		if (WatchedWaveId.IsNone()) Missing(TEXT("WatchedWaveId"));
		break;
	case EObjectiveCondition::SurviveDuration:
		if (DefendSeconds <= 0.f) Missing(TEXT("DefendSeconds"));
		break;
	case EObjectiveCondition::Manual:
	default:
		break;
	}

	AuditPromptKeyHint();

	for (const FObjectiveSideEffect& Effect : SideEffects)
	{
		if (Effect.Type == EObjectiveSideEffectType::TeleportSquad) AuditTeleportSquad(Effect);

		if (Effect.Type == EObjectiveSideEffectType::SetDoorsLocked)
		{
			if (Effect.DoorTargets.IsEmpty())
			{
				UE_LOG(LogObjectiveStep, Warning,
					TEXT("'%s' (step '%s'): SetDoorsLocked has no DoorTargets — the exits stay unlocked and the defend can be skipped"),
					*GetName(), *GetEffectiveStepId().ToString());
			}
			else
			{
				// Collect all mode-gate target doors once, rather than running a full
				// actor scan per DoorTargets entry.
				TArray<TPair<ADoorBase*, ACompanionModeDoorGate*>, TInlineAllocator<4>> GatedDoors;
				if (UWorld* World = GetWorld())
				{
					for (TActorIterator<ACompanionModeDoorGate> GateIt(World); GateIt; ++GateIt)
					{
						ADoorBase* GateDoor = GateIt->GetTargetDoor();
						if (IsValid(GateDoor))
							GatedDoors.Emplace(GateDoor, *GateIt);
					}
				}

				for (int32 Index = 0; Index < Effect.DoorTargets.Num(); ++Index)
				{
					if (!IsValid(Effect.DoorTargets[Index]))
					{
						UE_LOG(LogObjectiveStep, Warning,
							TEXT("'%s' (step '%s'): SetDoorsLocked DoorTargets[%d] is unset — that door will not be locked"),
							*GetName(), *GetEffectiveStepId().ToString(), Index);
						continue;
					}
					// A CompanionModeDoorGate that targets the same door writes the same
					// bExternalGateLocked bool. If the gate unlocks mid-defend (the player
					// sets the required mode), the lock placed by this effect is silently
					// cleared and the defend can be escaped.
					for (const auto& [GateDoor, Gate] : GatedDoors)
					{
						if (GateDoor == Effect.DoorTargets[Index].Get())
						{
							UE_LOG(LogObjectiveStep, Warning,
								TEXT("'%s' (step '%s'): SetDoorsLocked DoorTargets[%d] (%s) is also targeted by CompanionModeDoorGate %s — a mode change mid-defend will silently unlock it"),
								*GetName(), *GetEffectiveStepId().ToString(), Index,
								*Effect.DoorTargets[Index]->GetName(), *Gate->GetName());
						}
					}
				}
			}
		}
	}

	// Braces are load-bearing: UE_LOG expands to a braced block, so an unbraced if-body ends the
	// statement and orphans the else.
	if (NextStep.Get() == this)
	{
		UE_LOG(LogObjectiveStep, Error, TEXT("'%s' (step '%s'): NextStep points at itself — the chain cannot advance"),
			*GetName(), *GetEffectiveStepId().ToString());
	}
	else if (FObjectiveChainWalker::HasCycle(this, &NextOf))
	{
		UE_LOG(LogObjectiveStep, Error, TEXT("'%s' (step '%s'): the chain from this step loops back on itself"),
			*GetName(), *GetEffectiveStepId().ToString());
	}

	if (bIsCheckpoint && !IsValid(CheckpointSpawn))
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): checkpoint step has no CheckpointSpawn — a restart resumes at the level-start position"),
			*GetName(), *GetEffectiveStepId().ToString());
}

void AObjectiveStep::AuditPromptKeyHint() const
{
	const FString LabelString = Label.ToString();

	// Deliberately does NOT report "assigned but nothing bound": this runs at activation, and Enhanced
	// Input has not rebuilt its mapping table by then, so every hinted beat would warn on the frame it
	// goes live. The rebuild listener is what corrects the line; a genuinely unbound action shows up as
	// "[unbound]" on the HUD, which is louder than a log line anyway.
	auto AuditSlot = [this, &LabelString](const FString& Token, const UInputAction* Action, const TCHAR* PropertyName)
	{
		const bool bHasToken = LabelString.Contains(BracedToken(Token));
		const bool bHasAction = IsValid(Action);
		if (bHasToken == bHasAction) return;

		if (bHasToken)
		{
			UE_LOG(LogObjectiveStep, Warning,
				TEXT("'%s' (step '%s'): Label contains {%s} but %s is unset — the line will read \"[unbound]\""),
				*GetName(), *GetEffectiveStepId().ToString(), *Token, PropertyName);
			return;
		}

		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): %s is assigned but Label has no {%s} token — the key hint will not appear"),
			*GetName(), *GetEffectiveStepId().ToString(), PropertyName, *Token);
	};

	AuditSlot(PromptKeyToken, PromptAction, TEXT("PromptAction"));
	AuditSlot(SecondaryPromptKeyToken, SecondaryPromptAction, TEXT("SecondaryPromptAction"));
}

void AObjectiveStep::AuditTeleportSquad(const FObjectiveSideEffect& Effect)
{
	if (!IsValid(Effect.PlayerDestination))
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("'%s' (step '%s'): TeleportSquad has no PlayerDestination — the whole effect is skipped, "
				 "companions included"),
			*GetName(), *GetEffectiveStepId().ToString());

	TArray<ACompanionCharacter*> Companions;
	GatherSquadCompanions(Companions);
	if (Effect.CompanionDestinations.Num() >= Companions.Num()) return;

	// Verbose, not a warning: a short list is how a designer deliberately leaves one squadmate behind.
	// The count also changes across the mission because the gather filters on controller + alive +
	// not DBNO, so a companion downed between beats drops out of the list.
	UE_LOG(LogObjectiveStep, Verbose,
		TEXT("'%s' (step '%s'): TeleportSquad has %d destination(s) for %d companion(s) — the rest stay put"),
		*GetName(), *GetEffectiveStepId().ToString(), Effect.CompanionDestinations.Num(), Companions.Num());
}

void AObjectiveStep::AuditLevelWiring()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// One step owns the level-wide checks. The FIRST in iteration order, not the entry step — "this
	// level has no entry step at all" is precisely the case with no entry step to report it.
	TActorIterator<AObjectiveStep> First(World);
	if (!First || *First != this) return;

	TArray<AObjectiveStep*> Entries;
	TMap<FName, int32> IdCounts;
	for (TActorIterator<AObjectiveStep> It(World); It; ++It)
	{
		++IdCounts.FindOrAdd(It->GetEffectiveStepId());
		if (It->bIsEntryStep) Entries.Add(*It);
	}

	if (Entries.IsEmpty())
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("This level places objective steps but none is marked bIsEntryStep — no chain will start."));

	// Several entry steps are legitimate (parallel chains). Two entries feeding the SAME chain
	// are not: the second activation races the first and the beat runs twice.
	TMap<const AObjectiveStep*, int32> ReachCount;
	for (AObjectiveStep* Entry : Entries)
	{
		TArray<AObjectiveStep*> Order;
		FObjectiveChainWalker::Walk(Entry, &NextOf, Order);
		for (const AObjectiveStep* Step : Order) ++ReachCount.FindOrAdd(Step);
		AuditWaveOrphans(Order);
	}
	for (const TPair<const AObjectiveStep*, int32>& Reach : ReachCount)
	{
		if (Reach.Value < 2) continue;
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("Step '%s' is reachable from %d entry steps — a chain must have exactly one entry."),
			*Reach.Key->GetEffectiveStepId().ToString(), Reach.Value);
	}

	for (const TPair<FName, int32>& Id : IdCounts)
	{
		if (Id.Value < 2) continue;
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("StepId '%s' is used by %d steps — marker ids collide and the checkpoint record is ambiguous."),
			*Id.Key.ToString(), Id.Value);
	}
}

void AObjectiveStep::WarnOnUnappliablePredecessors()
{
	AObjectiveStep* Entry = FindEntryStep();
	if (!IsValid(Entry)) return;

	TArray<AObjectiveStep*> Earlier;
	if (!FObjectiveChainWalker::SplitAtId(Entry, &NextOf, &IdOf, GetEffectiveStepId(), Earlier)) return;

	for (const AObjectiveStep* Step : Earlier)
	{
		if (Step->DerivesWorldState() || Step->HasReplayableSideEffects()) continue;
		UE_LOG(LogObjectiveStep, Warning,
			TEXT("Checkpoint '%s': earlier step '%s' derives no world change from its condition and has no "
				 "replayable side effects — a resume past it leaves that beat unapplied."),
			*GetEffectiveStepId().ToString(), *Step->GetEffectiveStepId().ToString());
	}
}

int32 AObjectiveStep::AuditWaveOrphans(const TArray<AObjectiveStep*>& Order)
{
	int32 Findings = 0;

	for (int32 WatcherIndex = 0; WatcherIndex < Order.Num(); ++WatcherIndex)
	{
		const AObjectiveStep* Watcher = Order[WatcherIndex];
		if (!IsValid(Watcher) || Watcher->Condition != EObjectiveCondition::WaveCompleted) continue;
		if (Watcher->WatchedWaveId.IsNone()) continue;

		// The LAST beat at or before the watcher that could have started this wave. A watcher that
		// starts its own wave lands on itself here and is the shape the migration wants — the loop
		// below then has nothing to walk.
		int32 LastStarter = INDEX_NONE;
		for (int32 Index = 0; Index <= WatcherIndex; ++Index)
			if (IsValid(Order[Index]) && Order[Index]->StartsDirectorWave(Watcher->WatchedWaveId)) LastStarter = Index;

		// No starter anywhere on the chain means something outside it owns the wave (the extraction
		// target actor starts its own). Not this audit's business.
		if (LastStarter == INDEX_NONE) continue;

		for (int32 Index = LastStarter + 1; Index <= WatcherIndex; ++Index)
		{
			if (!IsValid(Order[Index]) || !Order[Index]->bIsCheckpoint) continue;
			++Findings;
			UE_LOG(LogObjectiveStep, Warning,
				TEXT("Checkpoint '%s' sits between the beat that starts wave '%s' and the beat that watches "
					 "for it ('%s'). A fast-forward never replays StartDirectorWave, so a resume there "
					 "activates the watching beat with no wave running and no way to start one — move the "
					 "StartDirectorWave onto the watching beat's OnActivate."),
				*Order[Index]->GetEffectiveStepId().ToString(), *Watcher->WatchedWaveId.ToString(),
				*Watcher->GetEffectiveStepId().ToString());
			break;
		}
	}
	return Findings;
}

bool AObjectiveStep::DerivesWorldState() const
{
	switch (Condition)
	{
	case EObjectiveCondition::DoorOpened:
	case EObjectiveCondition::EnemiesDead:
	case EObjectiveCondition::ContainerLooted:
	case EObjectiveCondition::AcquireKeycard:
	case EObjectiveCondition::ExtracteeRescued:
		return true;
	default:
		return false;
	}
}

AObjectiveStep* AObjectiveStep::FindEntryStep() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	// The entry of THIS chain, not the first entry step in iteration order — parallel chains are
	// legitimate, and the wrong entry names the wrong predecessors.
	for (TActorIterator<AObjectiveStep> It(World); It; ++It)
	{
		if (!It->bIsEntryStep) continue;

		TArray<AObjectiveStep*> Order;
		FObjectiveChainWalker::Walk(*It, &NextOf, Order);
		if (Order.Contains(this)) return *It;
	}
	return nullptr;
}

#if WITH_EDITOR
void AObjectiveStep::ValidateConfig() const
{
	if (Condition == EObjectiveCondition::ReachLocation && CompletionRadius <= 0.f)
		UE_LOG(LogObjectiveStep, Warning, TEXT("%s: ReachLocation with CompletionRadius %.1f (<= 0)"),
			*GetName(), CompletionRadius);

	if (Condition == EObjectiveCondition::SurviveDuration && DefendSeconds <= 0.f)
		UE_LOG(LogObjectiveStep, Warning, TEXT("%s: SurviveDuration with DefendSeconds %.1f (<= 0)"),
			*GetName(), DefendSeconds);

	if (Condition == EObjectiveCondition::ContainerLooted && TrackedContainers.Num() < 2 && !bRequiresAllContainers)
		UE_LOG(LogObjectiveStep, Warning, TEXT("%s: bRequiresAllContainers is off with %d container(s) — the flag does nothing"),
			*GetName(), TrackedContainers.Num());

	if (bIsCheckpoint && !IsValid(CheckpointSpawn))
		UE_LOG(LogObjectiveStep, Warning, TEXT("%s: checkpoint step has no CheckpointSpawn"), *GetName());

	if (NextStep.Get() == this)
		UE_LOG(LogObjectiveStep, Warning, TEXT("%s: NextStep points at itself"), *GetName());

	for (const FObjectiveSideEffect& Effect : SideEffects)
	{
		if (Effect.Type == EObjectiveSideEffectType::UnlockGate && !IsValid(Effect.GateTarget))
			UE_LOG(LogObjectiveStep, Warning, TEXT("%s: UnlockGate side effect has no GateTarget"), *GetName());
		if (Effect.Type == EObjectiveSideEffectType::ActivateActor && !IsValid(Effect.ActivateTarget))
			UE_LOG(LogObjectiveStep, Warning, TEXT("%s: ActivateActor side effect has no ActivateTarget"), *GetName());
		if (Effect.Type == EObjectiveSideEffectType::StartDirectorWave && Effect.WaveRequest.WaveId.IsNone())
			UE_LOG(LogObjectiveStep, Warning, TEXT("%s: StartDirectorWave side effect has no WaveId"), *GetName());
		if (Effect.Type == EObjectiveSideEffectType::SetExtracteeRescuable && !IsValid(Effect.ExtracteeTarget))
			UE_LOG(LogObjectiveStep, Warning, TEXT("%s: SetExtracteeRescuable side effect has no ExtracteeTarget"), *GetName());
		if (Effect.Type == EObjectiveSideEffectType::CommandCompanionRoute && !IsValid(Effect.RouteTarget))
			UE_LOG(LogObjectiveStep, Warning, TEXT("%s: CommandCompanionRoute side effect has no RouteTarget"), *GetName());
		if (Effect.Type == EObjectiveSideEffectType::TeleportSquad && !IsValid(Effect.PlayerDestination))
			UE_LOG(LogObjectiveStep, Warning, TEXT("%s: TeleportSquad side effect has no PlayerDestination"), *GetName());
		if (Effect.Type == EObjectiveSideEffectType::SetDoorsLocked && Effect.DoorTargets.IsEmpty())
			UE_LOG(LogObjectiveStep, Warning, TEXT("%s: SetDoorsLocked side effect has no DoorTargets"), *GetName());
	}
}
#endif

// ------------------------------------------------------------------
// Delegate handlers
// ------------------------------------------------------------------

void AObjectiveStep::OnCompletionSphereOverlap(UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!bActive || Condition != EObjectiveCondition::ReachLocation) return;
	if (!Cast<AExtractionPlayer>(OtherActor)) return;
	CompleteStep();
}

void AObjectiveStep::HandleKeycardRecorded(FName KeycardId)
{
	if (!bActive || KeycardId != RequiredKeycardId) return;
	CompleteStep();
}

void AObjectiveStep::HandleTrackedEnemyDeath()
{
	EvaluateCondition();
}

void AObjectiveStep::HandleTrackedEnemyDestroyed(AActor* /*DestroyedActor*/)
{
	EvaluateCondition();
}

void AObjectiveStep::HandleDoorOpened(AActor* Door)
{
	if (!bActive || Door != TrackedDoor) return;
	CompleteStep();
}

void AObjectiveStep::HandleLootCompleted(ALootContainer* /*Container*/, AActor* /*Looter*/)
{
	EvaluateCondition();
}

void AObjectiveStep::HandleContainerDestroyed(AActor* /*DestroyedActor*/)
{
	EvaluateCondition();
}

void AObjectiveStep::HandleRouteCompleted(bool bAborted)
{
	if (!bActive || bAborted) return;
	CompleteStep();
}

void AObjectiveStep::HandleWorldInteract(AActor* Target, AActor* Interactor)
{
	// Identity only — an interactable that destroys itself on use is still the one we were told
	// to watch, and dereferencing it here would be a use-after-destroy.
	if (!bActive || Target != TrackedInteractable) return;

	// The condition is "the PLAYER interacts". The companion loots through the very same containers
	// and the extraction target is driven externally with a null interactor, so an ungated beat ticks
	// itself off work the player never did.
	if (!bAnyInteractorCounts && !Cast<AExtractionPlayer>(Interactor)) return;
	CompleteStep();
}

void AObjectiveStep::HandleExtracteeRescued()
{
	if (!bActive) return;
	CompleteStep();
}

void AObjectiveStep::HandleDirectorWaveCompleted(FName WaveId)
{
	if (!bActive || WaveId != WatchedWaveId) return;
	CompleteStep();
}

void AObjectiveStep::HandleDirectorWaveBlocked(FName WaveId, FText Reason)
{
	if (!bActive || WaveId != WatchedWaveId) return;

	// Same player-facing surface the legacy flow used. The beat cannot complete until the wave does,
	// so a blocked wave is a stall the player has to be told about rather than left standing in.
	UWorld* World = GetWorld();
	if (UMissionInventorySubsystem* Inventory = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr)
		Inventory->OnLootNotify.Broadcast(Reason);

	UE_LOG(LogObjectiveStep, Warning, TEXT("'%s' (step '%s'): watched wave '%s' blocked: %s"),
		*GetName(), *GetEffectiveStepId().ToString(), *WaveId.ToString(), *Reason.ToString());
}

#if WITH_DEV_AUTOMATION_TESTS
void AObjectiveStep::TestSetTrackedContainers(const TArray<ALootContainer*>& InContainers)
{
	TrackedContainers.Reset(InContainers.Num());
	for (ALootContainer* Container : InContainers)
		TrackedContainers.Add(Container);
}

// Defined here rather than inline so ObjectiveStep.h keeps a forward declaration of UInputAction and
// does not drag EnhancedInput into every translation unit that includes it.
void AObjectiveStep::TestSetPromptAction(const UInputAction* InAction)
{
	PromptAction = InAction;
}

void AObjectiveStep::TestSetSecondaryPromptAction(const UInputAction* InAction)
{
	SecondaryPromptAction = InAction;
}
#endif
