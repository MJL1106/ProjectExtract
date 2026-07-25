// Automation coverage for authoritative stim inventory limits and player use behavior.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/ExtractionPlayer.h"
#include "Components/ConsumableInventoryComponent.h"
#include "Components/HealthComponent.h"
#include "Engine/World.h"
#include "InputActionValue.h"
#include "Tests/AutomationEditorCommon.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace ConsumableInventoryTest
{
	AExtractionPlayer* SpawnPlayer(UWorld* World)
	{
		return IsValid(World) ? World->SpawnActor<AExtractionPlayer>() : nullptr;
	}

	void SetDamagedHealth(AExtractionPlayer* Player, const float Damage)
	{
		UHealthComponent* Health = Player ? Player->GetHealthComponent() : nullptr;
		if (!Health) return;

		Health->InitializeHealth(100.f, 0.f);
		Health->TakeDamage(Damage);
	}

	/** The stim heal and its lockout are timer-driven; an editor test world never ticks on its
	 *  own. FTimerManager no-ops when it has already ticked the current frame, hence the counter. */
	void AdvanceTimers(UWorld* World, const float Seconds)
	{
		if (!IsValid(World)) return;

		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConsumableInventoryStimTest, "Extraction.Inventory.Consumables.Stims",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConsumableInventoryStimTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	AExtractionPlayer* Player = ConsumableInventoryTest::SpawnPlayer(World);
	TestNotNull(TEXT("test player spawned"), Player);
	if (!Player) return false;

	UConsumableInventoryComponent* Inventory = Player->GetConsumableInventoryComponent();
	UHealthComponent* Health = Player->GetHealthComponent();
	TestNotNull(TEXT("player owns consumable inventory"), Inventory);
	TestNotNull(TEXT("player owns health component"), Health);
	if (!Inventory || !Health) return false;

	// StartingStims lands from BeginPlay, which an editor test world never dispatches. Normalise
	// so the capacity assertions read the same either way.
	Inventory->TestSetStimCount(0);
	TestEqual(TEXT("normalised stim count is zero"), Inventory->GetStimCount(), 0);
	TestEqual(TEXT("partial grant reports amount added"), Inventory->AddStims(2), 2);
	TestEqual(TEXT("partial grant is stored"), Inventory->GetStimCount(), 2);
	TestEqual(TEXT("grant clamps to capacity"), Inventory->AddStims(5), 1);
	TestEqual(TEXT("capacity is exactly three"), Inventory->GetStimCount(), 3);
	TestEqual(TEXT("full inventory rejects grant"), Inventory->AddStims(1), 0);

	Health->InitializeHealth(100.f, 25.f);
	const float FullHealthShield = Health->GetCurrentShield();
	TestFalse(TEXT("full-health use is rejected"), Inventory->TryUseStim());
	TestFalse(TEXT("full-health rejection starts no injection"), Inventory->IsUsingStim());
	TestEqual(TEXT("full-health rejection consumes nothing"), Inventory->GetStimCount(), 3);
	TestEqual(TEXT("full-health rejection does not change shield"), Health->GetCurrentShield(), FullHealthShield);

	ConsumableInventoryTest::SetDamagedHealth(Player, 75.f);
	const float DamagedShield = Health->GetCurrentShield();
	TestTrue(TEXT("damaged player can use stim"), Inventory->TryUseStim());
	TestTrue(TEXT("use opens the injection window"), Inventory->IsUsingStim());
	TestEqual(TEXT("successful use consumes one immediately"), Inventory->GetStimCount(), 2);
	TestEqual(TEXT("heal does not land on the press"), Health->GetCurrentHealth(), 25.f);
	TestFalse(TEXT("a second press mid-injection is refused"), Inventory->TryUseStim());
	TestEqual(TEXT("refused double-press consumes nothing"), Inventory->GetStimCount(), 2);

	ConsumableInventoryTest::AdvanceTimers(World, 2.f);
	TestEqual(TEXT("stim restores exactly fifty health on the needle hit"), Health->GetCurrentHealth(), 75.f);
	TestEqual(TEXT("stim does not restore shield"), Health->GetCurrentShield(), DamagedShield);
	TestTrue(TEXT("injection window outlasts the heal"), Inventory->IsUsingStim());

	ConsumableInventoryTest::AdvanceTimers(World, 2.f);
	TestFalse(TEXT("injection window closes on its own"), Inventory->IsUsingStim());

	ConsumableInventoryTest::SetDamagedHealth(Player, 10.f);
	TestTrue(TEXT("healing clamps at max health"), Inventory->TryUseStim());
	ConsumableInventoryTest::AdvanceTimers(World, 4.f);
	TestEqual(TEXT("clamped heal reaches max health"), Health->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("clamped heal still consumes one"), Inventory->GetStimCount(), 1);

	Health->Die();
	TestFalse(TEXT("dead player cannot use a stim"), Inventory->TryUseStim());
	TestEqual(TEXT("dead rejection consumes nothing"), Inventory->GetStimCount(), 1);

	AExtractionPlayer* CancelPlayer = ConsumableInventoryTest::SpawnPlayer(World);
	TestNotNull(TEXT("cancel test player spawned"), CancelPlayer);
	UConsumableInventoryComponent* CancelInventory = CancelPlayer ? CancelPlayer->GetConsumableInventoryComponent() : nullptr;
	TestNotNull(TEXT("cancel test player owns consumable inventory"), CancelInventory);
	if (CancelInventory)
	{
		CancelInventory->TestSetStimCount(1);
		ConsumableInventoryTest::SetDamagedHealth(CancelPlayer, 75.f);
		TestTrue(TEXT("cancel fixture starts an injection"), CancelInventory->TryUseStim());
		CancelInventory->CancelStimUse();
		TestFalse(TEXT("cancel closes the injection window"), CancelInventory->IsUsingStim());
		ConsumableInventoryTest::AdvanceTimers(World, 4.f);
		TestEqual(TEXT("cancelled injection forfeits the heal"),
			CancelPlayer->GetHealthComponent()->GetCurrentHealth(), 25.f);
		TestEqual(TEXT("cancelled injection still spent the stim"), CancelInventory->GetStimCount(), 0);
	}

	AExtractionPlayer* DBNOPlayer = ConsumableInventoryTest::SpawnPlayer(World);
	TestNotNull(TEXT("DBNO test player spawned"), DBNOPlayer);
	UConsumableInventoryComponent* DBNOInventory = DBNOPlayer ? DBNOPlayer->GetConsumableInventoryComponent() : nullptr;
	TestNotNull(TEXT("DBNO test player owns consumable inventory"), DBNOInventory);
	if (DBNOInventory)
	{
		DBNOInventory->TestSetStimCount(1);
		ConsumableInventoryTest::SetDamagedHealth(DBNOPlayer, 25.f);
		FBoolProperty* DBNOProperty = FindFProperty<FBoolProperty>(DBNOPlayer->GetClass(), TEXT("bIsDBNO"));
		check(DBNOProperty);
		DBNOProperty->SetPropertyValue_InContainer(DBNOPlayer, true);
		TestTrue(TEXT("DBNO fixture state is active"), DBNOPlayer->GetIsDBNO());
		TestFalse(TEXT("DBNO player cannot use a stim"), DBNOInventory->TryUseStim());
		TestEqual(TEXT("DBNO rejection consumes nothing"), DBNOInventory->GetStimCount(), 1);
	}

	AExtractionPlayer* InputPlayer = ConsumableInventoryTest::SpawnPlayer(World);
	TestNotNull(TEXT("input-path test player spawned"), InputPlayer);
	UConsumableInventoryComponent* InputInventory = InputPlayer ? InputPlayer->GetConsumableInventoryComponent() : nullptr;
	TestNotNull(TEXT("input-path test player owns consumable inventory"), InputInventory);
	if (InputInventory)
	{
		ConsumableInventoryTest::SetDamagedHealth(InputPlayer, 75.f);
		InputInventory->TestSetStimCount(1);
		InputPlayer->UseStimInput(FInputActionValue(true));
		TestEqual(TEXT("player use path consumes one stim"), InputInventory->GetStimCount(), 0);
		ConsumableInventoryTest::AdvanceTimers(World, 4.f);
		TestEqual(TEXT("player use path restores fifty health"), InputPlayer->GetHealthComponent()->GetCurrentHealth(), 75.f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConsumableInventoryNetworkContractTest,
	"Extraction.Inventory.Consumables.NetworkContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConsumableInventoryNetworkContractTest::RunTest(const FString& Parameters)
{
	const UConsumableInventoryComponent* DefaultInventory = GetDefault<UConsumableInventoryComponent>();
	TestTrue(TEXT("component replicates by default"), DefaultInventory->GetIsReplicated());

	const FProperty* StimCountProperty = UConsumableInventoryComponent::StaticClass()->FindPropertyByName(TEXT("StimCount"));
	TestNotNull(TEXT("stim count property exists"), StimCountProperty);
	if (StimCountProperty)
	{
		TestTrue(TEXT("stim count is replicated"), StimCountProperty->HasAnyPropertyFlags(CPF_Net));
		TestEqual(TEXT("stim count has rep-notify"), StimCountProperty->RepNotifyFunc, FName(TEXT("OnRep_StimCount")));
	}

	const UFunction* ServerUseFunction = UConsumableInventoryComponent::StaticClass()->FindFunctionByName(TEXT("ServerTryUseStim"));
	TestNotNull(TEXT("server use RPC exists"), ServerUseFunction);
	if (ServerUseFunction)
	{
		TestTrue(TEXT("use RPC is server-routed"), ServerUseFunction->HasAnyFunctionFlags(FUNC_Net | FUNC_NetServer));
		TestTrue(TEXT("use RPC is reliable"), ServerUseFunction->HasAnyFunctionFlags(FUNC_NetReliable));
	}

	// The cancel split: the authority-gated entry point is the only one Blueprint can reach. The
	// local teardown is C++-only precisely so a client BP can't unlock its gate mid-injection.
	TestNotNull(TEXT("authority cancel is Blueprint-exposed"),
		UConsumableInventoryComponent::StaticClass()->FindFunctionByName(TEXT("CancelStimUse")));
	TestTrue(TEXT("local cancel teardown is not Blueprint-exposed"),
		UConsumableInventoryComponent::StaticClass()->FindFunctionByName(TEXT("CancelStimUseLocally")) == nullptr);

	// The owning client drives its lockout off these three, not off replicated state.
	for (const TCHAR* ClientRpcName : { TEXT("ClientConfirmStimUsed"), TEXT("ClientCancelStimUse"), TEXT("ClientStimUseRefused") })
	{
		const UFunction* ClientFunction = UConsumableInventoryComponent::StaticClass()->FindFunctionByName(ClientRpcName);
		TestNotNull(*FString::Printf(TEXT("%s exists"), ClientRpcName), ClientFunction);
		if (!ClientFunction) continue;

		TestTrue(*FString::Printf(TEXT("%s is client-routed"), ClientRpcName),
			ClientFunction->HasAnyFunctionFlags(FUNC_Net | FUNC_NetClient));
		TestTrue(*FString::Printf(TEXT("%s is reliable"), ClientRpcName),
			ClientFunction->HasAnyFunctionFlags(FUNC_NetReliable));
	}
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
