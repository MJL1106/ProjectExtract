// Automation coverage for the player-owned weapon presentation bridge.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/ExtractionPlayer.h"
#include "Components/PlayerWeaponPresentationComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WeaponComponent.h"
#include "Data/PlayerWeaponAttachmentDefinition.h"
#include "Data/PlayerWeaponPresentationProfile.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Movement/TraversalComponent.h"
#include "ReferenceSkeleton.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#include "Weapon/KitWeaponInterface.h"
#include "Weapon/WeaponBase.h"
#include "Weapon/PlayerWeaponView.h"

namespace PlayerWeaponPresentationTest
{
	bool InstallHandSocket(AExtractionPlayer& Player)
	{
		USkeletalMeshComponent* Mesh = Player.GetMesh();
		if (!IsValid(Mesh)) return false;

		USkeletalMesh* TestMesh =
			NewObject<USkeletalMesh>(&Player, NAME_None, RF_Transient);
		if (!IsValid(TestMesh)) return false;

		static const FName RootBone(TEXT("root"));
		{
			FReferenceSkeletonModifier Modifier(
				TestMesh->GetRefSkeleton(), nullptr);
			Modifier.Add(
				FMeshBoneInfo(
					RootBone, RootBone.ToString(), INDEX_NONE),
				FTransform::Identity);
		}
		TestMesh->CalculateInvRefMatrices();

		static const FName HandSocketName(TEXT("ik_hand_gun"));
		USkeletalMeshSocket* HandSocket =
			NewObject<USkeletalMeshSocket>(
				TestMesh, NAME_None, RF_Transient);
		HandSocket->SocketName = HandSocketName;
		HandSocket->BoneName = RootBone;
		TestMesh->GetMeshOnlySocketList().Add(HandSocket);
		TestMesh->RebuildSocketMap();
		Mesh->SetSkeletalMeshAsset(TestMesh);
		return Mesh->DoesSocketExist(HandSocketName);
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
		FClassProperty* Property =
			FindFProperty<FClassProperty>(WeaponComponent->GetClass(), TEXT("DefaultWeaponClass"));
		check(Property);
		Property->SetPropertyValue_InContainer(WeaponComponent, AWeaponBase::StaticClass());
		return Player;
	}

	bool FinishPlayerSpawn(AExtractionPlayer* Player)
	{
		if (!IsValid(Player)) return false;

		Player->FinishSpawning(FTransform::Identity);
		if (!Player->HasActorBegunPlay())
			Player->DispatchBeginPlay();
		if (UWeaponComponent* Weapons = Player->GetWeaponComponent())
			if (AWeaponBase* Weapon = Weapons->GetCurrentWeapon())
				if (!Weapon->HasActorBegunPlay())
					Weapon->DispatchBeginPlay();
		return InstallHandSocket(*Player);
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

	UWeaponDataAsset* CreateProfileData(UObject& Outer)
	{
		UWeaponDataAsset* Data = NewObject<UWeaponDataAsset>(&Outer);
		UPlayerWeaponPresentationProfile* Profile =
			NewObject<UPlayerWeaponPresentationProfile>(Data);
		if (!Data || !Profile) return nullptr;

		Profile->ProfileId = TEXT("profile.test.lifecycle");
		Profile->ViewClass = APlayerWeaponView::StaticClass();
		Data->PlayerPresentationProfile = Profile;
		Data->KitVisualWeaponClass = AActor::StaticClass();
		return Data;
	}

	UWeaponDataAsset* ConfigureProfileWeapon(AWeaponBase& Weapon)
	{
		UWeaponDataAsset* Data = CreateProfileData(Weapon);
		FObjectPropertyBase* Property =
			FindFProperty<FObjectPropertyBase>(
				Weapon.GetClass(), TEXT("WeaponData"));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(&Weapon, Data);
		return Data;
	}

	AWeaponBase* EquipProfileWeapon(
		UWeaponComponent& Component,
		TSubclassOf<AActor> ThirdPersonVisualClass = nullptr,
		bool* bOutDeferredHookSawNoCurrentWeapon = nullptr)
	{
		if (bOutDeferredHookSawNoCurrentWeapon)
			*bOutDeferredHookSawNoCurrentWeapon = false;
		UWeaponDataAsset* ConfiguredData = nullptr;
		Component.SetPreFinishWeaponSpawnHookForTesting(
			[&Component, &ConfiguredData, ThirdPersonVisualClass,
				bOutDeferredHookSawNoCurrentWeapon](AWeaponBase& Weapon)
			{
				if (bOutDeferredHookSawNoCurrentWeapon)
					*bOutDeferredHookSawNoCurrentWeapon =
						Component.GetCurrentWeapon() == nullptr;
				ConfiguredData = ConfigureProfileWeapon(Weapon);
				FObjectPropertyBase* VisualProperty =
					FindFProperty<FObjectPropertyBase>(
						Weapon.GetClass(),
						TEXT("ThirdPersonVisualActorClass"));
				check(VisualProperty);
				VisualProperty->SetObjectPropertyValue_InContainer(
					&Weapon, ThirdPersonVisualClass.Get());
			});

		Component.EquipWeapon(AWeaponBase::StaticClass());
		AWeaponBase* Equipped = Component.GetCurrentWeapon();
		if (IsValid(Equipped) && !Equipped->HasActorBegunPlay())
			Equipped->DispatchBeginPlay();
		const bool bCopiedProfile =
			IsValid(Equipped)
			&& Equipped->GetWeaponData() == ConfiguredData;
		Component.SetPreFinishWeaponSpawnHookForTesting({});
		return bCopiedProfile ? Equipped : nullptr;
	}

	AActor* GetThirdPersonVisual(AWeaponBase& Weapon)
	{
		FObjectProperty* Property = FindFProperty<FObjectProperty>(
			Weapon.GetClass(), TEXT("SpawnedVisualActor"));
		check(Property);
		return Cast<AActor>(
			Property->GetObjectPropertyValue_InContainer(&Weapon));
	}

	int32 CountLiveWeaponViews(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<APlayerWeaponView> It(&World); It; ++It)
			if (IsValid(*It) && !It->IsActorBeingDestroyed()) ++Count;
		return Count;
	}

	int32 CountLiveAttachmentViews(UWorld& World)
	{
		int32 Count = 0;
		for (TActorIterator<APlayerWeaponAttachmentView> It(&World); It; ++It)
			if (IsValid(*It) && !It->IsActorBeingDestroyed()) ++Count;
		return Count;
	}

	bool ResolvedSightsMatch(
		const FPlayerWeaponResolvedSight& A,
		const FPlayerWeaponResolvedSight& B)
	{
		return A.bIsValid == B.bIsValid
			&& A.bUsesOptic == B.bUsesOptic
			&& A.OpticId == B.OpticId
			&& A.AimSourceInHandSpace.Equals(B.AimSourceInHandSpace)
			&& FMath::IsNearlyEqual(
				A.ADSSettings.FieldOfView, B.ADSSettings.FieldOfView)
			&& FMath::IsNearlyEqual(
				A.ADSSettings.TransitionTime, B.ADSSettings.TransitionTime)
			&& FMath::IsNearlyEqual(
				A.ADSSettings.SensitivityMultiplier,
				B.ADSSettings.SensitivityMultiplier)
			&& FMath::IsNearlyEqual(
				A.ADSSettings.AimDistanceFromCameraCm,
				B.ADSSettings.AimDistanceFromCameraCm)
			&& FMath::IsNearlyEqual(
				A.ADSSettings.EyeReliefCm, B.ADSSettings.EyeReliefCm);
	}

	UWeaponDataAsset* ConfigureOpticProfileWeapon(AWeaponBase& Weapon)
	{
		UWeaponDataAsset* Data = ConfigureProfileWeapon(Weapon);
		UPlayerWeaponPresentationProfile* Profile =
			IsValid(Data) ? Data->PlayerPresentationProfile.Get() : nullptr;
		if (!IsValid(Data) || !IsValid(Profile)) return nullptr;

		Data->WeaponType = EWeaponType::Rifle;
		Profile->WeaponType = EWeaponType::Rifle;
		Profile->ADSDefaults.FieldOfView = 70.f;
		Profile->ADSDefaults.TransitionTime = 0.22f;
		Profile->ADSDefaults.SensitivityMultiplier = 0.8f;
		Profile->ADSDefaults.AimDistanceFromCameraCm = 47.f;
		Profile->ADSDefaults.EyeReliefCm = 4.f;
		Profile->MarkerRequirements.bRequireWeaponSeat = true;
		Profile->MarkerRequirements.bRequireIronRear = true;
		Profile->MarkerRequirements.bRequireIronFront = true;
		Profile->MarkerRequirements.bRequireOpticMount = true;
		Profile->DefaultOpticId = TEXT("optic.a");

		Profile->CompatibleAttachments.Reset();
		Profile->CompatibleAttachments.Reserve(4);
		auto AddDefinition = [Profile](
			FName Id,
			EPlayerWeaponAttachmentSlot Slot,
			EWeaponType CompatibleType)
		{
			UPlayerWeaponAttachmentDefinition* Definition =
				NewObject<UPlayerWeaponAttachmentDefinition>(Profile);
			check(Definition);
			Definition->AttachmentId = Id;
			Definition->Slot = Slot;
			Definition->ViewClass =
				APlayerWeaponAttachmentView::StaticClass();
			Definition->CompatibleWeaponTypes.Add(CompatibleType);
			Profile->CompatibleAttachments.Add(Definition);
			return Definition;
		};

		UPlayerWeaponAttachmentDefinition* OpticA = AddDefinition(
			TEXT("optic.a"),
			EPlayerWeaponAttachmentSlot::Optic,
			EWeaponType::Rifle);
		OpticA->OpticOverride.bOverrideFieldOfView = true;
		OpticA->OpticOverride.FieldOfView = 48.f;
		OpticA->OpticOverride.bOverrideAimDistance = true;
		OpticA->OpticOverride.AimDistanceFromCameraCm = 51.f;

		UPlayerWeaponAttachmentDefinition* OpticB = AddDefinition(
			TEXT("optic.b"),
			EPlayerWeaponAttachmentSlot::Optic,
			EWeaponType::Rifle);
		OpticB->OpticOverride.bOverrideFieldOfView = true;
		OpticB->OpticOverride.FieldOfView = 58.f;
		OpticB->OpticOverride.bOverrideAimDistance = true;
		OpticB->OpticOverride.AimDistanceFromCameraCm = 63.f;

		AddDefinition(
			TEXT("optic.incompatible"),
			EPlayerWeaponAttachmentSlot::Optic,
			EWeaponType::Pistol);
		AddDefinition(
			TEXT("grip.wrong-slot"),
			EPlayerWeaponAttachmentSlot::UnderbarrelGrip,
			EWeaponType::Rifle);
		return Data;
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

	if (!TestTrue(TEXT("player finishes with a valid hand socket"),
		PlayerWeaponPresentationTest::FinishPlayerSpawn(Player)))
		return false;

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

	if (!TestTrue(TEXT("player finishes with a valid hand socket"),
		PlayerWeaponPresentationTest::FinishPlayerSpawn(Player)))
		return false;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponProfileViewLifecycleTest,
	"Extraction.PlayerWeapon.Presentation.ProfileViewLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponProfileViewLifecycleTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(
		TEXT("shipping without KitWeaponPoseAsset"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		0);
	AddExpectedMessagePlain(
		TEXT("WeaponVisualMeshName 'WeaponMesh' not found"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains);
	AddExpectedMessagePlain(
		TEXT("cannot seat a player weapon view: socket ik_hand_gun is missing"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains);
	AddExpectedMessagePlain(
		TEXT("has an invalid required WeaponSeat marker"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		0);

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull(TEXT("automation world exists"), World);
	if (!World) return false;
	AExtractionPlayer* Player =
		PlayerWeaponPresentationTest::SpawnPlayerWithDefaultWeapon(World);
	TestNotNull(TEXT("player spawned deferred"), Player);
	if (!Player) return false;
	if (!TestTrue(TEXT("player finishes with a valid hand socket"),
		PlayerWeaponPresentationTest::FinishPlayerSpawn(Player)))
		return false;

	UWeaponComponent* Weapons = Player->GetWeaponComponent();
	UPlayerWeaponPresentationComponent* Presentation =
		Player->GetWeaponPresentationComponent();
	AWeaponBase* FirstWeapon = Weapons ? Weapons->GetCurrentWeapon() : nullptr;
	TestNotNull(TEXT("default weapon exists"), FirstWeapon);
	TestNotNull(TEXT("presentation exists"), Presentation);
	if (!FirstWeapon || !Presentation) return false;
	TestNotNull(TEXT("profile data is assigned"),
		PlayerWeaponPresentationTest::ConfigureProfileWeapon(*FirstWeapon));
	TestNull(TEXT("non-local profile does not spawn a view"),
		Presentation->GetActiveWeaponView());
	TestEqual(TEXT("non-local world has no passive views"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 0);

	APlayerController* Controller =
		PlayerWeaponPresentationTest::PossessPlayerLocally(World, Player);
	TestNotNull(TEXT("local controller possesses player"), Controller);
	if (!Controller) return false;

	APlayerWeaponView* FirstView = Presentation->GetActiveWeaponView();
	TestNotNull(TEXT("profile spawns one passive view"), FirstView);
	if (!FirstView) return false;
	FObjectPropertyBase* RetainedViewClassProperty =
		FindFProperty<FObjectPropertyBase>(
			Presentation->GetClass(), TEXT("CachedLoadedViewClass"));
	TestNotNull(TEXT("loaded view class uses a GC-visible cache"),
		RetainedViewClassProperty);
	if (RetainedViewClassProperty)
	{
		TestEqual(TEXT("cache retains the validated current view class"),
			Cast<UClass>(RetainedViewClassProperty->GetObjectPropertyValue_InContainer(
				Presentation)),
			FirstView->GetClass());
	}
	TestEqual(TEXT("one-view invariant holds"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 1);
	TestEqual(TEXT("local player owns the view"),
		FirstView->GetOwner(), static_cast<AActor*>(Player));
	TestFalse(TEXT("local presentation view does not replicate"),
		FirstView->GetIsReplicated());
	TestEqual(TEXT("view is attached to player mesh"),
		FirstView->GetAttachParentActor(), static_cast<AActor*>(Player));
	TestEqual(TEXT("view uses procedural gun socket"),
		FirstView->GetRootComponent()->GetAttachSocketName(), FName(TEXT("ik_hand_gun")));
	TestEqual(TEXT("visible muzzle is registered"),
		FirstWeapon->GetFirstPersonMuzzle(),
		static_cast<USceneComponent*>(FirstView->GetMuzzleMarker()));
	TestTrue(TEXT("seat placement is cached"), Presentation->HasCachedViewPlacement());
	TestNull(TEXT("profile suppresses legacy visual class"),
		IKitWeaponInterface::Execute_GetKitVisualWeaponClass(FirstWeapon));

	Weapons->OnCurrentWeaponChangedNative.Broadcast(FirstWeapon);
	TestEqual(TEXT("duplicate equip retains the same view"),
		Presentation->GetActiveWeaponView(), FirstView);
	TestEqual(TEXT("duplicate equip cannot create a second view"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 1);

	Presentation->SetWeaponViewHidden(true);
	TestTrue(TEXT("view can be hidden for blocked states"), FirstView->IsHidden());
	AWeaponBase* EventWeapon = nullptr;
	bool bReplacementReadyAtEvent = false;
	const FDelegateHandle ReplacementHandle =
		Presentation->OnPresentedWeaponChangedNative.AddLambda(
			[Presentation, &EventWeapon, &bReplacementReadyAtEvent](AWeaponBase* Weapon)
			{
				EventWeapon = Weapon;
				APlayerWeaponView* View = Presentation->GetActiveWeaponView();
				bReplacementReadyAtEvent = IsValid(Weapon) && IsValid(View)
					&& Weapon->GetFirstPersonMuzzle() == View->GetMuzzleMarker();
			});
	bool bDeferredHookSawNoCurrentWeapon = false;
	AWeaponBase* Replacement =
		PlayerWeaponPresentationTest::EquipProfileWeapon(
			*Weapons, ACharacter::StaticClass(),
			&bDeferredHookSawNoCurrentWeapon);
	Presentation->OnPresentedWeaponChangedNative.Remove(ReplacementHandle);
	TestNotNull(TEXT("production replacement profile weapon spawns"), Replacement);
	if (!Replacement) return false;
	APlayerWeaponView* ReplacementView = Presentation->GetActiveWeaponView();
	TestNotNull(TEXT("replacement gets a view"), ReplacementView);
	TestEqual(TEXT("production equip publishes the replacement"),
		EventWeapon, Replacement);
	TestTrue(TEXT("replacement view is ready before weapon event"),
		bReplacementReadyAtEvent);
	TestTrue(TEXT("deferred test hook observes production assignment order"),
		bDeferredHookSawNoCurrentWeapon);
	TestNotEqual(TEXT("replacement destroys the old view"), ReplacementView, FirstView);
	TestTrue(TEXT("production equip destroys the old weapon"),
		FirstWeapon->IsActorBeingDestroyed());
	TestTrue(TEXT("replacement inherits hidden state"),
		ReplacementView && ReplacementView->IsHidden());
	TestNull(TEXT("old weapon muzzle is cleared"), FirstWeapon->GetFirstPersonMuzzle());
	TestEqual(TEXT("replacement muzzle is registered"),
		Replacement->GetFirstPersonMuzzle(),
		ReplacementView
			? static_cast<USceneComponent*>(ReplacementView->GetMuzzleMarker())
			: nullptr);
	TestEqual(TEXT("replacement preserves one-view invariant"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 1);
	AActor* ThirdPersonVisual =
		PlayerWeaponPresentationTest::GetThirdPersonVisual(*Replacement);
	TestNotNull(TEXT("configured third-person visual spawns"), ThirdPersonVisual);
	if (ThirdPersonVisual)
	{
		TInlineComponentArray<UPrimitiveComponent*> Primitives;
		ThirdPersonVisual->GetComponents(Primitives);
		TestTrue(TEXT("third-person visual has render primitives"),
			Primitives.Num() > 0);
		for (const UPrimitiveComponent* Primitive : Primitives)
			if (IsValid(Primitive))
				TestTrue(TEXT("third-person primitive is hidden from its owner"),
					Primitive->bOwnerNoSee);
	}
	Presentation->SetWeaponViewHidden(false);
	UTraversalComponent* Traversal = Player->GetTraversalComponent();
	TestNotNull(TEXT("traversal visibility source exists"), Traversal);
	if (!Traversal) return false;
	Traversal->OnTraversalStarted.Broadcast(
		ETraversalType::Vault, 1.f, FVector::ZeroVector, FVector::ZeroVector);
	Player->OnDBNOStateChanged.Broadcast(true, 30.f);
	Traversal->OnTraversalEnded.Broadcast();
	TestTrue(TEXT("overlapping DBNO suppression keeps view hidden"),
		ReplacementView && ReplacementView->IsHidden());
	Player->OnDBNOStateChanged.Broadcast(false, 0.f);
	TestFalse(TEXT("view restores after final suppression clears"),
		ReplacementView && ReplacementView->IsHidden());
	Player->SetBeingRevived(true, 1.f);
	TestTrue(TEXT("being revived hides profile view"),
		ReplacementView && ReplacementView->IsHidden());
	Player->SetBeingRevived(false);
	TestFalse(TEXT("ending revive restores profile view"),
		ReplacementView && ReplacementView->IsHidden());
	Presentation->SetWeaponViewHidden(true);

	if (ReplacementView) ReplacementView->Destroy();
	Presentation->RefreshPresentation();
	APlayerWeaponView* RecreatedView = Presentation->GetActiveWeaponView();
	TestNotNull(TEXT("missing active view is recreated"), RecreatedView);
	TestNotEqual(TEXT("recreation uses a new instance"), RecreatedView, ReplacementView);
	TestTrue(TEXT("recreated view reuses hidden state"),
		RecreatedView && RecreatedView->IsHidden());
	TestEqual(TEXT("recreation still has exactly one live view"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 1);

	Player->GetMesh()->SetSkeletalMeshAsset(nullptr);
	if (RecreatedView) RecreatedView->Destroy();
	Presentation->RefreshPresentation();
	TestNull(TEXT("missing hand socket rejects the candidate view"),
		Presentation->GetActiveWeaponView());
	TestEqual(TEXT("failed seating leaves no view actor"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 0);
	TestNull(TEXT("failed seating leaves no visible muzzle"),
		Replacement->GetFirstPersonMuzzle());
	TestTrue(TEXT("test hand socket restores"),
		PlayerWeaponPresentationTest::InstallHandSocket(*Player));
	Presentation->RefreshPresentation();
	APlayerWeaponView* SocketRestoredView =
		Presentation->GetActiveWeaponView();
	TestNotNull(TEXT("restoring the hand socket recreates the view"),
		SocketRestoredView);
	TestEqual(TEXT("socket recovery preserves one-view invariant"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 1);
	Presentation->SetWeaponViewHidden(false);

	Player->SetTakedownPresentationActive(true);
	TestTrue(TEXT("takedown presentation state hides the view"),
		SocketRestoredView && SocketRestoredView->IsHidden());
	Player->SetTakedownPresentationActive(false);
	TestFalse(TEXT("ending takedown restores the view"),
		SocketRestoredView && SocketRestoredView->IsHidden());

	AExtractionPlayer* Patient = World->SpawnActor<AExtractionPlayer>();
	TestNotNull(TEXT("revive patient spawns"), Patient);
	if (!Patient) return false;
	Patient->EnterDBNO();
	Player->BeginReviveHold(Patient);
	TestTrue(TEXT("production revive hold starts"), Player->IsRevivingTarget());
	TestTrue(TEXT("reviver-side hold hides the view"),
		SocketRestoredView && SocketRestoredView->IsHidden());
	Player->CancelRevive();
	TestFalse(TEXT("production revive hold ends"), Player->IsRevivingTarget());
	TestFalse(TEXT("ending reviver-side hold restores the view"),
		SocketRestoredView && SocketRestoredView->IsHidden());
	Patient->ExitDBNO();

	Controller->UnPossess();
	Presentation->RefreshPresentation();
	TestNull(TEXT("unpossession destroys local view"),
		Presentation->GetActiveWeaponView());
	TestNull(TEXT("unpossession clears visible muzzle"),
		Replacement->GetFirstPersonMuzzle());
	TestEqual(TEXT("unpossession leaves no live views"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 0);
	CollectGarbage(RF_NoFlags);
	if (RetainedViewClassProperty)
	{
		TestEqual(TEXT("current view class remains retained while unpossessed"),
			Cast<UClass>(RetainedViewClassProperty->GetObjectPropertyValue_InContainer(
				Presentation)),
			APlayerWeaponView::StaticClass());
	}
	Controller->Possess(Player);
	Presentation->RefreshPresentation();
	APlayerWeaponView* RepossessedView =
		Presentation->GetActiveWeaponView();
	TestNotNull(TEXT("repossess after garbage collection recreates the view"),
		RepossessedView);
	TestTrue(TEXT("repossess reuses cached placement safely"),
		Presentation->HasCachedViewPlacement());
	TestEqual(TEXT("repossess still has exactly one view"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 1);

	Presentation->SetWeaponViewCreatedHookForTesting(
		[](APlayerWeaponView& Candidate)
		{
			Candidate.GetWeaponSeatMarker()->ConfigureMarker(
				EPlayerWeaponMarkerKind::AimPoint);
		});
	AWeaponBase* InvalidViewWeapon =
		PlayerWeaponPresentationTest::EquipProfileWeapon(*Weapons);
	Presentation->SetWeaponViewCreatedHookForTesting({});
	TestNotNull(TEXT("weapon survives invalid presentation view"),
		InvalidViewWeapon);
	TestNull(TEXT("invalid candidate is destroyed"),
		Presentation->GetActiveWeaponView());
	TestNull(TEXT("invalid candidate never registers a muzzle"),
		InvalidViewWeapon
			? InvalidViewWeapon->GetFirstPersonMuzzle()
			: nullptr);
	TestFalse(TEXT("invalid candidate does not leave cached placement"),
		Presentation->HasCachedViewPlacement());
	TestEqual(TEXT("invalid candidate leaves no passive view"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 0);

	AWeaponBase* FinalWeapon =
		PlayerWeaponPresentationTest::EquipProfileWeapon(*Weapons);
	APlayerWeaponView* FinalView =
		Presentation->GetActiveWeaponView();
	TestNotNull(TEXT("valid replacement recovers after invalid view"),
		FinalView);
	TestTrue(TEXT("traversal source is bound before player teardown"),
		Traversal->OnTraversalStarted.IsBoundToObject(Presentation));
	TestTrue(TEXT("DBNO source is bound before player teardown"),
		Presentation->AreVisibilitySourcesBoundForTesting());

	Player->EndPlay(EEndPlayReason::Destroyed);
	TestTrue(TEXT("player EndPlay destroys its view"),
		!IsValid(FinalView) || FinalView->IsActorBeingDestroyed());
	TestNull(TEXT("player EndPlay clears the visible muzzle"),
		FinalWeapon ? FinalWeapon->GetFirstPersonMuzzle() : nullptr);
	TestFalse(TEXT("player EndPlay unbinds traversal visibility"),
		Traversal->OnTraversalStarted.IsBoundToObject(Presentation));
	TestFalse(TEXT("player EndPlay unbinds DBNO visibility"),
		Presentation->AreVisibilitySourcesBoundForTesting());
	TestEqual(TEXT("player EndPlay leaves no passive views"),
		PlayerWeaponPresentationTest::CountLiveWeaponViews(*World), 0);
	if (RetainedViewClassProperty)
	{
		TestNull(TEXT("player EndPlay releases the retained view class"),
			Cast<UClass>(RetainedViewClassProperty->GetObjectPropertyValue_InContainer(
				Presentation)));
	}
	Player->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponThirdPersonAttachFailureTest,
	"Extraction.PlayerWeapon.Presentation.ThirdPersonAttachFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponThirdPersonAttachFailureTest::RunTest(
	const FString& Parameters)
{
	AddExpectedMessagePlain(
		TEXT("shipping without KitWeaponPoseAsset"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		0);
	AddExpectedMessagePlain(
		TEXT("ThirdPersonVisualActor spawned but attach failed"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains);

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	AExtractionPlayer* Player =
		PlayerWeaponPresentationTest::SpawnPlayerWithDefaultWeapon(World);
	TestNotNull(TEXT("player spawns deferred"), Player);
	if (!Player) return false;
	if (!TestTrue(TEXT("player finishes with a valid hand socket"),
		PlayerWeaponPresentationTest::FinishPlayerSpawn(Player)))
		return false;

	UWeaponComponent* Weapons = Player->GetWeaponComponent();
	TestNotNull(TEXT("weapon component exists"), Weapons);
	if (!Weapons) return false;

	bool bDeferredHookSawNoCurrentWeapon = false;
	AActor* FailedVisualCandidate = nullptr;
	const FDelegateHandle SpawnedHandle = World->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateLambda(
			[&FailedVisualCandidate](AActor* SpawnedActor)
			{
				if (IsValid(SpawnedActor)
					&& SpawnedActor->GetClass() == AActor::StaticClass())
					FailedVisualCandidate = SpawnedActor;
			}));
	AWeaponBase* Weapon = PlayerWeaponPresentationTest::EquipProfileWeapon(
		*Weapons, AActor::StaticClass(),
		&bDeferredHookSawNoCurrentWeapon);
	World->RemoveOnActorSpawnedHandler(SpawnedHandle);
	TestNotNull(TEXT("weapon survives visual attach failure"), Weapon);
	TestTrue(TEXT("deferred test hook matches production assignment order"),
		bDeferredHookSawNoCurrentWeapon);
	TestNotNull(TEXT("failed visual candidate was spawned"),
		FailedVisualCandidate);
	TestTrue(TEXT("failed visual candidate is pending destruction"),
		FailedVisualCandidate
			&& FailedVisualCandidate->IsActorBeingDestroyed());
	if (Weapon)
	{
		TestNull(TEXT("failed third-person visual is destroyed and released"),
			PlayerWeaponPresentationTest::GetThirdPersonVisual(*Weapon));
		TestTrue(TEXT("bare weapon mesh remains visible as fallback"),
			IsValid(Weapon->GetWeaponMesh())
				&& Weapon->GetWeaponMesh()->IsVisible());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponSeatPlacementTest,
	"Extraction.PlayerWeapon.Presentation.SeatPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponSeatPlacementTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	AExtractionPlayer* Player = World->SpawnActor<AExtractionPlayer>();
	APlayerWeaponView* View = World->SpawnActor<APlayerWeaponView>();
	TestNotNull(TEXT("player spawns"), Player);
	TestNotNull(TEXT("view spawns"), View);
	if (!Player || !View) return false;

	const FTransform SeatRelative(
		FRotator(7.f, -21.f, 13.f), FVector(14.f, -3.f, 8.f));
	const FVector AuthoredArtScale(1.4f, 0.8f, 1.9f);
	View->GetArtRoot()->SetRelativeScale3D(AuthoredArtScale);
	View->GetWeaponSeatMarker()->SetRelativeTransform(SeatRelative);
	TestTrue(TEXT("offset view remains valid"), View->InitializeView());
	FTransform Placement = FTransform::Identity;
	TestTrue(TEXT("offset seat resolves"),
		UPlayerWeaponPresentationComponent::ResolveViewPlacement(
			*View, EPlayerWeaponSeatPolicy::WeaponSeatMarker, Placement));

	TestTrue(TEXT("test hand socket installs"),
		PlayerWeaponPresentationTest::InstallHandSocket(*Player));
	USkeletalMeshComponent* Mesh = Player->GetMesh();
	TestNotNull(TEXT("player mesh exists"), Mesh);
	if (!Mesh) return false;
	const FName HandSocket(TEXT("ik_hand_gun"));
	View->AttachToComponent(
		Mesh,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		HandSocket);
	View->GetRootComponent()->SetRelativeLocationAndRotation(
		Placement.GetLocation(), Placement.GetRotation());
	const FTransform Target = Mesh->GetSocketTransform(HandSocket);
	TestTrue(TEXT("weapon seat lands on hand target"),
		View->GetWeaponSeatMarker()->GetComponentLocation().Equals(
			Target.GetLocation(), 0.01f));
	TestTrue(TEXT("weapon seat orientation matches hand target"),
		View->GetWeaponSeatMarker()->GetComponentQuat().Equals(
			Target.GetRotation(), 0.001f));
	TestTrue(TEXT("seating preserves authored art scale"),
		View->GetArtRoot()->GetRelativeScale3D().Equals(AuthoredArtScale));

	View->GetWeaponSeatMarker()->SetRelativeScale3D(FVector(1.2f));
	TestFalse(TEXT("scaled seat marker is rejected"),
		UPlayerWeaponPresentationComponent::ResolveViewPlacement(
			*View, EPlayerWeaponSeatPolicy::WeaponSeatMarker, Placement));
	View->GetWeaponSeatMarker()->SetRelativeScale3D(FVector::OneVector);
	View->GetRootComponent()->SetRelativeScale3D(FVector(0.8f));
	TestFalse(TEXT("scaled view root is rejected"),
		UPlayerWeaponPresentationComponent::ResolveViewPlacement(
			*View, EPlayerWeaponSeatPolicy::WeaponSeatMarker, Placement));
	View->GetRootComponent()->SetRelativeScale3D(FVector::OneVector);
	View->GetWeaponSeatMarker()->ConfigureMarker(EPlayerWeaponMarkerKind::AimPoint);
	TestFalse(TEXT("missing typed seat rejects placement"),
		UPlayerWeaponPresentationComponent::ResolveViewPlacement(
			*View, EPlayerWeaponSeatPolicy::WeaponSeatMarker, Placement));
	TestTrue(TEXT("legacy root policy remains identity"),
		UPlayerWeaponPresentationComponent::ResolveViewPlacement(
			*View, EPlayerWeaponSeatPolicy::LegacyViewRoot, Placement));
	TestTrue(TEXT("legacy root placement is identity"),
		Placement.Equals(FTransform::Identity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerWeaponOpticLifecycleTest,
	"Extraction.PlayerWeapon.Presentation.OpticLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPlayerWeaponOpticLifecycleTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(
		TEXT("shipping without KitWeaponPoseAsset"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		0);
	AddExpectedMessagePlain(
		TEXT("WeaponVisualMeshName 'WeaponMesh' not found"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		0);

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	AExtractionPlayer* Player =
		PlayerWeaponPresentationTest::SpawnPlayerWithDefaultWeapon(World);
	TestNotNull(TEXT("player spawns deferred"), Player);
	if (!Player) return false;
	if (!TestTrue(TEXT("player finishes with a valid hand socket"),
		PlayerWeaponPresentationTest::FinishPlayerSpawn(Player)))
		return false;

	UWeaponComponent* Weapons = Player->GetWeaponComponent();
	UPlayerWeaponPresentationComponent* Presentation =
		Player->GetWeaponPresentationComponent();
	AWeaponBase* InitialWeapon =
		Weapons ? Weapons->GetCurrentWeapon() : nullptr;
	TestNotNull(TEXT("weapon component exists"), Weapons);
	TestNotNull(TEXT("presentation component exists"), Presentation);
	TestNotNull(TEXT("initial weapon exists"), InitialWeapon);
	if (!Weapons || !Presentation || !InitialWeapon) return false;


	UWeaponDataAsset* InitialData =
		PlayerWeaponPresentationTest::ConfigureOpticProfileWeapon(
			*InitialWeapon);
	UPlayerWeaponPresentationProfile* InitialProfile =
		IsValid(InitialData)
			? InitialData->PlayerPresentationProfile.Get()
			: nullptr;
	TestNotNull(TEXT("optic profile data is assigned"), InitialData);
	TestNotNull(TEXT("optic profile exists"), InitialProfile);
	if (!InitialData || !InitialProfile) return false;

	Presentation->SetWeaponViewCreatedHookForTesting(
		[](APlayerWeaponView& Candidate)
		{
			Candidate.GetIronRearMarker()->SetRelativeTransform(
				FTransform(FQuat::Identity, FVector(14.f, 0.f, 5.f)));
			Candidate.GetIronFrontMarker()->SetRelativeTransform(
				FTransform(FQuat::Identity, FVector(44.f, 0.f, 5.f)));
			Candidate.GetOpticMountMarker()->SetRelativeTransform(
				FTransform(
					FRotator(2.f, 3.f, -1.f),
					FVector(20.f, 1.f, 7.f)));
		});

	int32 AttachmentViewsCreated = 0;
	const FDelegateHandle AttachmentSpawnHandle =
		World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateLambda(
				[&AttachmentViewsCreated](AActor* SpawnedActor)
				{
					APlayerWeaponAttachmentView* Candidate =
						Cast<APlayerWeaponAttachmentView>(
							SpawnedActor);
					if (!IsValid(Candidate)) return;

					++AttachmentViewsCreated;
					check(Candidate->GetAttachmentMountMarker());
					check(Candidate->GetAimPointMarker());
					Candidate->GetAttachmentMountMarker()->
						SetRelativeTransform(
							FTransform(
								FRotator(-1.f, 5.f, 2.f),
								FVector(-3.f, 1.f, 2.f)));
					Candidate->GetAimPointMarker()->
						SetRelativeTransform(
							FTransform(
								FRotator(-2.f, 1.f, 4.f),
								FVector(4.f, 0.f, 3.f)));
				}));

	int32 SightEvents = 0;
	bool bSightReadyBeforeEvent = true;
	const FDelegateHandle SightHandle =
		Presentation->OnPresentedSightChangedNative.AddLambda(
			[Presentation, &SightEvents, &bSightReadyBeforeEvent](
				const FPlayerWeaponResolvedSight& Sight)
			{
				++SightEvents;
				bSightReadyBeforeEvent &=
					PlayerWeaponPresentationTest::ResolvedSightsMatch(
						Presentation->GetResolvedSight(), Sight);
				bSightReadyBeforeEvent &=
					Presentation->GetSelectedOpticId()
					== Sight.OpticId;

				APlayerWeaponAttachmentView* ActiveOptic =
					Presentation->GetActiveOpticView();
				if (Sight.bIsValid && Sight.bUsesOptic)
				{
					bSightReadyBeforeEvent &=
						IsValid(ActiveOptic)
						&& !ActiveOptic->IsActorBeingDestroyed()
						&& ActiveOptic->IsViewInitialized();
				}
				else
				{
					bSightReadyBeforeEvent &= !IsValid(ActiveOptic);
				}
			});

	APlayerController* Controller =
		PlayerWeaponPresentationTest::PossessPlayerLocally(
			World, Player);
	TestNotNull(TEXT("local controller possesses player"), Controller);
	if (!Controller)
	{
		Presentation->OnPresentedSightChangedNative.Remove(SightHandle);
		World->RemoveOnActorSpawnedHandler(AttachmentSpawnHandle);
		Presentation->SetWeaponViewCreatedHookForTesting({});
		return false;
	}

	const FName OpticAId(TEXT("optic.a"));
	const FName OpticBId(TEXT("optic.b"));
	APlayerWeaponAttachmentView* DefaultOptic =
		Presentation->GetActiveOpticView();
	const FPlayerWeaponResolvedSight DefaultSight =
		Presentation->GetResolvedSight();
	TestNotNull(TEXT("default stable optic ID spawns a view"), DefaultOptic);
	TestEqual(TEXT("default optic commits by stable ID"),
		Presentation->GetSelectedOpticId(), OpticAId);
	TestEqual(TEXT("default optic creates exactly one view"),
		AttachmentViewsCreated, 1);
	TestEqual(TEXT("default optic keeps exactly one live view"),
		PlayerWeaponPresentationTest::CountLiveAttachmentViews(*World), 1);
	TestTrue(TEXT("default optic view is initialized before publication"),
		IsValid(DefaultOptic) && DefaultOptic->IsViewInitialized());
	TestTrue(TEXT("default optic resolves a valid sight"),
		DefaultSight.bIsValid && DefaultSight.bUsesOptic);
	TestEqual(TEXT("default resolved sight keeps stable ID"),
		DefaultSight.OpticId, OpticAId);
	TestEqual(TEXT("default optic applies its FOV override"),
		DefaultSight.ADSSettings.FieldOfView, 48.f);
	TestEqual(TEXT("default optic applies its aim-distance override"),
		DefaultSight.ADSSettings.AimDistanceFromCameraCm, 51.f);
	TestEqual(TEXT("default optic publishes one sight edge"),
		SightEvents, 1);
	TestTrue(TEXT("default resolved state is ready before its event"),
		bSightReadyBeforeEvent);
	if (!DefaultOptic)
	{
		Presentation->OnPresentedSightChangedNative.Remove(SightHandle);
		World->RemoveOnActorSpawnedHandler(AttachmentSpawnHandle);
		Presentation->SetWeaponViewCreatedHookForTesting({});
		return false;
	}

	const FName CommittedId = Presentation->GetSelectedOpticId();
	APlayerWeaponAttachmentView* CommittedView =
		Presentation->GetActiveOpticView();
	const FPlayerWeaponResolvedSight CommittedSight =
		Presentation->GetResolvedSight();
	const int32 CommittedSightEvents = SightEvents;
	const int32 CommittedCreatedViews = AttachmentViewsCreated;
	auto TestRejectedSelection =
		[this, Presentation, World, CommittedId, CommittedView,
			CommittedSight, CommittedSightEvents, &SightEvents,
			CommittedCreatedViews, &AttachmentViewsCreated](
				FName RejectedId, const TCHAR* Description)
		{
			TestFalse(
				FString::Printf(
					TEXT("%s selection is rejected"), Description),
				Presentation->SetSelectedOpticId(RejectedId));
			TestEqual(
				FString::Printf(
					TEXT("%s cannot change the committed ID"),
					Description),
				Presentation->GetSelectedOpticId(),
				CommittedId);
			TestEqual(
				FString::Printf(
					TEXT("%s cannot swap the committed view"),
					Description),
				Presentation->GetActiveOpticView(),
				CommittedView);
			TestTrue(
				FString::Printf(
					TEXT("%s cannot change the resolved sight"),
					Description),
				PlayerWeaponPresentationTest::ResolvedSightsMatch(
					Presentation->GetResolvedSight(),
					CommittedSight));
			TestEqual(
				FString::Printf(
					TEXT("%s cannot publish a sight event"),
					Description),
				SightEvents,
				CommittedSightEvents);
			TestEqual(
				FString::Printf(
					TEXT("%s cannot create an attachment view"),
					Description),
				AttachmentViewsCreated,
				CommittedCreatedViews);
			TestEqual(
				FString::Printf(
					TEXT("%s preserves one live attachment view"),
					Description),
				PlayerWeaponPresentationTest::CountLiveAttachmentViews(
					*World),
				1);
		};

	TestRejectedSelection(
		FName(TEXT("optic.incompatible")),
		TEXT("incompatible optic ID"));
	TestRejectedSelection(
		FName(TEXT("optic.missing")),
		TEXT("missing optic ID"));
	TestRejectedSelection(
		FName(TEXT("grip.wrong-slot")),
		TEXT("wrong-slot attachment ID"));
	TestEqual(TEXT("all rejected selections preserve event count"),
		SightEvents, CommittedSightEvents);

	const int32 EventsBeforeSwap = SightEvents;
	const int32 ViewsBeforeSwap = AttachmentViewsCreated;
	TestTrue(TEXT("compatible optic B selection succeeds"),
		Presentation->SetSelectedOpticId(OpticBId));
	APlayerWeaponAttachmentView* OpticB =
		Presentation->GetActiveOpticView();
	const FPlayerWeaponResolvedSight OpticBSight =
		Presentation->GetResolvedSight();
	TestNotNull(TEXT("optic B owns an active view"), OpticB);
	TestNotEqual(TEXT("optic B swaps the view once"),
		OpticB, DefaultOptic);
	TestTrue(TEXT("optic A is destroyed by the committed swap"),
		!IsValid(DefaultOptic)
			|| DefaultOptic->IsActorBeingDestroyed());
	TestEqual(TEXT("optic B commits its stable ID"),
		Presentation->GetSelectedOpticId(), OpticBId);
	TestEqual(TEXT("optic B creates exactly one replacement view"),
		AttachmentViewsCreated, ViewsBeforeSwap + 1);
	TestEqual(TEXT("optic B emits exactly one sight edge"),
		SightEvents, EventsBeforeSwap + 1);
	TestEqual(TEXT("optic swap preserves one live attachment"),
		PlayerWeaponPresentationTest::CountLiveAttachmentViews(*World), 1);
	TestTrue(TEXT("optic B resolves valid optic ADS"),
		OpticBSight.bIsValid && OpticBSight.bUsesOptic);
	TestEqual(TEXT("optic B resolved sight keeps stable ID"),
		OpticBSight.OpticId, OpticBId);
	TestEqual(TEXT("optic B applies its FOV override"),
		OpticBSight.ADSSettings.FieldOfView, 58.f);
	TestEqual(TEXT("optic B applies its aim-distance override"),
		OpticBSight.ADSSettings.AimDistanceFromCameraCm, 63.f);
	TestTrue(TEXT("optic B state is ready before its event"),
		bSightReadyBeforeEvent);

	const int32 EventsBeforeReorder = SightEvents;
	const int32 ViewsBeforeReorder = AttachmentViewsCreated;
	APlayerWeaponAttachmentView* ViewBeforeReorder =
		Presentation->GetActiveOpticView();
	const FPlayerWeaponResolvedSight SightBeforeReorder =
		Presentation->GetResolvedSight();
	InitialProfile->CompatibleAttachments.Swap(0, 1);
	Presentation->RefreshPresentation();
	TestEqual(TEXT("array reorder preserves optic B stable ID"),
		Presentation->GetSelectedOpticId(), OpticBId);
	TestEqual(TEXT("array reorder preserves the active optic instance"),
		Presentation->GetActiveOpticView(), ViewBeforeReorder);
	TestTrue(TEXT("array reorder preserves resolved ADS"),
		PlayerWeaponPresentationTest::ResolvedSightsMatch(
			Presentation->GetResolvedSight(),
			SightBeforeReorder));
	TestEqual(TEXT("array reorder creates no attachment view"),
		AttachmentViewsCreated, ViewsBeforeReorder);
	TestEqual(TEXT("array reorder emits no sight edge"),
		SightEvents, EventsBeforeReorder);

	UPlayerWeaponAttachmentDefinition* InactiveOpticA =
		InitialProfile->FindAttachmentDefinition(OpticAId);
	UPlayerWeaponAttachmentDefinition* SelectedOpticB =
		InitialProfile->FindAttachmentDefinition(OpticBId);
	TestNotNull(TEXT("inactive optic A definition exists"),
		InactiveOpticA);
	TestNotNull(TEXT("selected optic B definition exists"),
		SelectedOpticB);
	if (!InactiveOpticA || !SelectedOpticB)
	{
		Presentation->OnPresentedSightChangedNative.Remove(SightHandle);
		World->RemoveOnActorSpawnedHandler(AttachmentSpawnHandle);
		Presentation->SetWeaponViewCreatedHookForTesting({});
		return false;
	}

	const int32 EventsBeforeInactiveRemoval = SightEvents;
	const int32 ViewsBeforeInactiveRemoval = AttachmentViewsCreated;
	const FPlayerWeaponResolvedSight SightBeforeInactiveRemoval =
		Presentation->GetResolvedSight();
	APlayerWeaponAttachmentView* ViewBeforeInactiveRemoval =
		Presentation->GetActiveOpticView();
	TestEqual(TEXT("inactive optic A definition is removed once"),
		InitialProfile->CompatibleAttachments.Remove(InactiveOpticA), 1);
	Presentation->RefreshPresentation();
	TestEqual(TEXT("inactive removal preserves selected optic B ID"),
		Presentation->GetSelectedOpticId(), OpticBId);
	TestEqual(TEXT("inactive removal preserves selected optic B view"),
		Presentation->GetActiveOpticView(), ViewBeforeInactiveRemoval);
	TestTrue(TEXT("inactive removal preserves resolved optic B ADS"),
		PlayerWeaponPresentationTest::ResolvedSightsMatch(
			Presentation->GetResolvedSight(),
			SightBeforeInactiveRemoval));
	TestEqual(TEXT("inactive removal creates no attachment view"),
		AttachmentViewsCreated, ViewsBeforeInactiveRemoval);
	TestEqual(TEXT("inactive removal emits no sight edge"),
		SightEvents, EventsBeforeInactiveRemoval);

	InitialProfile->CompatibleAttachments.Add(InactiveOpticA);
	Presentation->RefreshPresentation();
	TestEqual(TEXT("restoring inactive optic A remains presentation-silent"),
		SightEvents, EventsBeforeInactiveRemoval);
	TestEqual(TEXT("restoring inactive optic A preserves optic B view"),
		Presentation->GetActiveOpticView(), ViewBeforeInactiveRemoval);

	const int32 EventsBeforeSelectedRemoval = SightEvents;
	const int32 ViewsBeforeSelectedRemoval = AttachmentViewsCreated;
	APlayerWeaponAttachmentView* ViewBeforeSelectedRemoval =
		Presentation->GetActiveOpticView();
	TestEqual(TEXT("selected optic B definition is removed once"),
		InitialProfile->CompatibleAttachments.Remove(SelectedOpticB), 1);
	Presentation->RefreshPresentation();
	APlayerWeaponAttachmentView* DefaultAfterSelectedRemoval =
		Presentation->GetActiveOpticView();
	const FPlayerWeaponResolvedSight SightAfterSelectedRemoval =
		Presentation->GetResolvedSight();
	TestEqual(TEXT("selected removal falls back by default stable ID"),
		Presentation->GetSelectedOpticId(), OpticAId);
	TestNotNull(TEXT("selected removal spawns the default optic"),
		DefaultAfterSelectedRemoval);
	TestNotEqual(TEXT("selected removal replaces the stale optic view"),
		DefaultAfterSelectedRemoval, ViewBeforeSelectedRemoval);
	TestTrue(TEXT("selected removal destroys the stale optic view"),
		!IsValid(ViewBeforeSelectedRemoval)
			|| ViewBeforeSelectedRemoval->IsActorBeingDestroyed());
	TestEqual(TEXT("selected removal creates exactly one default view"),
		AttachmentViewsCreated, ViewsBeforeSelectedRemoval + 1);
	TestEqual(TEXT("selected removal emits exactly one sight edge"),
		SightEvents, EventsBeforeSelectedRemoval + 1);
	TestEqual(TEXT("selected removal preserves one live optic"),
		PlayerWeaponPresentationTest::CountLiveAttachmentViews(*World), 1);
	TestTrue(TEXT("selected removal resolves the default optic"),
		SightAfterSelectedRemoval.bIsValid
			&& SightAfterSelectedRemoval.bUsesOptic
			&& SightAfterSelectedRemoval.OpticId == OpticAId);
	TestEqual(TEXT("selected removal reapplies default optic settings"),
		SightAfterSelectedRemoval.ADSSettings.AimDistanceFromCameraCm,
		51.f);
	TestTrue(TEXT("selected removal commits before its event"),
		bSightReadyBeforeEvent);

	const int32 EventsBeforeIrons = SightEvents;
	const int32 ViewsBeforeIrons = AttachmentViewsCreated;
	TestTrue(TEXT("NAME_None selects authored iron sights"),
		Presentation->SetSelectedOpticId(NAME_None));
	const FPlayerWeaponResolvedSight IronSight =
		Presentation->GetResolvedSight();
	TestEqual(TEXT("irons commit NAME_None"),
		Presentation->GetSelectedOpticId(), NAME_None);
	TestNull(TEXT("irons atomically clear the active optic"),
		Presentation->GetActiveOpticView());
	TestTrue(TEXT("active default optic is destroyed when irons commit"),
		!IsValid(DefaultAfterSelectedRemoval)
			|| DefaultAfterSelectedRemoval->IsActorBeingDestroyed());
	TestEqual(TEXT("irons leave no live attachment views"),
		PlayerWeaponPresentationTest::CountLiveAttachmentViews(*World), 0);
	TestEqual(TEXT("irons do not create an attachment view"),
		AttachmentViewsCreated, ViewsBeforeIrons);
	TestEqual(TEXT("irons emit one resolved sight edge"),
		SightEvents, EventsBeforeIrons + 1);
	TestTrue(TEXT("paired irons resolve valid ADS"),
		IronSight.bIsValid && !IronSight.bUsesOptic);
	TestEqual(TEXT("resolved irons retain NAME_None"),
		IronSight.OpticId, NAME_None);
	TestEqual(TEXT("irons retain profile FOV"),
		IronSight.ADSSettings.FieldOfView, 70.f);
	TestEqual(TEXT("irons retain the profile aim distance"),
		IronSight.ADSSettings.AimDistanceFromCameraCm, 47.f);
	TestTrue(TEXT("irons are committed before their event"),
		bSightReadyBeforeEvent);

	const int32 EventsBeforeUnpossess = SightEvents;
	Controller->UnPossess();
	Presentation->RefreshPresentation();
	TestNull(TEXT("unpossess clears the active weapon view"),
		Presentation->GetActiveWeaponView());
	TestNull(TEXT("unpossess clears the active optic view"),
		Presentation->GetActiveOpticView());
	TestFalse(TEXT("unpossess invalidates resolved sight"),
		Presentation->GetResolvedSight().bIsValid);
	TestEqual(TEXT("unpossess emits one sight clear edge"),
		SightEvents, EventsBeforeUnpossess + 1);
	TestEqual(TEXT("unpossess leaves no live optic"),
		PlayerWeaponPresentationTest::CountLiveAttachmentViews(*World), 0);

	Controller->Possess(Player);
	Presentation->RefreshPresentation();
	const FPlayerWeaponResolvedSight RepossessedSight =
		Presentation->GetResolvedSight();
	TestEqual(TEXT("repossess retains selected irons"),
		Presentation->GetSelectedOpticId(), NAME_None);
	TestNull(TEXT("repossess does not fabricate an optic"),
		Presentation->GetActiveOpticView());
	TestTrue(TEXT("repossess restores valid paired irons"),
		RepossessedSight.bIsValid && !RepossessedSight.bUsesOptic);
	TestEqual(TEXT("repossess emits one restored sight edge"),
		SightEvents, EventsBeforeUnpossess + 2);
	TestEqual(TEXT("repossess creates no attachment view"),
		AttachmentViewsCreated, ViewsBeforeIrons);
	TestTrue(TEXT("possession lifecycle events see committed state"),
		bSightReadyBeforeEvent);

	const int32 EventsBeforeReplacement = SightEvents;
	const int32 ViewsBeforeReplacement = AttachmentViewsCreated;
	UWeaponDataAsset* ReplacementData = nullptr;
	Weapons->SetPreFinishWeaponSpawnHookForTesting(
		[&ReplacementData](AWeaponBase& Weapon)
		{
			ReplacementData =
				PlayerWeaponPresentationTest::
					ConfigureOpticProfileWeapon(Weapon);
		});
	Weapons->EquipWeapon(AWeaponBase::StaticClass());
	AWeaponBase* ReplacementWeapon = Weapons->GetCurrentWeapon();
	if (IsValid(ReplacementWeapon)
		&& !ReplacementWeapon->HasActorBegunPlay())
		ReplacementWeapon->DispatchBeginPlay();
	Weapons->SetPreFinishWeaponSpawnHookForTesting({});
	Presentation->RefreshPresentation();

	UPlayerWeaponPresentationProfile* ReplacementProfile =
		IsValid(ReplacementData)
			? ReplacementData->PlayerPresentationProfile.Get()
			: nullptr;
	APlayerWeaponAttachmentView* ReplacementOptic =
		Presentation->GetActiveOpticView();
	const FPlayerWeaponResolvedSight ReplacementSight =
		Presentation->GetResolvedSight();
	TestNotNull(TEXT("replacement profile weapon equips"),
		ReplacementWeapon);
	TestNotNull(TEXT("replacement profile data is assigned"),
		ReplacementData);
	TestNotNull(TEXT("replacement profile exists"),
		ReplacementProfile);
	TestNotNull(TEXT("replacement default optic spawns"),
		ReplacementOptic);
	TestEqual(TEXT("replacement shares the same default stable ID"),
		ReplacementProfile
			? ReplacementProfile->DefaultOpticId
			: NAME_None,
		OpticAId);
	TestEqual(TEXT("replacement reinitializes its default optic ID"),
		Presentation->GetSelectedOpticId(), OpticAId);
	TestNotEqual(TEXT("same default ID still creates a new optic view"),
		ReplacementOptic, DefaultOptic);
	TestEqual(TEXT("replacement creates exactly one optic view"),
		AttachmentViewsCreated, ViewsBeforeReplacement + 1);
	TestEqual(TEXT("replacement preserves one live optic"),
		PlayerWeaponPresentationTest::CountLiveAttachmentViews(*World), 1);
	TestEqual(TEXT("replacement clears the stale sight then publishes the new sight"),
		SightEvents, EventsBeforeReplacement + 2);
	TestTrue(TEXT("replacement resolves valid default optic ADS"),
		ReplacementSight.bIsValid
			&& ReplacementSight.bUsesOptic
			&& ReplacementSight.OpticId == OpticAId);
	TestEqual(TEXT("replacement default reapplies its ADS override"),
		ReplacementSight.ADSSettings.AimDistanceFromCameraCm, 51.f);
	TestTrue(TEXT("replacement state is ready before its event"),
		bSightReadyBeforeEvent);

	APlayerWeaponView* ReplacementWeaponView =
		Presentation->GetActiveWeaponView();
	TestNotNull(TEXT("replacement weapon view exists"),
		ReplacementWeaponView);
	Presentation->SetWeaponViewHidden(true);
	TestTrue(TEXT("suppression hides the weapon view"),
		IsValid(ReplacementWeaponView)
			&& ReplacementWeaponView->IsHidden());
	TestTrue(TEXT("suppression hides the optic with its weapon"),
		IsValid(ReplacementOptic)
			&& ReplacementOptic->IsHidden());
	Presentation->SetWeaponViewHidden(false);
	TestTrue(TEXT("suppression release restores the weapon view"),
		IsValid(ReplacementWeaponView)
			&& !ReplacementWeaponView->IsHidden());
	TestTrue(TEXT("suppression release restores the optic"),
		IsValid(ReplacementOptic)
			&& !ReplacementOptic->IsHidden());

	Presentation->OnPresentedSightChangedNative.Remove(SightHandle);
	World->RemoveOnActorSpawnedHandler(AttachmentSpawnHandle);
	Presentation->SetWeaponViewCreatedHookForTesting({});
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
