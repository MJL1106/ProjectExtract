// Automation coverage for the player-owned weapon presentation bridge.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/ExtractionPlayer.h"
#include "Components/PlayerWeaponPresentationComponent.h"
#include "Components/WeaponComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#include "Weapon/WeaponBase.h"

namespace PlayerWeaponPresentationTest
{
	AExtractionPlayer* SpawnPlayerWithDefaultWeapon(UWorld* World)
	{
		if (!IsValid(World)) return nullptr;

		AExtractionPlayer* Player = World->SpawnActorDeferred<AExtractionPlayer>(
			AExtractionPlayer::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(Player)) return nullptr;

		UWeaponComponent* WeaponComponent = Player->GetWeaponComponent();
		FClassProperty* Property =
			FindFProperty<FClassProperty>(WeaponComponent->GetClass(), TEXT("DefaultWeaponClass"));
		check(Property);
		Property->SetPropertyValue_InContainer(WeaponComponent, AWeaponBase::StaticClass());
		return Player;
	}

	void FinishPlayerSpawn(AExtractionPlayer* Player)
	{
		if (!IsValid(Player)) return;

		Player->FinishSpawning(FTransform::Identity);
		if (!Player->HasActorBegunPlay())
			Player->DispatchBeginPlay();
	}

	APlayerController* PossessPlayerLocally(UWorld* World, AExtractionPlayer* Player)
	{
		if (!IsValid(World) || !IsValid(Player)) return nullptr;

		APlayerController* Controller = World->SpawnActor<APlayerController>();
		if (!IsValid(Controller) || !GEngine) return nullptr;

		ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
		Controller->SetPlayer(LocalPlayer);
		Controller->Possess(Player);
		if (UPlayerWeaponPresentationComponent* Presentation = Player->GetWeaponPresentationComponent())
			Presentation->RefreshPresentation();
		return Controller;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponPresentationPresenceAndCatchUpTest,
	"Extraction.PlayerWeapon.Presentation.PresenceAndCatchUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponPresentationPresenceAndCatchUpTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	AExtractionPlayer* Player = PlayerWeaponPresentationTest::SpawnPlayerWithDefaultWeapon(World);
	TestNotNull(TEXT("player spawned deferred"), Player);
	if (!Player) return false;

	UPlayerWeaponPresentationComponent* Presentation = Player->GetWeaponPresentationComponent();
	TestNotNull(TEXT("player owns presentation component"), Presentation);
	if (!Presentation) return false;

	TestEqual(
		TEXT("presentation getter matches component lookup"),
		Player->FindComponentByClass<UPlayerWeaponPresentationComponent>(),
		Presentation);
	TestFalse(TEXT("presentation component does not tick"), Presentation->PrimaryComponentTick.bCanEverTick);
	TestFalse(
		TEXT("native-owned presentation component cannot be added twice in Blueprint"),
		Presentation->GetClass()->HasMetaData(TEXT("BlueprintSpawnableComponent")));

	int32 WeaponEvents = 0;
	int32 ActiveEvents = 0;
	TArray<bool> ActiveStates;
	ActiveStates.Reserve(3);
	bool bCacheReadyAtEvent = true;
	const FDelegateHandle WeaponHandle = Presentation->OnPresentedWeaponChangedNative.AddLambda(
		[Presentation, &WeaponEvents, &bCacheReadyAtEvent](AWeaponBase* Weapon)
		{
			++WeaponEvents;
			bCacheReadyAtEvent &= Presentation->GetCurrentWeapon() == Weapon;
		});
	const FDelegateHandle ActiveHandle = Presentation->OnPresentationActiveChangedNative.AddLambda(
		[&ActiveEvents, &ActiveStates](bool bActive)
		{
			++ActiveEvents;
			ActiveStates.Add(bActive);
		});

	PlayerWeaponPresentationTest::FinishPlayerSpawn(Player);

	UWeaponComponent* WeaponComponent = Player->GetWeaponComponent();
	AWeaponBase* Weapon = WeaponComponent->GetCurrentWeapon();
	TestNotNull(TEXT("default weapon equipped"), Weapon);
	if (!Weapon)
	{
		Presentation->OnPresentedWeaponChangedNative.Remove(WeaponHandle);
		Presentation->OnPresentationActiveChangedNative.Remove(ActiveHandle);
		return false;
	}

	TestEqual(TEXT("non-local player caches without publishing"), WeaponEvents, 0);
	TestEqual(TEXT("non-local player never activates presentation"), ActiveEvents, 0);
	TestTrue(TEXT("weapon cache updates before presentation event"), bCacheReadyAtEvent);
	TestEqual(TEXT("presentation caches equipped weapon"), Presentation->GetCurrentWeapon(), Weapon);
	TestEqual(TEXT("presentation catches up current ammo"), Presentation->GetCurrentAmmo(), Weapon->GetCurrentAmmo());
	TestEqual(TEXT("presentation catches up reserve ammo"), Presentation->GetReserveAmmo(), Weapon->GetReserveAmmo());
	TestFalse(TEXT("no reload phase is fabricated during catch-up"), Presentation->HasObservedReloadPhase());

	APlayerController* Controller = PlayerWeaponPresentationTest::PossessPlayerLocally(World, Player);
	TestNotNull(TEXT("local controller possesses player"), Controller);
	if (!Controller)
	{
		Presentation->OnPresentedWeaponChangedNative.Remove(WeaponHandle);
		Presentation->OnPresentationActiveChangedNative.Remove(ActiveHandle);
		return false;
	}

	TestTrue(TEXT("possessed player is locally controlled"), Player->IsLocallyControlled());
	TestTrue(TEXT("possession activates presentation"), Presentation->IsPresentationActive());
	TestEqual(TEXT("possession emits one activation edge"), ActiveEvents, 1);
	TestEqual(TEXT("possession publishes one cached weapon"), WeaponEvents, 1);

	WeaponComponent->OnCurrentWeaponChangedNative.Broadcast(Weapon);
	TestEqual(TEXT("duplicate equip event is idempotent"), WeaponEvents, 1);

	Controller->UnPossess();
	Presentation->RefreshPresentation();
	TestFalse(TEXT("unpossess deactivates presentation"), Presentation->IsPresentationActive());
	TestEqual(TEXT("unpossess emits one deactivation edge"), ActiveEvents, 2);
	WeaponComponent->EquipWeapon(AWeaponBase::StaticClass());
	AWeaponBase* ReplacementWeapon = WeaponComponent->GetCurrentWeapon();
	TestNotNull(TEXT("replacement weapon equipped while unpossessed"), ReplacementWeapon);
	TestEqual(TEXT("unpossessed replacement stays presentation-silent"), WeaponEvents, 1);
	TestEqual(TEXT("unpossessed replacement still updates cache"), Presentation->GetCurrentWeapon(), ReplacementWeapon);

	Controller->Possess(Player);
	Presentation->RefreshPresentation();
	TestTrue(TEXT("re-possession reactivates presentation"), Presentation->IsPresentationActive());
	TestEqual(TEXT("re-possession emits one activation edge"), ActiveEvents, 3);
	TestEqual(TEXT("first active edge is true"), ActiveStates[0], true);
	TestEqual(TEXT("second active edge is false"), ActiveStates[1], false);
	TestEqual(TEXT("third active edge is true"), ActiveStates[2], true);
	TestEqual(TEXT("re-possession republishes cached replacement"), WeaponEvents, 2);

	Presentation->OnPresentedWeaponChangedNative.Remove(WeaponHandle);
	Presentation->OnPresentationActiveChangedNative.Remove(ActiveHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponPresentationRelayAndCleanupTest,
	"Extraction.PlayerWeapon.Presentation.RelaysAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponPresentationRelayAndCleanupTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	AExtractionPlayer* Player = PlayerWeaponPresentationTest::SpawnPlayerWithDefaultWeapon(World);
	TestNotNull(TEXT("player spawned deferred"), Player);
	if (!Player) return false;

	PlayerWeaponPresentationTest::FinishPlayerSpawn(Player);
	UWeaponComponent* WeaponComponent = Player->GetWeaponComponent();
	UPlayerWeaponPresentationComponent* Presentation = Player->GetWeaponPresentationComponent();
	TestNotNull(TEXT("weapon component exists"), WeaponComponent);
	TestNotNull(TEXT("presentation component exists"), Presentation);
	if (!WeaponComponent || !Presentation) return false;

	APlayerController* Controller = PlayerWeaponPresentationTest::PossessPlayerLocally(World, Player);
	TestNotNull(TEXT("local controller possesses player"), Controller);
	if (!Controller) return false;

	int32 WeaponEvents = 0;
	int32 AimEvents = 0;
	int32 TriggerEvents = 0;
	int32 ShotEvents = 0;
	int32 AmmoEvents = 0;
	int32 ReloadEvents = 0;
	bool bCachesReadyAtEvents = true;
	bool bTrackEquipWhileAimingOrder = false;
	TArray<FName> EquipWhileAimingOrder;
	EquipWhileAimingOrder.Reserve(2);

	const FDelegateHandle WeaponHandle = Presentation->OnPresentedWeaponChangedNative.AddLambda(
		[Presentation, &WeaponEvents, &bCachesReadyAtEvents, &bTrackEquipWhileAimingOrder, &EquipWhileAimingOrder](
			AWeaponBase* Weapon)
		{
			++WeaponEvents;
			bCachesReadyAtEvents &= Presentation->GetCurrentWeapon() == Weapon;
			if (bTrackEquipWhileAimingOrder)
				EquipWhileAimingOrder.Add(TEXT("Weapon"));
		});
	const FDelegateHandle AimHandle = Presentation->OnPresentedAimingChangedNative.AddLambda(
		[Presentation, &AimEvents, &bCachesReadyAtEvents, &bTrackEquipWhileAimingOrder, &EquipWhileAimingOrder](
			bool bAiming)
		{
			++AimEvents;
			bCachesReadyAtEvents &= Presentation->IsAiming() == bAiming;
			if (bTrackEquipWhileAimingOrder)
				EquipWhileAimingOrder.Add(TEXT("Aim"));
		});
	const FDelegateHandle TriggerHandle = Presentation->OnPresentedTriggerChangedNative.AddLambda(
		[Presentation, &TriggerEvents, &bCachesReadyAtEvents](bool bHeld)
		{
			++TriggerEvents;
			bCachesReadyAtEvents &= Presentation->IsTriggerHeld() == bHeld;
		});
	const FDelegateHandle ShotHandle =
		Presentation->OnPresentedShotNative.AddLambda([&ShotEvents]() { ++ShotEvents; });
	const FDelegateHandle AmmoHandle = Presentation->OnPresentedAmmoChangedNative.AddLambda(
		[Presentation, &AmmoEvents, &bCachesReadyAtEvents](int32 CurrentAmmo, int32 ReserveAmmo)
		{
			++AmmoEvents;
			bCachesReadyAtEvents &= Presentation->GetCurrentAmmo() == CurrentAmmo;
			bCachesReadyAtEvents &= Presentation->GetReserveAmmo() == ReserveAmmo;
		});
	const FDelegateHandle ReloadHandle = Presentation->OnPresentedReloadPhaseChangedNative.AddLambda(
		[Presentation, &ReloadEvents, &bCachesReadyAtEvents](EWeaponReloadPhase Phase)
		{
			++ReloadEvents;
			bCachesReadyAtEvents &= Presentation->GetLastReloadPhase() == Phase;
		});

	WeaponComponent->OnAimingChangedNative.Broadcast(true);
	WeaponComponent->OnAimingChangedNative.Broadcast(true);
	WeaponComponent->OnAimingChangedNative.Broadcast(false);
	WeaponComponent->OnAimingChangedNative.Broadcast(false);
	TestEqual(TEXT("duplicate aim edges are suppressed"), AimEvents, 2);

	WeaponComponent->SetAiming(true);
	TestEqual(TEXT("real aim edge reaches presentation"), AimEvents, 3);
	bTrackEquipWhileAimingOrder = true;
	WeaponComponent->EquipWeapon(AWeaponBase::StaticClass());
	bTrackEquipWhileAimingOrder = false;
	TestEqual(TEXT("equip while aiming publishes weapon once"), WeaponEvents, 1);
	TestEqual(TEXT("equip while aiming reapplies aim once"), AimEvents, 4);
	TestEqual(TEXT("equip while aiming publishes two ordered events"), EquipWhileAimingOrder.Num(), 2);
	if (EquipWhileAimingOrder.Num() == 2)
	{
		TestEqual(TEXT("new weapon publishes before aim reapply"), EquipWhileAimingOrder[0], FName(TEXT("Weapon")));
		TestEqual(TEXT("aim reapplies after new weapon"), EquipWhileAimingOrder[1], FName(TEXT("Aim")));
	}

	WeaponComponent->OnTriggerChangedNative.Broadcast(true);
	WeaponComponent->OnTriggerChangedNative.Broadcast(true);
	WeaponComponent->OnTriggerChangedNative.Broadcast(false);
	WeaponComponent->OnTriggerChangedNative.Broadcast(false);
	TestEqual(TEXT("duplicate trigger edges are suppressed"), TriggerEvents, 2);

	WeaponComponent->OnWeaponShotNative.Broadcast();
	TestEqual(TEXT("accepted shot relays once"), ShotEvents, 1);

	WeaponComponent->OnWeaponAmmoChangedNative.Broadcast(3, 7);
	WeaponComponent->OnWeaponAmmoChangedNative.Broadcast(3, 7);
	TestEqual(TEXT("duplicate ammo state is suppressed"), AmmoEvents, 1);

	WeaponComponent->OnWeaponReloadPhaseChangedNative.Broadcast(EWeaponReloadPhase::Started);
	WeaponComponent->OnWeaponReloadPhaseChangedNative.Broadcast(EWeaponReloadPhase::ShellInserted);
	WeaponComponent->OnWeaponReloadPhaseChangedNative.Broadcast(EWeaponReloadPhase::ShellInserted);
	WeaponComponent->OnWeaponReloadPhaseChangedNative.Broadcast(EWeaponReloadPhase::Completed);
	TestEqual(TEXT("repeated shell insert phases remain observable"), ReloadEvents, 4);
	TestTrue(TEXT("reload phase becomes observed"), Presentation->HasObservedReloadPhase());
	TestTrue(TEXT("all caches update before presentation events"), bCachesReadyAtEvents);

	TestTrue(
		TEXT("weapon source is bound"),
		WeaponComponent->OnCurrentWeaponChangedNative.IsBoundToObject(Presentation));
	TestTrue(
		TEXT("aim source is bound"),
		WeaponComponent->OnAimingChangedNative.IsBoundToObject(Presentation));
	TestTrue(
		TEXT("trigger source is bound"),
		WeaponComponent->OnTriggerChangedNative.IsBoundToObject(Presentation));
	TestTrue(
		TEXT("shot source is bound"),
		WeaponComponent->OnWeaponShotNative.IsBoundToObject(Presentation));
	TestTrue(
		TEXT("ammo source is bound"),
		WeaponComponent->OnWeaponAmmoChangedNative.IsBoundToObject(Presentation));
	TestTrue(
		TEXT("reload source is bound"),
		WeaponComponent->OnWeaponReloadPhaseChangedNative.IsBoundToObject(Presentation));

	Presentation->OnPresentedWeaponChangedNative.Remove(WeaponHandle);
	Presentation->OnPresentedAimingChangedNative.Remove(AimHandle);
	Presentation->OnPresentedTriggerChangedNative.Remove(TriggerHandle);
	Presentation->OnPresentedShotNative.Remove(ShotHandle);
	Presentation->OnPresentedAmmoChangedNative.Remove(AmmoHandle);
	Presentation->OnPresentedReloadPhaseChangedNative.Remove(ReloadHandle);
	Presentation->DestroyComponent();

	TestFalse(
		TEXT("weapon source unbound on teardown"),
		WeaponComponent->OnCurrentWeaponChangedNative.IsBoundToObject(Presentation));
	TestFalse(
		TEXT("aim source unbound on teardown"),
		WeaponComponent->OnAimingChangedNative.IsBoundToObject(Presentation));
	TestFalse(
		TEXT("trigger source unbound on teardown"),
		WeaponComponent->OnTriggerChangedNative.IsBoundToObject(Presentation));
	TestFalse(
		TEXT("shot source unbound on teardown"),
		WeaponComponent->OnWeaponShotNative.IsBoundToObject(Presentation));
	TestFalse(
		TEXT("ammo source unbound on teardown"),
		WeaponComponent->OnWeaponAmmoChangedNative.IsBoundToObject(Presentation));
	TestFalse(
		TEXT("reload source unbound on teardown"),
		WeaponComponent->OnWeaponReloadPhaseChangedNative.IsBoundToObject(Presentation));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponPresentationAmmoReplicationContractTest,
	"Extraction.PlayerWeapon.Presentation.AmmoReplicationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponPresentationAmmoReplicationContractTest::RunTest(const FString& Parameters)
{
	const FProperty* ReserveAmmoProperty =
		FindFProperty<FProperty>(AWeaponBase::StaticClass(), TEXT("ReserveAmmo"));
	TestNotNull(TEXT("reserve ammo property exists"), ReserveAmmoProperty);
	if (!ReserveAmmoProperty) return false;

	TestTrue(
		TEXT("reserve-only changes have a replication notify"),
		ReserveAmmoProperty->HasAnyPropertyFlags(CPF_RepNotify));
	TestEqual(
		TEXT("reserve replication uses the ammo presentation notify"),
		ReserveAmmoProperty->RepNotifyFunc,
		FName(TEXT("OnRep_ReserveAmmo")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
