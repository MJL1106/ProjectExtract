// Automation coverage for authoritative stim inventory limits and player use behavior.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/ExtractionPlayer.h"
#include "Components/ConsumableInventoryComponent.h"
#include "Components/HealthComponent.h"
#include "InputActionValue.h"
#include "Tests/AutomationEditorCommon.h"
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

	TestEqual(TEXT("initial stim count is zero"), Inventory->GetStimCount(), 0);
	TestEqual(TEXT("partial grant reports amount added"), Inventory->AddStims(2), 2);
	TestEqual(TEXT("partial grant is stored"), Inventory->GetStimCount(), 2);
	TestEqual(TEXT("grant clamps to capacity"), Inventory->AddStims(5), 1);
	TestEqual(TEXT("capacity is exactly three"), Inventory->GetStimCount(), 3);
	TestEqual(TEXT("full inventory rejects grant"), Inventory->AddStims(1), 0);

	Health->InitializeHealth(100.f, 25.f);
	const float FullHealthShield = Health->GetCurrentShield();
	TestFalse(TEXT("full-health use is rejected"), Inventory->TryUseStim());
	TestEqual(TEXT("full-health rejection consumes nothing"), Inventory->GetStimCount(), 3);
	TestEqual(TEXT("full-health rejection does not change shield"), Health->GetCurrentShield(), FullHealthShield);

	ConsumableInventoryTest::SetDamagedHealth(Player, 75.f);
	const float DamagedShield = Health->GetCurrentShield();
	TestTrue(TEXT("damaged player can use stim"), Inventory->TryUseStim());
	TestEqual(TEXT("stim restores exactly fifty health"), Health->GetCurrentHealth(), 75.f);
	TestEqual(TEXT("stim does not restore shield"), Health->GetCurrentShield(), DamagedShield);
	TestEqual(TEXT("successful use consumes one"), Inventory->GetStimCount(), 2);

	ConsumableInventoryTest::SetDamagedHealth(Player, 10.f);
	TestTrue(TEXT("healing clamps at max health"), Inventory->TryUseStim());
	TestEqual(TEXT("clamped heal reaches max health"), Health->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("clamped heal still consumes one"), Inventory->GetStimCount(), 1);

	Health->Die();
	TestFalse(TEXT("dead player cannot use a stim"), Inventory->TryUseStim());
	TestEqual(TEXT("dead rejection consumes nothing"), Inventory->GetStimCount(), 1);

	AExtractionPlayer* DBNOPlayer = ConsumableInventoryTest::SpawnPlayer(World);
	TestNotNull(TEXT("DBNO test player spawned"), DBNOPlayer);
	if (DBNOPlayer)
	{
		DBNOPlayer->DispatchBeginPlay();
		UConsumableInventoryComponent* DBNOInventory = DBNOPlayer->GetConsumableInventoryComponent();
		UHealthComponent* DBNOHealth = DBNOPlayer->GetHealthComponent();
		DBNOInventory->AddStims(1);
		DBNOHealth->Die();
		TestTrue(TEXT("death path enters DBNO"), DBNOPlayer->GetIsDBNO());
		TestFalse(TEXT("DBNO player cannot use a stim"), DBNOInventory->TryUseStim());
		TestEqual(TEXT("DBNO rejection consumes nothing"), DBNOInventory->GetStimCount(), 1);
	}

	AExtractionPlayer* InputPlayer = ConsumableInventoryTest::SpawnPlayer(World);
	TestNotNull(TEXT("input-path test player spawned"), InputPlayer);
	if (InputPlayer)
	{
		ConsumableInventoryTest::SetDamagedHealth(InputPlayer, 75.f);
		UConsumableInventoryComponent* InputInventory = InputPlayer->GetConsumableInventoryComponent();
		InputInventory->AddStims(1);
		InputPlayer->UseStimInput(FInputActionValue(true));
		TestEqual(TEXT("player use path consumes one stim"), InputInventory->GetStimCount(), 0);
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
	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
