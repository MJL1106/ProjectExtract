#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/LevelObjectiveFlow.h"
#include "UI/ObjectiveMarkerWidget.h"
#include "UObject/UnrealType.h"
#include "Tests/AutomationEditorCommon.h"
#include "World/ScriptedDoor.h"
#include "World/LootContainer.h"
#include "World/ExtractionTargetActor.h"
#include "Enemy/EnemyCharacter.h"
#include "Components/HealthComponent.h"
#include "Character/ExtractionPlayer.h"
#include "Components/ConsumableInventoryComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"


namespace LevelObjectiveWorldTest
{
	template <typename TObjectType>
	void SetObjectArray(UObject* Owner, const FName PropertyName, const TArray<TObjectType*>& Values)
	{
		FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(Owner->GetClass(), PropertyName);
		check(ArrayProperty);
		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Owner));
		Helper.Resize(Values.Num());
		FObjectPropertyBase* Inner = CastFieldChecked<FObjectPropertyBase>(ArrayProperty->Inner);
		for (int32 Index = 0; Index < Values.Num(); ++Index)
			Inner->SetObjectPropertyValue(Helper.GetRawPtr(Index), Values[Index]);
	}

	ALevelObjectiveFlow* SpawnDormantFlow(UWorld* World)
	{
		ALevelObjectiveFlow* Flow = World->SpawnActorDeferred<ALevelObjectiveFlow>(
			ALevelObjectiveFlow::StaticClass(), FTransform::Identity);
		FBoolProperty* AutoProperty = FindFProperty<FBoolProperty>(Flow->GetClass(), TEXT("bAutoActivate"));
		check(AutoProperty);
		AutoProperty->SetPropertyValue_InContainer(Flow, false);
		Flow->FinishSpawning(FTransform::Identity);
		return Flow;
	}
	void GiveOneStim(ALootContainer* Container)
	{
		FArrayProperty* ContentsProperty = FindFProperty<FArrayProperty>(Container->GetClass(), TEXT("Contents"));
		check(ContentsProperty);
		FScriptArrayHelper Helper(ContentsProperty, ContentsProperty->ContainerPtrToValuePtr<void>(Container));
		Helper.Resize(1);
		FLootGrant* Grant = reinterpret_cast<FLootGrant*>(Helper.GetRawPtr(0));
		Grant->Type = ELootType::Stim;
		Grant->StimCount = 1;
	}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelObjectiveStateMachineTest,
	"Extraction.Objectives.LevelFlow.PrimarySequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLevelObjectiveStateMachineTest::RunTest(const FString& Parameters)
{
	FLevelObjectiveStateMachine State;
	TestEqual(TEXT("flow starts inactive"), State.GetCurrentStep(), ELevelObjectiveStep::Inactive);
	TestTrue(TEXT("activation enters roof breach"), State.Activate());

	const TArray<TPair<ELevelObjectiveEvent, ELevelObjectiveStep>> Sequence = {
		{ ELevelObjectiveEvent::RoofDoorOpened, ELevelObjectiveStep::FollowCompanionDownstairs },
		{ ELevelObjectiveEvent::RouteCompleted, ELevelObjectiveStep::BreachStairwellDoor },
		{ ELevelObjectiveEvent::StairwellDoorOpened, ELevelObjectiveStep::ClearRoom1 },
		{ ELevelObjectiveEvent::Room1Cleared, ELevelObjectiveStep::FindOfficeKeycard },
		{ ELevelObjectiveEvent::KeycardLooted, ELevelObjectiveStep::UnlockStairwellDoor },
		{ ELevelObjectiveEvent::ExitDoorOpened, ELevelObjectiveStep::SwitchCompanionToStealth },
		{ ELevelObjectiveEvent::Room2DoorOpened, ELevelObjectiveStep::FirstDoubleTakedown },
		{ ELevelObjectiveEvent::FirstPairCleared, ELevelObjectiveStep::SecondDoubleTakedown },
		{ ELevelObjectiveEvent::SecondPairCleared, ELevelObjectiveStep::ReachExtractionTarget },
		{ ELevelObjectiveEvent::ExtractionStarted, ELevelObjectiveStep::DefendPosition },
		{ ELevelObjectiveEvent::ExtractionCompleted, ELevelObjectiveStep::UseLift },
		{ ELevelObjectiveEvent::LiftCompleted, ELevelObjectiveStep::Completed },
	};

	for (const TPair<ELevelObjectiveEvent, ELevelObjectiveStep>& Transition : Sequence)
	{
		TestTrue(TEXT("expected event advances"), State.Advance(Transition.Key));
		TestEqual(TEXT("expected step selected"), State.GetCurrentStep(), Transition.Value);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelObjectiveOutOfOrderEventTest,
	"Extraction.Objectives.LevelFlow.OutOfOrderEventsDoNotAdvance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLevelObjectiveOutOfOrderEventTest::RunTest(const FString& Parameters)
{
	FLevelObjectiveStateMachine State;
	State.Activate();
	TestFalse(TEXT("early Room 1 clear is ignored"), State.Advance(ELevelObjectiveEvent::Room1Cleared));
	TestEqual(TEXT("roof objective remains active"), State.GetCurrentStep(), ELevelObjectiveStep::BreachRoofDoor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveMarkerMathTest,
	"Extraction.Objectives.Marker.SmoothingAndClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FObjectiveMarkerMathTest::RunTest(const FString& Parameters)
{
	const FVector2D Start(0.f, 0.f);
	const FVector2D Target(100.f, 100.f);
	const FVector2D OneFrame = UObjectiveMarkerWidget::InterpolateScreenPosition(Start, Target, 1.f, 4.f);
	FVector2D TenFrames = Start;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		TenFrames = UObjectiveMarkerWidget::InterpolateScreenPosition(TenFrames, Target, 0.1f, 4.f);
	}
	TestTrue(TEXT("convergence is frame-rate independent"), OneFrame.Equals(TenFrames, 0.01f));

	const FVector2D Clamped = UObjectiveMarkerWidget::ClampToViewport(
		FVector2D(-50.f, 900.f), FVector2D(1280.f, 720.f), 60.f);
	TestEqual(TEXT("left edge clamps"), Clamped.X, 60.0);
	TestEqual(TEXT("bottom edge clamps"), Clamped.Y, 660.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelObjectiveReplicationContractTest,
	"Extraction.Objectives.LevelFlow.ReplicationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLevelObjectiveReplicationContractTest::RunTest(const FString& Parameters)
{
	const ALevelObjectiveFlow* DefaultFlow = GetDefault<ALevelObjectiveFlow>();
	TestTrue(TEXT("flow actor replicates"), DefaultFlow->GetIsReplicated());
	TestFalse(TEXT("flow remains non-ticking"), DefaultFlow->PrimaryActorTick.bCanEverTick);

	const FProperty* CurrentStepProperty = ALevelObjectiveFlow::StaticClass()->FindPropertyByName(TEXT("CurrentStep"));
	TestNotNull(TEXT("current step property exists"), CurrentStepProperty);
	if (CurrentStepProperty)
	{
		TestTrue(TEXT("current step is replicated"), CurrentStepProperty->HasAnyPropertyFlags(CPF_Net));
		TestEqual(TEXT("current step has presentation rep-notify"),
			CurrentStepProperty->RepNotifyFunc, FName(TEXT("OnRep_CurrentStep")));
	}

	const FProperty* OptionalProperty = ALevelObjectiveFlow::StaticClass()->FindPropertyByName(TEXT("bOptionalSuppliesActive"));
	TestNotNull(TEXT("optional objective state exists"), OptionalProperty);
	if (OptionalProperty)
		TestTrue(TEXT("optional objective activation replicates independently"), OptionalProperty->HasAnyPropertyFlags(CPF_Net));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelObjectivePairGateTest,
	"Extraction.Objectives.LevelFlow.PairAndOptionalGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLevelObjectivePairGateTest::RunTest(const FString& Parameters)
{
	FLevelObjectiveStateMachine State;
	State.Activate();
	const ELevelObjectiveEvent ToFirstPair[] = {
		ELevelObjectiveEvent::RoofDoorOpened, ELevelObjectiveEvent::RouteCompleted,
		ELevelObjectiveEvent::StairwellDoorOpened, ELevelObjectiveEvent::Room1Cleared,
		ELevelObjectiveEvent::KeycardLooted, ELevelObjectiveEvent::ExitDoorOpened,
		ELevelObjectiveEvent::Room2DoorOpened };
	for (const ELevelObjectiveEvent Event : ToFirstPair) State.Advance(Event);

	TestEqual(TEXT("first pair step reached"), State.GetCurrentStep(), ELevelObjectiveStep::FirstDoubleTakedown);
	TestFalse(TEXT("second-pair completion cannot satisfy first pair"), State.Advance(ELevelObjectiveEvent::SecondPairCleared));
	TestEqual(TEXT("pair remains gated"), State.GetCurrentStep(), ELevelObjectiveStep::FirstDoubleTakedown);
	TestTrue(TEXT("explicit first pair completion advances"), State.Advance(ELevelObjectiveEvent::FirstPairCleared));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLevelObjectiveActorSeamsTest,
	"Extraction.Objectives.LevelFlow.ActorWorldSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLevelObjectiveActorSeamsTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("automation world exists"), World);
	if (!World) return false;

	AScriptedDoor* Door = World->SpawnActor<AScriptedDoor>();
	TestFalse(TEXT("door request has not completed-open"), Door->TestHasBroadcastDoorOpened());
	Door->Breach_Implementation(nullptr);
	TestFalse(TEXT("breach request does not complete objective early"), Door->TestHasBroadcastDoorOpened());
	Door->NotifyDoorStateChanged(true);
	TestTrue(TEXT("fully-open notification completes door"), Door->TestHasBroadcastDoorOpened());
	// Direct notify calls with no swing-start are exactly what the unwired-door tripwire warns on.
	AddExpectedMessage(TEXT("no swing-start recorded"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	Door->NotifyDoorStateChanged(false);
	Door->NotifyDoorStateChanged(true);
	TestTrue(TEXT("door completion remains one-shot latched"), Door->TestHasBroadcastDoorOpened());

	AExtractionTargetActor* Target = World->SpawnActor<AExtractionTargetActor>();
	TestFalse(TEXT("extraction is dormant before activation"), Target->CanWorldInteract_Implementation(nullptr));
	Target->ActivateTarget();
	TestTrue(TEXT("explicit activation enables extraction"), Target->CanWorldInteract_Implementation(nullptr));

	AExtractionPlayer* Player = World->SpawnActor<AExtractionPlayer>();
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController)
	{
		PlayerController = World->SpawnActor<APlayerController>();
		if (PlayerController && UGameplayStatics::GetPlayerController(World, 0) != PlayerController)
			World->AddController(PlayerController);
	}
	TestNotNull(TEXT("test player controller resolved"), PlayerController);
	if (!PlayerController) return false;
	PlayerController->SetRole(ROLE_Authority);
	PlayerController->Possess(Player);
	TestEqual(TEXT("loot default recipient resolves test player"),
		UGameplayStatics::GetPlayerPawn(World, 0), static_cast<APawn*>(Player));
	Player->GetConsumableInventoryComponent()->AddStims(3);
	ALootContainer* FullGrantCrate = World->SpawnActor<ALootContainer>();
	FullGrantCrate->SetRole(ROLE_Authority);
	LevelObjectiveWorldTest::GiveOneStim(FullGrantCrate);
	TestTrue(TEXT("full crate fixture is authoritative"), FullGrantCrate->HasAuthority());
	TestTrue(TEXT("full crate fixture is lootable"), FullGrantCrate->CanLoot_Implementation());
	FullGrantCrate->Loot_Implementation(Player);
	TestTrue(TEXT("rejected full-capacity grant still completes crate"), FullGrantCrate->IsLooted());
	TestEqual(TEXT("rejected full-capacity grant leaves stims unchanged"),
		Player->GetConsumableInventoryComponent()->GetStimCount(), 3);
	TestEqual(TEXT("loot completion broadcasts once"), FullGrantCrate->TestGetCompletionBroadcastCount(), 1);
	FullGrantCrate->Loot_Implementation(Player);
	TestEqual(TEXT("repeat loot does not rebroadcast"), FullGrantCrate->TestGetCompletionBroadcastCount(), 1);

	ALevelObjectiveFlow* MissingFlow = LevelObjectiveWorldTest::SpawnDormantFlow(World);
	AddExpectedError(TEXT("missing required reference"), EAutomationExpectedErrorFlags::Contains, 8);
	AddExpectedError(TEXT("Room1Enemies is empty"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("each takedown pair"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("SupplyCrates must contain"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("missing required references reject activation"), MissingFlow->TestValidateReferences());

	AEnemyCharacter* PairA = World->SpawnActor<AEnemyCharacter>();
	AEnemyCharacter* PairB = World->SpawnActor<AEnemyCharacter>();
	AEnemyCharacter* PairSecondA = World->SpawnActor<AEnemyCharacter>();
	AEnemyCharacter* PairSecondB = World->SpawnActor<AEnemyCharacter>();
	ALevelObjectiveFlow* PairFlow = LevelObjectiveWorldTest::SpawnDormantFlow(World);
	LevelObjectiveWorldTest::SetObjectArray(PairFlow, TEXT("FirstTakedownPair"), TArray<AEnemyCharacter*>{PairA, PairB});
	LevelObjectiveWorldTest::SetObjectArray(PairFlow, TEXT("SecondTakedownPair"), TArray<AEnemyCharacter*>{PairSecondA, PairSecondB});
	PairFlow->TestSetCurrentStep(ELevelObjectiveStep::FirstDoubleTakedown);
	PairA->GetHealthComponent()->Die();
	PairFlow->TestEvaluateCurrentEnemyStep();
	TestEqual(TEXT("one member cannot complete pair"), PairFlow->GetCurrentStep(), ELevelObjectiveStep::FirstDoubleTakedown);
	PairB->GetHealthComponent()->Die();
	PairFlow->TestEvaluateCurrentEnemyStep();
	TestEqual(TEXT("both members complete pair"), PairFlow->GetCurrentStep(), ELevelObjectiveStep::SecondDoubleTakedown);

	AEnemyCharacter* EarlyA = World->SpawnActor<AEnemyCharacter>();
	AEnemyCharacter* EarlyB = World->SpawnActor<AEnemyCharacter>();
	ALevelObjectiveFlow* EarlyFlow = LevelObjectiveWorldTest::SpawnDormantFlow(World);
	LevelObjectiveWorldTest::SetObjectArray(EarlyFlow, TEXT("FirstTakedownPair"), TArray<AEnemyCharacter*>{EarlyA, EarlyB});
	LevelObjectiveWorldTest::SetObjectArray(EarlyFlow, TEXT("SecondTakedownPair"), TArray<AEnemyCharacter*>{PairSecondA, PairSecondB});
	EarlyA->GetHealthComponent()->Die();
	EarlyB->GetHealthComponent()->Die();
	EarlyFlow->TestEvaluateCurrentEnemyStep();
	EarlyA->Destroy();
	EarlyB->Destroy();
	EarlyFlow->TestSetCurrentStep(ELevelObjectiveStep::FirstDoubleTakedown);
	EarlyFlow->TestEvaluateCurrentEnemyStep();
	TestEqual(TEXT("early deaths survive corpse destruction and catch up"),
		EarlyFlow->GetCurrentStep(), ELevelObjectiveStep::SecondDoubleTakedown);

	ALootContainer* NearCrate = World->SpawnActor<ALootContainer>(FVector(100.f, 0.f, 0.f), FRotator::ZeroRotator);
	ALootContainer* MidCrate = World->SpawnActor<ALootContainer>(FVector(500.f, 0.f, 0.f), FRotator::ZeroRotator);
	ALootContainer* FarCrate = World->SpawnActor<ALootContainer>(FVector(1000.f, 0.f, 0.f), FRotator::ZeroRotator);
	NearCrate->SetRole(ROLE_Authority);
	MidCrate->SetRole(ROLE_Authority);
	FarCrate->SetRole(ROLE_Authority);
	LevelObjectiveWorldTest::GiveOneStim(NearCrate);
	LevelObjectiveWorldTest::GiveOneStim(MidCrate);
	LevelObjectiveWorldTest::GiveOneStim(FarCrate);
	ALevelObjectiveFlow* OptionalFlow = LevelObjectiveWorldTest::SpawnDormantFlow(World);
	LevelObjectiveWorldTest::SetObjectArray(OptionalFlow, TEXT("SupplyCrates"), TArray<ALootContainer*>{NearCrate, MidCrate, FarCrate});
	OptionalFlow->TestActivateOptionalSupplies();
	TestTrue(TEXT("optional objective activates independently"), OptionalFlow->TestIsOptionalSuppliesActive());
	TestEqual(TEXT("nearest unlooted crate is targeted"), OptionalFlow->TestGetOptionalTarget(), NearCrate);
	NearCrate->Destroy();
	OptionalFlow->TestRefreshOptionalSupplies();
	TestEqual(TEXT("optional target retargets after destroyed crate"), OptionalFlow->TestGetOptionalTarget(), MidCrate);
	MidCrate->Loot_Implementation(Player);
	OptionalFlow->TestRefreshOptionalSupplies();
	TestEqual(TEXT("optional target retargets after loot"), OptionalFlow->TestGetOptionalTarget(), FarCrate);
	FarCrate->Loot_Implementation(Player);
	OptionalFlow->TestRefreshOptionalSupplies();
	TestFalse(TEXT("optional objective deactivates after all crates"), OptionalFlow->TestIsOptionalSuppliesActive());
	TestNull(TEXT("completed optional objective clears replicated target"), OptionalFlow->TestGetOptionalTarget());
	return true;
}
#endif
