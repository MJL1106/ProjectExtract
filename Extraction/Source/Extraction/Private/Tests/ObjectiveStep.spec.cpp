#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/ObjectiveChainWalker.h"
#include "World/ObjectiveStep.h"
#include "World/InteractionEventSubsystem.h"
#include "World/LootContainer.h"
#include "World/LootTypes.h"
#include "Character/ExtractionPlayer.h"
#include "Companion/CompanionRoute.h"
#include "Engine/World.h"
#include "Enemy/EnemyDirectorSubsystem.h"
#include "Game/ObjectiveSubsystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace ObjectiveStepTest
{
	/** Stand-in chain node: the walker is a pure template, so the traversal rules can be exercised
	 *  without a world full of actors. */
	struct FFakeStep
	{
		FName Id;
		FFakeStep* Next = nullptr;
	};

	FFakeStep* NextOf(FFakeStep* Step) { return Step ? Step->Next : nullptr; }
	FName IdOf(const FFakeStep* Step) { return Step ? Step->Id : NAME_None; }

	void GiveOneKeycard(ALootContainer* Container, FName KeycardId)
	{
		FArrayProperty* ContentsProperty = FindFProperty<FArrayProperty>(Container->GetClass(), TEXT("Contents"));
		check(ContentsProperty);
		FScriptArrayHelper Helper(ContentsProperty, ContentsProperty->ContainerPtrToValuePtr<void>(Container));
		Helper.Resize(1);
		FLootGrant* Grant = reinterpret_cast<FLootGrant*>(Helper.GetRawPtr(0));
		Grant->Type = ELootType::Keycard;
		Grant->KeycardId = KeycardId;
	}

	ALootContainer* SpawnLootableContainer(UWorld* World, const FVector& Location, FName KeycardId)
	{
		ALootContainer* Container = World->SpawnActor<ALootContainer>(Location, FRotator::ZeroRotator);
		Container->SetRole(ROLE_Authority);
		GiveOneKeycard(Container, KeycardId);
		return Container;
	}

	AObjectiveStep* SpawnStep(UWorld* World, FName StepId, EObjectiveCondition Condition)
	{
		AObjectiveStep* Step = World->SpawnActor<AObjectiveStep>();
		Step->TestSetStepId(StepId);
		Step->TestSetCondition(Condition);
		return Step;
	}

	AObjectiveStep* SpawnWaveWatcher(UWorld* World, FName StepId, FName WaveId, bool bCheckpoint)
	{
		AObjectiveStep* Step = SpawnStep(World, StepId, EObjectiveCondition::WaveCompleted);
		Step->TestSetWatchedWaveId(WaveId);
		Step->TestSetCheckpoint(bCheckpoint);
		return Step;
	}

	FObjectiveSideEffect MakeWaveEffect(EObjectiveSideEffectWhen When, FName WaveId)
	{
		FObjectiveSideEffect Effect;
		Effect.When = When;
		Effect.Type = EObjectiveSideEffectType::StartDirectorWave;
		Effect.WaveRequest.WaveId = WaveId;
		Effect.WaveRequest.TargetSquads = 1;
		return Effect;
	}

	FObjectiveSideEffect MakeActivateEffect(EObjectiveSideEffectWhen When, AActor* Target, bool bReplayOnResume)
	{
		FObjectiveSideEffect Effect;
		Effect.When = When;
		Effect.Type = EObjectiveSideEffectType::ActivateActor;
		Effect.ActivateTarget = Target;
		Effect.bReplayOnResume = bReplayOnResume;
		return Effect;
	}

	FObjectiveSideEffect MakeRouteEffect(EObjectiveSideEffectWhen When, ACompanionRoute* Route)
	{
		FObjectiveSideEffect Effect;
		Effect.When = When;
		Effect.Type = EObjectiveSideEffectType::CommandCompanionRoute;
		Effect.RouteTarget = Route;
		return Effect;
	}

	/** Drains a SetTimerForNextTick. Two ticks with a real delta, not one: a timer set outside the
	 *  manager's own tick lands on the pending list, which is drained only AFTER the fire pass. */
	void PumpNextTickTimers(UWorld* World)
	{
		if (!World) return;
		for (int32 Tick = 0; Tick < 2; ++Tick) World->GetTimerManager().Tick(1.f / 60.f);
	}

	const FObjectiveMarker* FindMarker(const UWorld* World, FName StepId)
	{
		const UObjectiveSubsystem* Objectives = World ? World->GetSubsystem<UObjectiveSubsystem>() : nullptr;
		if (!Objectives) return nullptr;
		return Objectives->GetObjectives().FindByPredicate(
			[StepId](const FObjectiveMarker& Marker) { return Marker.Id == StepId; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveChainLinearWalkTest,
	"Extraction.Objectives.Step.ChainLinearAdvance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveChainLinearWalkTest::RunTest(const FString& Parameters)
{
	using namespace ObjectiveStepTest;

	FFakeStep D{ TEXT("D"), nullptr };
	FFakeStep C{ TEXT("C"), &D };
	FFakeStep B{ TEXT("B"), &C };
	FFakeStep A{ TEXT("A"), &B };

	TArray<FFakeStep*> Order;
	TestTrue(TEXT("a linear chain walks cleanly"), FObjectiveChainWalker::Walk(&A, &NextOf, Order));
	TestEqual(TEXT("every beat is visited once"), Order.Num(), 4);
	TestEqual(TEXT("order is authored order"), IdOf(Order[0]), FName(TEXT("A")));
	TestEqual(TEXT("order is authored order"), IdOf(Order[3]), FName(TEXT("D")));
	TestFalse(TEXT("a linear chain has no cycle"), FObjectiveChainWalker::HasCycle(&A, &NextOf));

	// A null NextStep ends the chain rather than looping back to the entry.
	TArray<FFakeStep*> TailOrder;
	TestTrue(TEXT("the last beat walks to itself only"), FObjectiveChainWalker::Walk(&D, &NextOf, TailOrder));
	TestEqual(TEXT("the last beat ends the chain"), TailOrder.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveChainCycleTest,
	"Extraction.Objectives.Step.ChainCycleDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveChainCycleTest::RunTest(const FString& Parameters)
{
	using namespace ObjectiveStepTest;

	FFakeStep C{ TEXT("C"), nullptr };
	FFakeStep B{ TEXT("B"), &C };
	FFakeStep A{ TEXT("A"), &B };
	C.Next = &B; // closes the loop mid-chain, not back at the entry

	TArray<FFakeStep*> Order;
	TestFalse(TEXT("a loop is reported"), FObjectiveChainWalker::Walk(&A, &NextOf, Order));
	TestTrue(TEXT("HasCycle agrees"), FObjectiveChainWalker::HasCycle(&A, &NextOf));
	TestEqual(TEXT("the acyclic prefix is still usable"), Order.Num(), 3);

	// Self-reference is the degenerate case and must not spin.
	FFakeStep Selfish{ TEXT("Selfish"), nullptr };
	Selfish.Next = &Selfish;
	TestTrue(TEXT("a self-referencing step is a cycle"), FObjectiveChainWalker::HasCycle(&Selfish, &NextOf));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveChainFastForwardTest,
	"Extraction.Objectives.Step.ChainFastForwardSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveChainFastForwardTest::RunTest(const FString& Parameters)
{
	using namespace ObjectiveStepTest;

	FFakeStep D{ TEXT("D"), nullptr };
	FFakeStep C{ TEXT("C"), &D };
	FFakeStep B{ TEXT("B"), &C };
	FFakeStep A{ TEXT("A"), &B };

	TArray<FFakeStep*> Earlier;
	FFakeStep* Resume = FObjectiveChainWalker::SplitAtId(&A, &NextOf, &IdOf, FName(TEXT("C")), Earlier);
	TestTrue(TEXT("the recorded checkpoint resolves"), Resume == &C);
	TestEqual(TEXT("only the beats before it are replayed"), Earlier.Num(), 2);
	TestEqual(TEXT("replay starts at the entry"), IdOf(Earlier[0]), FName(TEXT("A")));
	TestEqual(TEXT("replay stops before the resume point"), IdOf(Earlier[1]), FName(TEXT("B")));

	// Resuming at the entry itself replays nothing.
	FFakeStep* EntryResume = FObjectiveChainWalker::SplitAtId(&A, &NextOf, &IdOf, FName(TEXT("A")), Earlier);
	TestTrue(TEXT("the entry resolves as its own resume point"), EntryResume == &A);
	TestEqual(TEXT("nothing to replay ahead of the entry"), Earlier.Num(), 0);

	// A stale checkpoint from another level must not fast-forward anything.
	TestNull(TEXT("an off-chain id resolves to nothing"),
		FObjectiveChainWalker::SplitAtId(&A, &NextOf, &IdOf, FName(TEXT("Nowhere")), Earlier));
	TestEqual(TEXT("an off-chain id replays nothing"), Earlier.Num(), 0);
	TestNull(TEXT("a None id resolves to nothing"),
		FObjectiveChainWalker::SplitAtId(&A, &NextOf, &IdOf, NAME_None, Earlier));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveContainerRuleTest,
	"Extraction.Objectives.Step.ContainerAllVsAnyRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveContainerRuleTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("all-rule: partial does not satisfy"),
		FObjectiveConditionRules::AreContainersSatisfied(3, 2, /*bRequiresAll*/ true));
	TestTrue(TEXT("all-rule: every container satisfies"),
		FObjectiveConditionRules::AreContainersSatisfied(3, 3, /*bRequiresAll*/ true));
	TestTrue(TEXT("any-rule: one container satisfies"),
		FObjectiveConditionRules::AreContainersSatisfied(3, 1, /*bRequiresAll*/ false));
	TestFalse(TEXT("any-rule: none satisfies nothing"),
		FObjectiveConditionRules::AreContainersSatisfied(3, 0, /*bRequiresAll*/ false));

	// An unwired step must stall loudly, never tick itself off the instant it activates.
	TestFalse(TEXT("an empty container set never satisfies (all)"),
		FObjectiveConditionRules::AreContainersSatisfied(0, 0, /*bRequiresAll*/ true));
	TestFalse(TEXT("an empty container set never satisfies (any)"),
		FObjectiveConditionRules::AreContainersSatisfied(0, 0, /*bRequiresAll*/ false));

	TestFalse(TEXT("a surviving enemy blocks the beat"), FObjectiveConditionRules::AreEnemiesSatisfied(4, 3));
	TestTrue(TEXT("every enemy down completes the beat"), FObjectiveConditionRules::AreEnemiesSatisfied(4, 4));
	TestFalse(TEXT("an empty enemy set never satisfies"), FObjectiveConditionRules::AreEnemiesSatisfied(0, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveSideEffectReplayRuleTest,
	"Extraction.Objectives.Step.SideEffectReplayRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveSideEffectReplayRuleTest::RunTest(const FString& Parameters)
{
	// Re-applying an idempotent effect just re-asserts the world the player left behind.
	const EObjectiveSideEffectType Idempotent[] = {
		EObjectiveSideEffectType::SetMissionPhase,
		EObjectiveSideEffectType::UnlockGate,
		EObjectiveSideEffectType::SetCompanionMode,
		EObjectiveSideEffectType::SetExtracteeRescuable,
		EObjectiveSideEffectType::ActivateActor,
		EObjectiveSideEffectType::TripAlarm,
		EObjectiveSideEffectType::SetDirectorSpawning,
		EObjectiveSideEffectType::SetDoorsLocked,
	};
	for (EObjectiveSideEffectType Type : Idempotent)
	{
		FObjectiveSideEffect Effect;
		Effect.Type = Type;
		TestTrue(TEXT("an idempotent effect replays on resume by default"), Effect.ShouldReplayOnResume());

		Effect.bReplayOnResume = false;
		TestFalse(TEXT("a designer can opt an idempotent effect out of the replay"), Effect.ShouldReplayOnResume());
	}

	// Re-starting a beaten wave at level load, or ending the level from an earlier beat, is never
	// a replay — the flag cannot opt either back in.
	FObjectiveSideEffect Wave;
	Wave.Type = EObjectiveSideEffectType::StartDirectorWave;
	TestFalse(TEXT("a wave never replays on resume"), Wave.ShouldReplayOnResume());
	Wave.bReplayOnResume = true;
	TestFalse(TEXT("a wave stays un-replayable even when the flag is set"), Wave.ShouldReplayOnResume());

	FObjectiveSideEffect Complete;
	Complete.Type = EObjectiveSideEffectType::CompleteLevel;
	Complete.bReplayOnResume = true;
	TestFalse(TEXT("CompleteLevel never replays on resume"), Complete.ShouldReplayOnResume());

	// A replayed route command would march the companion off from wherever the checkpoint teleport
	// just dropped him, re-walking choreography the player already watched.
	FObjectiveSideEffect Route;
	Route.Type = EObjectiveSideEffectType::CommandCompanionRoute;
	TestFalse(TEXT("a route command never replays on resume"), Route.ShouldReplayOnResume());
	Route.bReplayOnResume = true;
	TestFalse(TEXT("a route command stays un-replayable even when the flag is set"),
		Route.ShouldReplayOnResume());

	// The checkpoint teleport owns where the player starts; a replayed squad teleport would yank the
	// party clean across the level the instant the save loads.
	FObjectiveSideEffect Teleport;
	Teleport.Type = EObjectiveSideEffectType::TeleportSquad;
	TestFalse(TEXT("a squad teleport never replays on resume"), Teleport.ShouldReplayOnResume());
	Teleport.bReplayOnResume = true;
	TestFalse(TEXT("a squad teleport stays un-replayable even when the flag is set"),
		Teleport.ShouldReplayOnResume());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveStepActorSeamsTest,
	"Extraction.Objectives.Step.ActorWorldSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveStepActorSeamsTest::RunTest(const FString& Parameters)
{
	using namespace ObjectiveStepTest;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("automation world exists"), World);
	if (!World) return false;

	// --- Linear advance through a manual chain ---

	AObjectiveStep* First = SpawnStep(World, TEXT("First"), EObjectiveCondition::Manual);
	AObjectiveStep* Second = SpawnStep(World, TEXT("Second"), EObjectiveCondition::Manual);
	AObjectiveStep* Third = SpawnStep(World, TEXT("Third"), EObjectiveCondition::Manual);
	First->TestSetNextStep(Second);
	Second->TestSetNextStep(Third);

	First->Activate();
	TestTrue(TEXT("the entry beat goes live"), First->IsStepActive());
	TestFalse(TEXT("activation does not complete the beat"), First->IsStepCompleted());
	TestFalse(TEXT("later beats stay dormant"), Second->IsStepActive());

	First->Activate();
	TestTrue(TEXT("re-activation is a no-op"), First->IsStepActive());
	TestFalse(TEXT("re-activation does not complete"), First->IsStepCompleted());

	First->CompleteStep();
	TestTrue(TEXT("the finished beat is completed"), First->IsStepCompleted());
	TestFalse(TEXT("the finished beat is no longer active"), First->IsStepActive());
	TestTrue(TEXT("completion activates NextStep"), Second->IsStepActive());
	TestFalse(TEXT("completion advances exactly one beat"), Third->IsStepActive());

	First->CompleteStep();
	TestFalse(TEXT("a repeat completion cannot skip a beat"), Third->IsStepActive());

	First->Activate();
	TestFalse(TEXT("a completed beat cannot be re-activated"), First->IsStepActive());

	Second->CompleteStep();
	Third->CompleteStep();
	TestTrue(TEXT("the tail beat completes"), Third->IsStepCompleted());
	TestNull(TEXT("a null NextStep ends the chain"), Third->GetNextStep());

	// --- Container completion: any one vs all ---

	ALootContainer* AnyLeft = SpawnLootableContainer(World, FVector(100.f, 0.f, 0.f), TEXT("AnyLeft"));
	ALootContainer* AnyRight = SpawnLootableContainer(World, FVector(400.f, 0.f, 0.f), TEXT("AnyRight"));
	AObjectiveStep* AnyStep = SpawnStep(World, TEXT("AnyStep"), EObjectiveCondition::ContainerLooted);
	AnyStep->TestSetRequiresAllContainers(false);
	AnyStep->TestSetTrackedContainers(TArray<ALootContainer*>{ AnyLeft, AnyRight });

	AnyStep->Activate();
	TestFalse(TEXT("any-rule beat waits for a first loot"), AnyStep->IsStepCompleted());
	AnyLeft->Loot_Implementation(nullptr);
	TestTrue(TEXT("any-rule beat completes on the first container"), AnyStep->IsStepCompleted());
	TestFalse(TEXT("any-rule beat leaves the rest lootable"), AnyRight->IsLooted());

	ALootContainer* AllLeft = SpawnLootableContainer(World, FVector(0.f, 100.f, 0.f), TEXT("AllLeft"));
	ALootContainer* AllRight = SpawnLootableContainer(World, FVector(0.f, 400.f, 0.f), TEXT("AllRight"));
	AObjectiveStep* AllStep = SpawnStep(World, TEXT("AllStep"), EObjectiveCondition::ContainerLooted);
	AllStep->TestSetRequiresAllContainers(true);
	AllStep->TestSetTrackedContainers(TArray<ALootContainer*>{ AllLeft, AllRight });

	AllStep->Activate();
	AllLeft->Loot_Implementation(nullptr);
	TestFalse(TEXT("all-rule beat is not satisfied by one container"), AllStep->IsStepCompleted());
	AllRight->Loot_Implementation(nullptr);
	TestTrue(TEXT("all-rule beat completes once every container is emptied"), AllStep->IsStepCompleted());

	// --- An unwired container slot is not a looted one ---
	// A seven-crate beat with six empty slots must stall on the mis-wire, not tick itself off on
	// the first real loot.

	ALootContainer* GappedCrate = SpawnLootableContainer(World, FVector(0.f, 0.f, 400.f), TEXT("Gapped"));
	AObjectiveStep* GappedAllStep = SpawnStep(World, TEXT("GappedAll"), EObjectiveCondition::ContainerLooted);
	GappedAllStep->TestSetRequiresAllContainers(true);
	GappedAllStep->TestSetTrackedContainers(TArray<ALootContainer*>{ GappedCrate, nullptr });

	GappedAllStep->Activate();
	TestFalse(TEXT("an empty slot does not pre-satisfy an all-containers beat"), GappedAllStep->IsStepCompleted());
	GappedCrate->Loot_Implementation(nullptr);
	TestFalse(TEXT("an all-containers beat cannot complete over an empty slot"), GappedAllStep->IsStepCompleted());

	// The any-rule reads the same slot the same way — one REAL loot is what completes it.
	ALootContainer* GappedAnyCrate = SpawnLootableContainer(World, FVector(0.f, 0.f, 600.f), TEXT("GappedAny"));
	AObjectiveStep* GappedAnyStep = SpawnStep(World, TEXT("GappedAny"), EObjectiveCondition::ContainerLooted);
	GappedAnyStep->TestSetRequiresAllContainers(false);
	GappedAnyStep->TestSetTrackedContainers(TArray<ALootContainer*>{ nullptr, GappedAnyCrate });

	GappedAnyStep->Activate();
	TestFalse(TEXT("an empty slot alone does not complete an any-container beat"), GappedAnyStep->IsStepCompleted());
	GappedAnyCrate->Loot_Implementation(nullptr);
	TestTrue(TEXT("one real loot completes an any-container beat"), GappedAnyStep->IsStepCompleted());

	// --- A crate blown up mid-mission is gone, not un-searched ---
	// The other half of the rule above: an authored-null slot stalls the beat, but a container that
	// existed at activation and was destroyed during the fight took its loot with it. Refusing to
	// count it soft-locks an all-containers beat on a crate that can never be searched again.

	ALootContainer* SurvivingCrate = SpawnLootableContainer(World, FVector(0.f, 0.f, 800.f), TEXT("Surviving"));
	ALootContainer* DoomedCrate = SpawnLootableContainer(World, FVector(0.f, 0.f, 900.f), TEXT("Doomed"));
	AObjectiveStep* DestroyedCrateStep = SpawnStep(World, TEXT("DestroyedCrate"), EObjectiveCondition::ContainerLooted);
	DestroyedCrateStep->TestSetRequiresAllContainers(true);
	DestroyedCrateStep->TestSetTrackedContainers(TArray<ALootContainer*>{ SurvivingCrate, DoomedCrate });

	DestroyedCrateStep->Activate();
	SurvivingCrate->Loot_Implementation(nullptr);
	TestFalse(TEXT("one crate searched, one still standing"), DestroyedCrateStep->IsStepCompleted());
	DoomedCrate->Destroy();
	TestTrue(TEXT("a crate destroyed after activation counts as searched"), DestroyedCrateStep->IsStepCompleted());

	// --- Interacted answers to the PLAYER ---
	// The companion loots through the same containers and the extraction target is driven externally
	// with a null interactor, so an ungated beat ticks itself off work the player never did.

	ALootContainer* GateCrate = SpawnLootableContainer(World, FVector(1200.f, 0.f, 0.f), TEXT("GateCrate"));
	UInteractionEventSubsystem* Events = World->GetSubsystem<UInteractionEventSubsystem>();
	TestNotNull(TEXT("the interaction event subsystem exists"), Events);
	if (!Events) return false;

	AObjectiveStep* PlayerOnlyStep = SpawnStep(World, TEXT("PlayerOnly"), EObjectiveCondition::Interacted);
	PlayerOnlyStep->TestSetTrackedInteractable(GateCrate);
	PlayerOnlyStep->Activate();

	Events->NotifyWorldInteract(GateCrate, nullptr);
	TestFalse(TEXT("a scripted interaction with no interactor does not complete a player beat"),
		PlayerOnlyStep->IsStepCompleted());

	AObjectiveStep* NonPlayerStandIn = SpawnStep(World, TEXT("NonPlayerStandIn"), EObjectiveCondition::Manual);
	Events->NotifyWorldInteract(GateCrate, NonPlayerStandIn);
	TestFalse(TEXT("somebody other than the player does not complete a player beat"),
		PlayerOnlyStep->IsStepCompleted());

	if (AExtractionPlayer* Player = World->SpawnActor<AExtractionPlayer>())
	{
		Events->NotifyWorldInteract(GateCrate, Player);
		TestTrue(TEXT("the player's own interaction completes the beat"), PlayerOnlyStep->IsStepCompleted());
	}

	// A companion-loot beat is still authorable — that is what the opt-in is for.
	AObjectiveStep* AnyInteractorStep = SpawnStep(World, TEXT("AnyInteractor"), EObjectiveCondition::Interacted);
	AnyInteractorStep->TestSetTrackedInteractable(GateCrate);
	AnyInteractorStep->TestSetAnyInteractorCounts(true);
	AnyInteractorStep->Activate();
	Events->NotifyWorldInteract(GateCrate, nullptr);
	TestTrue(TEXT("bAnyInteractorCounts keeps a companion-loot beat authorable"),
		AnyInteractorStep->IsStepCompleted());

	// --- Checkpoint fast-forward derives the world state from the condition ---

	ALootContainer* SkippedCrate = SpawnLootableContainer(World, FVector(0.f, 0.f, 200.f), TEXT("Skipped"));
	AObjectiveStep* SkippedStep = SpawnStep(World, TEXT("Skipped"), EObjectiveCondition::ContainerLooted);
	AObjectiveStep* ResumeStep = SpawnStep(World, TEXT("Resume"), EObjectiveCondition::Manual);
	SkippedStep->TestSetTrackedContainers(TArray<ALootContainer*>{ SkippedCrate });
	SkippedStep->TestSetNextStep(ResumeStep);

	const int32 CrateBroadcastsBefore = SkippedCrate->TestGetCompletionBroadcastCount();
	SkippedStep->TestApplyCompletedWorldState();
	TestTrue(TEXT("fast-forward marks the skipped beat complete"), SkippedStep->IsStepCompleted());
	TestTrue(TEXT("fast-forward loots the skipped beat's container"), SkippedCrate->IsLooted());
	TestFalse(TEXT("fast-forward does not advance the chain itself"), ResumeStep->IsStepActive());

	// The legacy ALevelObjectiveFlow still binds OnLootCompleted on these containers — a resume
	// must not drive its state machine, so the silent path broadcasts nothing.
	TestEqual(TEXT("fast-forward loot raises no completion broadcast"),
		SkippedCrate->TestGetCompletionBroadcastCount(), CrateBroadcastsBefore);

	ResumeStep->Activate();
	TestTrue(TEXT("the recorded checkpoint resumes live"), ResumeStep->IsStepActive());

	// --- Fast-forward replays only what a resume may safely repeat ---

	AObjectiveStep* ReplayOnTarget = SpawnStep(World, TEXT("ReplayOnTarget"), EObjectiveCondition::Manual);
	AObjectiveStep* ReplayOnStep = SpawnStep(World, TEXT("ReplayOn"), EObjectiveCondition::Manual);
	ReplayOnStep->TestAddSideEffect(
		MakeActivateEffect(EObjectiveSideEffectWhen::OnActivate, ReplayOnTarget, /*bReplayOnResume=*/true));

	ReplayOnStep->TestApplyCompletedWorldState();
	TestTrue(TEXT("a resume replays an earlier beat's OnActivate effects (the optional-supplies chain)"),
		ReplayOnTarget->IsStepActive());

	AObjectiveStep* ReplayOffTarget = SpawnStep(World, TEXT("ReplayOffTarget"), EObjectiveCondition::Manual);
	AObjectiveStep* ReplayOffStep = SpawnStep(World, TEXT("ReplayOff"), EObjectiveCondition::Manual);
	ReplayOffStep->TestAddSideEffect(
		MakeActivateEffect(EObjectiveSideEffectWhen::OnComplete, ReplayOffTarget, /*bReplayOnResume=*/false));

	ReplayOffStep->TestApplyCompletedWorldState();
	TestFalse(TEXT("a resume honours bReplayOnResume = false"), ReplayOffTarget->IsStepActive());

	if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
	{
		AObjectiveStep* SkippedWaveStep = SpawnStep(World, TEXT("SkippedWave"), EObjectiveCondition::Manual);
		SkippedWaveStep->TestAddSideEffect(
			MakeWaveEffect(EObjectiveSideEffectWhen::OnComplete, TEXT("AlreadyBeatenWave")));

		SkippedWaveStep->TestApplyCompletedWorldState();
		TestEqual(TEXT("a resume never re-starts a wave the player already beat"),
			Director->GetActiveWaveId(), FName(NAME_None));

		// Positive control: the same effect on a LIVE completion still starts its wave.
		AObjectiveStep* LiveWaveStep = SpawnStep(World, TEXT("LiveWave"), EObjectiveCondition::Manual);
		LiveWaveStep->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnComplete, TEXT("LiveWave")));
		LiveWaveStep->Activate();
		LiveWaveStep->CompleteStep();
		TestEqual(TEXT("a live completion still starts its wave"),
			Director->GetActiveWaveId(), FName(TEXT("LiveWave")));

		Director->CancelWave(TEXT("LiveWave"));
	}

	// --- A route command survives a mis-wire and never replays on resume ---
	// The automation world has no companion at all, so both guards (no route, no companion) are on
	// the path here: the beat must run to completion and hand on rather than stall the chain.

	AObjectiveStep* UnwiredRouteNext = SpawnStep(World, TEXT("UnwiredRouteNext"), EObjectiveCondition::Manual);
	AObjectiveStep* UnwiredRouteStep = SpawnStep(World, TEXT("UnwiredRoute"), EObjectiveCondition::Manual);
	UnwiredRouteStep->TestSetNextStep(UnwiredRouteNext);
	UnwiredRouteStep->TestAddSideEffect(MakeRouteEffect(EObjectiveSideEffectWhen::OnActivate, nullptr));

	UnwiredRouteStep->Activate();
	TestTrue(TEXT("an unwired route command still lets the beat go live"), UnwiredRouteStep->IsStepActive());
	UnwiredRouteStep->CompleteStep();
	TestTrue(TEXT("an unwired route command does not block completion"), UnwiredRouteStep->IsStepCompleted());
	TestTrue(TEXT("an unwired route command does not stall the chain"), UnwiredRouteNext->IsStepActive());

	ACompanionRoute* PlacedRoute = World->SpawnActor<ACompanionRoute>();
	AObjectiveStep* NoCompanionRouteStep = SpawnStep(World, TEXT("NoCompanionRoute"), EObjectiveCondition::Manual);
	NoCompanionRouteStep->TestAddSideEffect(MakeRouteEffect(EObjectiveSideEffectWhen::OnComplete, PlacedRoute));

	NoCompanionRouteStep->Activate();
	NoCompanionRouteStep->CompleteStep();
	TestTrue(TEXT("a wired route with no companion to command still completes the beat"),
		NoCompanionRouteStep->IsStepCompleted());

	AObjectiveStep* SkippedRouteStep = SpawnStep(World, TEXT("SkippedRoute"), EObjectiveCondition::Manual);
	SkippedRouteStep->TestAddSideEffect(MakeRouteEffect(EObjectiveSideEffectWhen::OnActivate, PlacedRoute));
	SkippedRouteStep->TestApplyCompletedWorldState();
	TestTrue(TEXT("a fast-forward past a route beat still marks it complete"),
		SkippedRouteStep->IsStepCompleted());

	// --- Marker target precedence ---

	ALootContainer* Watched = SpawnLootableContainer(World, FVector(800.f, 0.f, 0.f), TEXT("Watched"));
	AObjectiveStep* InteractStep = SpawnStep(World, TEXT("InteractMarker"), EObjectiveCondition::Interacted);
	InteractStep->TestSetTrackedInteractable(Watched);
	TestTrue(TEXT("an unwired marker falls back to the condition's payload actor"),
		InteractStep->TestResolveMarkerTarget() == Watched);

	AObjectiveStep* ExplicitMarkerStep = SpawnStep(World, TEXT("ExplicitMarker"), EObjectiveCondition::Interacted);
	ExplicitMarkerStep->TestSetTrackedInteractable(Watched);
	ExplicitMarkerStep->TestSetMarkerTarget(ResumeStep);
	TestTrue(TEXT("an explicit MarkerTarget outranks the payload"),
		ExplicitMarkerStep->TestResolveMarkerTarget() == ResumeStep);

	// The derived anchors outrank the payload too: containers empty out from under the marker.
	ALootContainer* NearCrate = SpawnLootableContainer(World, FVector(200.f, 0.f, 0.f), TEXT("NearCrate"));
	ALootContainer* FarCrate = SpawnLootableContainer(World, FVector(2000.f, 0.f, 0.f), TEXT("FarCrate"));
	AObjectiveStep* CrateMarkerStep = SpawnStep(World, TEXT("CrateMarker"), EObjectiveCondition::ContainerLooted);
	CrateMarkerStep->TestSetTrackedContainers(TArray<ALootContainer*>{ NearCrate, FarCrate });
	TestTrue(TEXT("a container beat points at the nearest unlooted crate"),
		CrateMarkerStep->TestResolveMarkerTarget() == NearCrate);
	NearCrate->Loot_Implementation(nullptr);
	TestTrue(TEXT("the marker re-points once the nearest crate is emptied"),
		CrateMarkerStep->TestResolveMarkerTarget() == FarCrate);

	// Conditions with no payload actor fall all the way through to the step's own placed location.
	AObjectiveStep* ReachStep = SpawnStep(World, TEXT("ReachMarker"), EObjectiveCondition::ReachLocation);
	TestNull(TEXT("a ReachLocation beat has no marker target"), ReachStep->TestResolveMarkerTarget());
	TestTrue(TEXT("a ReachLocation beat marks its own location"),
		ReachStep->TestResolveStaticMarkerLocation().Equals(ReachStep->GetActorLocation()));

	AObjectiveStep* AreaStep = SpawnStep(World, TEXT("AreaMarker"), EObjectiveCondition::EnemiesDead);
	TestNull(TEXT("an EnemiesDead beat leaves the marker to the area anchor"),
		AreaStep->TestResolveMarkerTarget());

	// --- A frozen marker holds the ground, not the actor that was standing on it ---
	// The Defend beat: the squadmate the marker resolved from walks off, and the player is still
	// meant to hold the spot he was rescued at.

	AObjectiveStep* Mover = SpawnStep(World, TEXT("FrozenMover"), EObjectiveCondition::Manual);
	Mover->SetActorLocation(FVector(1000.f, 1000.f, 0.f));

	AObjectiveStep* FrozenStep = SpawnStep(World, TEXT("FrozenMarker"), EObjectiveCondition::Manual);
	FrozenStep->TestSetMarkerTarget(Mover);
	FrozenStep->TestSetFreezeMarkerAtActivation(true);
	FrozenStep->Activate();

	const FObjectiveMarker* Frozen = FindMarker(World, TEXT("FrozenMarker"));
	TestNotNull(TEXT("a frozen beat still registers a marker"), Frozen);
	if (!Frozen) return false;
	TestNull(TEXT("a frozen marker is registered with no target to follow"), Frozen->TargetActor.Get());

	const FVector PinnedAt = Frozen->WorldLocation;
	TestTrue(TEXT("the pin lands on the target it froze from"),
		FVector::Dist2D(PinnedAt, Mover->GetActorLocation()) < 1.f);

	Mover->SetActorLocation(FVector(5000.f, 5000.f, 0.f));
	FrozenStep->TestEvaluateCondition();
	const FObjectiveMarker* StillFrozen = FindMarker(World, TEXT("FrozenMarker"));
	TestNotNull(TEXT("the frozen marker survives a refresh"), StillFrozen);
	if (!StillFrozen) return false;
	TestTrue(TEXT("the pin ignores the target walking away"), StillFrozen->WorldLocation.Equals(PinnedAt));

	// Control: the same wiring without the flag is the following marker it has always been.
	AObjectiveStep* FollowStep = SpawnStep(World, TEXT("FollowMarker"), EObjectiveCondition::Manual);
	FollowStep->TestSetMarkerTarget(Mover);
	FollowStep->Activate();

	const FObjectiveMarker* Following = FindMarker(World, TEXT("FollowMarker"));
	TestNotNull(TEXT("an unfrozen beat registers a marker"), Following);
	if (!Following) return false;
	TestTrue(TEXT("an unfrozen marker keeps following its target"), Following->TargetActor.Get() == Mover);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveStepResumeSideChainTest,
	"Extraction.Objectives.Step.ResumeSideChainFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveStepResumeSideChainTest::RunTest(const FString& Parameters)
{
	using namespace ObjectiveStepTest;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("automation world exists"), World);
	if (!World) return false;

	UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>();
	TestNotNull(TEXT("the director subsystem exists"), Director);
	if (!Director) return false;

	// --- The resume filter survives the hand-off ---
	// ActivateActor is the one place a fast-forward reaches a beat it is not replaying. The optional-
	// supplies chain is exactly this shape, so it is on the main resume route: without the filter
	// travelling with the activation, the side chain re-starts a wave the player already beat.

	AObjectiveStep* SideChain = SpawnStep(World, TEXT("SideChain"), EObjectiveCondition::Manual);
	SideChain->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnActivate, TEXT("SideChainWave")));

	AObjectiveStep* Replayed = SpawnStep(World, TEXT("Replayed"), EObjectiveCondition::Manual);
	Replayed->TestAddSideEffect(
		MakeActivateEffect(EObjectiveSideEffectWhen::OnActivate, SideChain, /*bReplayOnResume=*/true));

	Replayed->TestApplyCompletedWorldState();
	TestTrue(TEXT("the side chain still goes live on a resume"), SideChain->IsStepActive());
	TestEqual(TEXT("but the wave it would have started stays blocked through the hand-off"),
		Director->GetActiveWaveId(), FName(NAME_None));

	// Positive control: the same step activated live is exactly as free to start its wave as ever.
	AObjectiveStep* LiveSideChain = SpawnStep(World, TEXT("LiveSideChain"), EObjectiveCondition::Manual);
	LiveSideChain->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnActivate, TEXT("LiveSideChainWave")));
	LiveSideChain->Activate();
	TestEqual(TEXT("a live activation still starts its wave"),
		Director->GetActiveWaveId(), FName(TEXT("LiveSideChainWave")));
	Director->CancelWave(TEXT("LiveSideChainWave"));
	LiveSideChain->Deactivate();

	// --- A beat stood up by the resume is stood back down when its own end-state lands ---
	// It is the same actor twice: once as an earlier beat's ActivateActor target, once as its own
	// entry in the replay prefix. Marking it complete without deactivating leaves its HUD objective
	// line registered for a beat the resume has just applied.

	AObjectiveStep* InPrefix = SpawnStep(World, TEXT("InPrefix"), EObjectiveCondition::Manual);
	AObjectiveStep* Activator = SpawnStep(World, TEXT("Activator"), EObjectiveCondition::Manual);
	Activator->TestAddSideEffect(
		MakeActivateEffect(EObjectiveSideEffectWhen::OnActivate, InPrefix, /*bReplayOnResume=*/true));

	Activator->TestApplyCompletedWorldState();
	TestTrue(TEXT("the target beat goes live during the fast-forward"), InPrefix->IsStepActive());
	TestNotNull(TEXT("and registers its objective line"), FindMarker(World, TEXT("InPrefix")));

	InPrefix->TestApplyCompletedWorldState();
	TestTrue(TEXT("its own end-state still applies"), InPrefix->IsStepCompleted());
	TestFalse(TEXT("the beat is stood back down rather than left live"), InPrefix->IsStepActive());
	TestNull(TEXT("no stale HUD objective line outlives the fast-forward"), FindMarker(World, TEXT("InPrefix")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveStepWaveOrphanAuditTest,
	"Extraction.Objectives.Step.WaveOrphanAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveStepWaveOrphanAuditTest::RunTest(const FString& Parameters)
{
	using namespace ObjectiveStepTest;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("automation world exists"), World);
	if (!World) return false;

	// The rescue beat asks for the extraction wave on completion; the defend beat watches for that
	// wave and carries the checkpoint. A resume there never re-starts the wave (StartDirectorWave is
	// one of the three effects a fast-forward refuses), so the defend beat can never complete.
	AObjectiveStep* Rescue = SpawnStep(World, TEXT("Rescue"), EObjectiveCondition::Manual);
	Rescue->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnComplete, TEXT("ExtractWave")));

	AObjectiveStep* Defend = SpawnWaveWatcher(World, TEXT("Defend"), TEXT("ExtractWave"), /*bCheckpoint=*/true);
	TestEqual(TEXT("a checkpoint between the wave's start and the beat watching for it is a soft-lock"),
		AObjectiveStep::TestAuditWaveOrphans(TArray<AObjectiveStep*>{ Rescue, Defend }), 1);

	// The safe shape the migration authors: the watching beat starts its own wave on activation, so
	// the resume that lands on it starts the wave it is about to watch for.
	AObjectiveStep* SafeDefend = SpawnWaveWatcher(World, TEXT("SafeDefend"), TEXT("ExtractWave"), /*bCheckpoint=*/true);
	SafeDefend->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnActivate, TEXT("ExtractWave")));
	TestEqual(TEXT("a watcher that starts its own wave is not orphaned"),
		AObjectiveStep::TestAuditWaveOrphans(TArray<AObjectiveStep*>{ Rescue, SafeDefend }), 0);

	// No checkpoint in between: a fresh run always walks the starting beat, so there is nothing to warn about.
	AObjectiveStep* PlainDefend = SpawnWaveWatcher(World, TEXT("PlainDefend"), TEXT("ExtractWave"), /*bCheckpoint=*/false);
	TestEqual(TEXT("without a checkpoint in between there is nothing to warn about"),
		AObjectiveStep::TestAuditWaveOrphans(TArray<AObjectiveStep*>{ Rescue, PlainDefend }), 0);

	// A wave nothing on the chain starts belongs to something else (the extraction target actor owns
	// its own) — the audit must not cry wolf over it.
	AObjectiveStep* ForeignDefend = SpawnWaveWatcher(World, TEXT("ForeignDefend"), TEXT("SomebodyElsesWave"), /*bCheckpoint=*/true);
	TestEqual(TEXT("a wave no beat on the chain starts is not this audit's business"),
		AObjectiveStep::TestAuditWaveOrphans(TArray<AObjectiveStep*>{ Rescue, ForeignDefend }), 0);

	// A checkpoint BEFORE the wave starts is fine: the resume replays the starting beat.
	AObjectiveStep* EarlyCheckpoint = SpawnStep(World, TEXT("EarlyCheckpoint"), EObjectiveCondition::Manual);
	EarlyCheckpoint->TestSetCheckpoint(true);
	TestEqual(TEXT("a checkpoint ahead of the wave's start is safe"),
		AObjectiveStep::TestAuditWaveOrphans(TArray<AObjectiveStep*>{ EarlyCheckpoint, Rescue, PlainDefend }), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveStepResumeCompletionFilterTest,
	"Extraction.Objectives.Step.ResumeCompletionFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveStepResumeCompletionFilterTest::RunTest(const FString& Parameters)
{
	using namespace ObjectiveStepTest;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("automation world exists"), World);
	if (!World) return false;

	UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>();
	TestNotNull(TEXT("the director subsystem exists"), Director);
	if (!Director) return false;

	// --- A beat the fast-forward itself satisfies is finishing AS PART of the resume ---
	// The card an earlier prefix beat re-granted is already in the mission inventory by the time the
	// side chain it stands up goes live, so that beat completes synchronously at level load. Its
	// OnComplete effects belong to the load, not to play — and neither does the cascade past it.

	AObjectiveStep* Cascade = SpawnStep(World, TEXT("ResumeCascade"), EObjectiveCondition::Manual);
	Cascade->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnActivate, TEXT("CascadeWave")));

	AObjectiveStep* PreSatisfied = SpawnStep(World, TEXT("PreSatisfied"), EObjectiveCondition::AcquireKeycard);
	PreSatisfied->TestSetRequiredKeycardId(TEXT("ResumeCard"));
	PreSatisfied->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnComplete, TEXT("PreSatisfiedWave")));
	PreSatisfied->TestSetNextStep(Cascade);

	AObjectiveStep* CardBeat = SpawnStep(World, TEXT("CardBeat"), EObjectiveCondition::AcquireKeycard);
	CardBeat->TestSetRequiredKeycardId(TEXT("ResumeCard"));
	CardBeat->TestAddSideEffect(
		MakeActivateEffect(EObjectiveSideEffectWhen::OnComplete, PreSatisfied, /*bReplayOnResume=*/true));

	CardBeat->TestApplyCompletedWorldState();
	TestTrue(TEXT("the side chain completes on the card the fast-forward re-granted"),
		PreSatisfied->IsStepCompleted());
	TestEqual(TEXT("but the wave its completion would have started stays blocked"),
		Director->GetActiveWaveId(), FName(NAME_None));
	TestTrue(TEXT("the beat it cascades into still goes live"), Cascade->IsStepActive());
	TestEqual(TEXT("and the cascade carries the same filter rather than going live one beat later"),
		Director->GetActiveWaveId(), FName(NAME_None));

	// Positive control: the same late-entry completion reached LIVE is as free to start its wave as
	// it has always been.
	AObjectiveStep* LiveLateEntry = SpawnStep(World, TEXT("LiveLateEntry"), EObjectiveCondition::AcquireKeycard);
	LiveLateEntry->TestSetRequiredKeycardId(TEXT("ResumeCard"));
	LiveLateEntry->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnComplete, TEXT("LiveLateEntryWave")));

	LiveLateEntry->Activate();
	TestTrue(TEXT("a live beat still completes on a card already held"), LiveLateEntry->IsStepCompleted());
	TestEqual(TEXT("and a live completion runs its wave"),
		Director->GetActiveWaveId(), FName(TEXT("LiveLateEntryWave")));
	Director->CancelWave(TEXT("LiveLateEntryWave"));

	// --- The filter does not outlive the activation that carried it ---
	// A beat the resume stands up but does NOT satisfy is left for the player. When they finish it,
	// minutes later, it is an ordinary completion and runs every effect.

	AObjectiveStep* LaterBeat = SpawnStep(World, TEXT("LaterBeat"), EObjectiveCondition::Manual);
	LaterBeat->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnComplete, TEXT("LaterWave")));

	AObjectiveStep* StandsUp = SpawnStep(World, TEXT("StandsUp"), EObjectiveCondition::Manual);
	StandsUp->TestAddSideEffect(
		MakeActivateEffect(EObjectiveSideEffectWhen::OnActivate, LaterBeat, /*bReplayOnResume=*/true));

	StandsUp->TestApplyCompletedWorldState();
	TestTrue(TEXT("the beat the resume stood up is live"), LaterBeat->IsStepActive());
	TestFalse(TEXT("and unfinished — nothing satisfied it"), LaterBeat->IsStepCompleted());

	LaterBeat->CompleteStep();
	TestEqual(TEXT("the player's own completion runs every effect"),
		Director->GetActiveWaveId(), FName(TEXT("LaterWave")));
	Director->CancelWave(TEXT("LaterWave"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveStepResumePointScopeTest,
	"Extraction.Objectives.Step.ResumePointOutsidePrefix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveStepResumePointScopeTest::RunTest(const FString& Parameters)
{
	using namespace ObjectiveStepTest;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("automation world exists"), World);
	if (!World) return false;

	UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>();
	TestNotNull(TEXT("the director subsystem exists"), Director);
	if (!Director) return false;

	// --- A prefix beat naming the resume step does not stand it up ---
	// The shape the wave-orphan audit mandates is StartDirectorWave on the WATCHING beat's OnActivate.
	// A walk that activated that beat would filter exactly that effect, and the audit would never
	// catch it because the authored shape is the safe one.

	AObjectiveStep* Resume = SpawnWaveWatcher(World, TEXT("ResumePoint"), TEXT("ResumeWave"), /*bCheckpoint=*/false);
	Resume->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnActivate, TEXT("ResumeWave")));

	AObjectiveStep* Prefix = SpawnStep(World, TEXT("PrefixNamingResume"), EObjectiveCondition::Manual);
	Prefix->TestSetNextStep(Resume);
	Prefix->TestAddSideEffect(
		MakeActivateEffect(EObjectiveSideEffectWhen::OnActivate, Resume, /*bReplayOnResume=*/true));

	Prefix->TestReplayPrefix(TArray<AObjectiveStep*>{ Prefix }, Resume);
	TestTrue(TEXT("the prefix beat is applied"), Prefix->IsStepCompleted());
	TestFalse(TEXT("the resume point is not stood up from inside the walk"), Resume->IsStepActive());
	TestEqual(TEXT("so its marker and its wave both wait for the checkpoint teleport"),
		Director->GetActiveWaveId(), FName(NAME_None));

	// The caller's activation, after the teleport: unfiltered, exactly as a fresh run would run it.
	Resume->Activate();
	TestTrue(TEXT("the resume point goes live once the caller stands it up"), Resume->IsStepActive());
	TestEqual(TEXT("and starts the wave it is about to watch for"),
		Director->GetActiveWaveId(), FName(TEXT("ResumeWave")));
	Director->CancelWave(TEXT("ResumeWave"));
	Resume->Deactivate();

	// --- A beat PAST the resume point is ahead of the player, so it is stood up live ---

	AObjectiveStep* PastResume = SpawnStep(World, TEXT("PastResume"), EObjectiveCondition::Manual);
	PastResume->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnActivate, TEXT("PastResumeWave")));

	AObjectiveStep* SecondResume = SpawnStep(World, TEXT("SecondResume"), EObjectiveCondition::Manual);
	SecondResume->TestSetNextStep(PastResume);

	AObjectiveStep* SecondPrefix = SpawnStep(World, TEXT("SecondPrefix"), EObjectiveCondition::Manual);
	SecondPrefix->TestSetNextStep(SecondResume);
	SecondPrefix->TestAddSideEffect(
		MakeActivateEffect(EObjectiveSideEffectWhen::OnActivate, PastResume, /*bReplayOnResume=*/true));

	SecondPrefix->TestReplayPrefix(TArray<AObjectiveStep*>{ SecondPrefix }, SecondResume);
	TestTrue(TEXT("a beat past the resume point still goes live"), PastResume->IsStepActive());
	TestEqual(TEXT("and keeps effects the player has never seen run"),
		Director->GetActiveWaveId(), FName(TEXT("PastResumeWave")));
	Director->CancelWave(TEXT("PastResumeWave"));

	// --- Control: a side chain the walk reaches is still inside the replay ---
	// It is not on the chain at all, so it is neither at nor after the resume point: the filter that
	// stops it re-starting a beaten wave has to survive the hand-off.

	AObjectiveStep* SideChain = SpawnStep(World, TEXT("ScopedSideChain"), EObjectiveCondition::Manual);
	SideChain->TestAddSideEffect(MakeWaveEffect(EObjectiveSideEffectWhen::OnActivate, TEXT("ScopedSideChainWave")));

	AObjectiveStep* ThirdResume = SpawnStep(World, TEXT("ThirdResume"), EObjectiveCondition::Manual);
	AObjectiveStep* ThirdPrefix = SpawnStep(World, TEXT("ThirdPrefix"), EObjectiveCondition::Manual);
	ThirdPrefix->TestSetNextStep(ThirdResume);
	ThirdPrefix->TestAddSideEffect(
		MakeActivateEffect(EObjectiveSideEffectWhen::OnActivate, SideChain, /*bReplayOnResume=*/true));

	ThirdPrefix->TestReplayPrefix(TArray<AObjectiveStep*>{ ThirdPrefix }, ThirdResume);
	TestTrue(TEXT("the side chain still goes live on a resume"), SideChain->IsStepActive());
	TestEqual(TEXT("but the wave it would have started stays blocked"),
		Director->GetActiveWaveId(), FName(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLootContainerRestoreHookDeferralTest,
	"Extraction.Objectives.Step.RestoredLootedDeferral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLootContainerRestoreHookDeferralTest::RunTest(const FString& Parameters)
{
	using namespace ObjectiveStepTest;

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("automation world exists"), World);
	if (!World) return false;

	// The fast-forward runs from the entry step's BeginPlay, and actor BeginPlay order is unspecified:
	// a container Blueprint that caches its closed pose in its OWN BeginPlay would overwrite an inline
	// restore, and the emptied drawer would read as untouched all over again.

	ALootContainer* Restored = SpawnLootableContainer(World, FVector(0.f, 0.f, 300.f), TEXT("RestoredCard"));
	AObjectiveStep* SkippedCrateBeat = SpawnStep(World, TEXT("SkippedCrateBeat"), EObjectiveCondition::ContainerLooted);
	SkippedCrateBeat->TestSetTrackedContainers(TArray<ALootContainer*>{ Restored });

	SkippedCrateBeat->TestApplyCompletedWorldState();
	TestTrue(TEXT("the fast-forward empties the crate at once"), Restored->IsLooted());
	TestEqual(TEXT("but the restore hook does not fire inside the entry step's BeginPlay"),
		Restored->TestGetRestoredLootedCount(), 0);

	PumpNextTickTimers(World);
	TestEqual(TEXT("the restore hook lands on the next tick"), Restored->TestGetRestoredLootedCount(), 1);

	// An already-emptied crate is a no-op, so the pose is restored exactly once per level load.
	Restored->MarkLootedForCheckpoint();
	PumpNextTickTimers(World);
	TestEqual(TEXT("the restore hook fires exactly once"), Restored->TestGetRestoredLootedCount(), 1);

	// A crate destroyed inside the one-tick gap must not take the pending hook down with it.
	ALootContainer* Doomed = SpawnLootableContainer(World, FVector(0.f, 0.f, 500.f), TEXT("DoomedCard"));
	Doomed->MarkLootedForCheckpoint();
	Doomed->Destroy();
	PumpNextTickTimers(World);
	TestEqual(TEXT("a destroyed crate leaves the surviving crate's hook alone"),
		Restored->TestGetRestoredLootedCount(), 1);
	return true;
}

#endif
