// Automation coverage for the normalized player weapon presentation event contract.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/ExtractionPlayer.h"
#include "Components/WeaponComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#include "Weapon/WeaponBase.h"

namespace PlayerWeaponEventContractTest
{
	void SetWeaponData(AWeaponBase* Weapon, UWeaponDataAsset* WeaponData)
	{
		FObjectProperty* Property = FindFProperty<FObjectProperty>(Weapon->GetClass(), TEXT("WeaponData"));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(Weapon, WeaponData);
	}

	AWeaponBase* SpawnConfiguredWeapon(UWorld* World, const bool bShellByShell)
	{
		if (!IsValid(World)) return nullptr;

		AWeaponBase* Weapon = World->SpawnActorDeferred<AWeaponBase>(
			AWeaponBase::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!IsValid(Weapon)) return nullptr;

		UWeaponDataAsset* WeaponData = NewObject<UWeaponDataAsset>();
		WeaponData->MagazineSize = 2;
		WeaponData->DefaultReserveAmmo = 4;
		WeaponData->ReloadTime = 0.1f;
		WeaponData->bIsAutomatic = false;
		WeaponData->bShellByShellReload = bShellByShell;
		SetWeaponData(Weapon, WeaponData);
		Weapon->FinishSpawning(FTransform::Identity);
		if (!Weapon->HasActorBegunPlay())
			Weapon->DispatchBeginPlay();
		return Weapon;
	}

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
		FClassProperty* Property = FindFProperty<FClassProperty>(WeaponComponent->GetClass(), TEXT("DefaultWeaponClass"));
		check(Property);
		Property->SetPropertyValue_InContainer(WeaponComponent, AWeaponBase::StaticClass());
		return Player;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponReloadPhaseContractTest,
	"Extraction.PlayerWeapon.Events.ReloadPhases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponReloadPhaseContractTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	AWeaponBase* CompletedWeapon = PlayerWeaponEventContractTest::SpawnConfiguredWeapon(World, true);
	TestNotNull(TEXT("shell-reload weapon spawned"), CompletedWeapon);
	if (!CompletedWeapon) return false;

	TArray<EWeaponReloadPhase> CompletedPhases;
	CompletedPhases.Reserve(3);
	const FDelegateHandle CompletedHandle = CompletedWeapon->OnReloadPhaseChangedNative.AddLambda(
		[&CompletedPhases](const EWeaponReloadPhase Phase)
		{
			CompletedPhases.Add(Phase);
		});

	CompletedWeapon->StartFiring();
	CompletedWeapon->Reload();
	CompletedWeapon->HandleShellInserted();

	TestEqual(TEXT("accepted shell reload emits three phases"), CompletedPhases.Num(), 3);
	if (CompletedPhases.Num() == 3)
	{
		TestEqual(TEXT("reload starts first"), CompletedPhases[0], EWeaponReloadPhase::Started);
		TestEqual(TEXT("shell insertion is reported second"), CompletedPhases[1], EWeaponReloadPhase::ShellInserted);
		TestEqual(TEXT("reload completes last"), CompletedPhases[2], EWeaponReloadPhase::Completed);
	}

	AWeaponBase* CancelledWeapon = PlayerWeaponEventContractTest::SpawnConfiguredWeapon(World, false);
	TestNotNull(TEXT("cancellable reload weapon spawned"), CancelledWeapon);
	if (!CancelledWeapon)
	{
		CompletedWeapon->OnReloadPhaseChangedNative.Remove(CompletedHandle);
		return false;
	}

	TArray<EWeaponReloadPhase> CancelledPhases;
	CancelledPhases.Reserve(2);
	const FDelegateHandle CancelledHandle = CancelledWeapon->OnReloadPhaseChangedNative.AddLambda(
		[&CancelledPhases](const EWeaponReloadPhase Phase)
		{
			CancelledPhases.Add(Phase);
		});

	CancelledWeapon->StartFiring();
	CancelledWeapon->Reload();
	CancelledWeapon->CancelReload();

	TestEqual(TEXT("cancelled reload emits two phases"), CancelledPhases.Num(), 2);
	if (CancelledPhases.Num() == 2)
	{
		TestEqual(TEXT("cancelled reload starts"), CancelledPhases[0], EWeaponReloadPhase::Started);
		TestEqual(TEXT("cancelled reload interrupts"), CancelledPhases[1], EWeaponReloadPhase::Interrupted);
	}
	TestFalse(TEXT("cancelled reload never completes"), CancelledPhases.Contains(EWeaponReloadPhase::Completed));
	CompletedWeapon->OnReloadPhaseChangedNative.Remove(CompletedHandle);
	CancelledWeapon->OnReloadPhaseChangedNative.Remove(CancelledHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponComponentRelayContractTest,
	"Extraction.PlayerWeapon.Events.ComponentRelays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponComponentRelayContractTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	AExtractionPlayer* Player = PlayerWeaponEventContractTest::SpawnPlayerWithDefaultWeapon(World);
	TestNotNull(TEXT("player spawned deferred"), Player);
	if (!Player) return false;

	UWeaponComponent* WeaponComponent = Player->GetWeaponComponent();
	TestNotNull(TEXT("player owns weapon component"), WeaponComponent);
	if (!WeaponComponent) return false;

	int32 EquipEvents = 0;
	int32 AimEvents = 0;
	int32 TriggerEvents = 0;
	int32 ShotEvents = 0;
	int32 AmmoEvents = 0;
	int32 ReloadEvents = 0;
	AWeaponBase* EquippedFromEvent = nullptr;

	const FDelegateHandle EquipHandle = WeaponComponent->OnCurrentWeaponChangedNative.AddLambda(
		[&EquipEvents, &EquippedFromEvent](AWeaponBase* Weapon)
		{
			++EquipEvents;
			EquippedFromEvent = Weapon;
		});
	const FDelegateHandle AimHandle =
		WeaponComponent->OnAimingChangedNative.AddLambda([&AimEvents](bool) { ++AimEvents; });
	const FDelegateHandle TriggerHandle =
		WeaponComponent->OnTriggerChangedNative.AddLambda([&TriggerEvents](bool) { ++TriggerEvents; });
	const FDelegateHandle ShotHandle =
		WeaponComponent->OnWeaponShotNative.AddLambda([&ShotEvents]() { ++ShotEvents; });
	const FDelegateHandle AmmoHandle = WeaponComponent->OnWeaponAmmoChangedNative.AddLambda(
		[&AmmoEvents](int32, int32) { ++AmmoEvents; });
	const FDelegateHandle ReloadHandle = WeaponComponent->OnWeaponReloadPhaseChangedNative.AddLambda(
		[&ReloadEvents](EWeaponReloadPhase) { ++ReloadEvents; });
	const auto RemoveListeners = [WeaponComponent, EquipHandle, AimHandle, TriggerHandle, ShotHandle, AmmoHandle, ReloadHandle]()
	{
		WeaponComponent->OnCurrentWeaponChangedNative.Remove(EquipHandle);
		WeaponComponent->OnAimingChangedNative.Remove(AimHandle);
		WeaponComponent->OnTriggerChangedNative.Remove(TriggerHandle);
		WeaponComponent->OnWeaponShotNative.Remove(ShotHandle);
		WeaponComponent->OnWeaponAmmoChangedNative.Remove(AmmoHandle);
		WeaponComponent->OnWeaponReloadPhaseChangedNative.Remove(ReloadHandle);
	};

	Player->FinishSpawning(FTransform::Identity);
	if (!Player->HasActorBegunPlay())
		Player->DispatchBeginPlay();
	AWeaponBase* FirstWeapon = WeaponComponent->GetCurrentWeapon();
	TestNotNull(TEXT("default weapon equipped"), FirstWeapon);
	if (!FirstWeapon)
	{
		RemoveListeners();
		return false;
	}
	TestEqual(TEXT("default equip broadcasts once"), EquipEvents, 1);
	TestEqual(TEXT("equip event carries bound current weapon"), EquippedFromEvent, FirstWeapon);

	WeaponComponent->SetAiming(true);
	WeaponComponent->SetAiming(true);
	WeaponComponent->SetAiming(false);
	WeaponComponent->SetAiming(false);
	TestEqual(TEXT("aim emits only true and false edges"), AimEvents, 2);

	WeaponComponent->StartFire();
	WeaponComponent->StartFire();
	WeaponComponent->StopFire();
	WeaponComponent->StopFire();
	TestEqual(TEXT("trigger emits only press and release edges"), TriggerEvents, 2);

	UWeaponDataAsset* FirstData = NewObject<UWeaponDataAsset>();
	FirstData->MagazineSize = 2;
	FirstData->DefaultReserveAmmo = 4;
	FirstData->bIsAutomatic = false;
	PlayerWeaponEventContractTest::SetWeaponData(FirstWeapon, FirstData);
	FirstWeapon->InitializeAmmo();
	FirstWeapon->StartFiring();
	TestEqual(TEXT("current weapon ammo relays"), AmmoEvents, 2);
	TestEqual(TEXT("current weapon shot relays"), ShotEvents, 1);
	FirstWeapon->OnReloadPhaseChangedNative.Broadcast(EWeaponReloadPhase::Started);
	TestEqual(TEXT("current weapon reload phase relays"), ReloadEvents, 1);

	bool bDestroyedWeaponCallbackRan = false;
	const FDelegateHandle DestroyedWeaponHandle = World->AddOnActorDestroyedHandler(
		FOnActorDestroyed::FDelegate::CreateLambda(
			[FirstWeapon, &bDestroyedWeaponCallbackRan](AActor* DestroyedActor)
			{
				if (DestroyedActor != FirstWeapon) return;

				bDestroyedWeaponCallbackRan = true;
				AWeaponBase* DestroyedWeapon = CastChecked<AWeaponBase>(DestroyedActor);
				DestroyedWeapon->OnWeaponFired.Broadcast();
				DestroyedWeapon->OnAmmoChanged.Broadcast(1, 1);
				DestroyedWeapon->OnReloadPhaseChangedNative.Broadcast(EWeaponReloadPhase::Completed);
			}));

	WeaponComponent->EquipWeapon(AWeaponBase::StaticClass());
	World->RemoveOnActorDestroyedHandler(DestroyedWeaponHandle);
	AWeaponBase* SecondWeapon = WeaponComponent->GetCurrentWeapon();
	TestNotNull(TEXT("replacement weapon equipped"), SecondWeapon);
	if (!SecondWeapon)
	{
		RemoveListeners();
		return false;
	}
	TestNotEqual(TEXT("replacement is a new actor"), SecondWeapon, FirstWeapon);
	TestTrue(TEXT("replacement destroys the previous weapon"), bDestroyedWeaponCallbackRan);
	TestEqual(TEXT("replacement equip broadcasts once"), EquipEvents, 2);
	TestEqual(TEXT("replacement event carries current weapon"), EquippedFromEvent, SecondWeapon);

	TestEqual(TEXT("replaced weapon cannot relay shots"), ShotEvents, 1);
	TestEqual(TEXT("replaced weapon cannot relay ammo"), AmmoEvents, 2);
	TestEqual(TEXT("replaced weapon cannot relay reload phases"), ReloadEvents, 1);

	RemoveListeners();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
