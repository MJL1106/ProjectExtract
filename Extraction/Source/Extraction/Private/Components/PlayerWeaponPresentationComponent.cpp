// Player-owned bridge between authoritative weapon state and first-person presentation.

#include "Components/PlayerWeaponPresentationComponent.h"

#include "Character/ExtractionPlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WeaponComponent.h"
#include "Core/Extraction.h"
#include "Data/PlayerWeaponAttachmentDefinition.h"
#include "Data/PlayerWeaponPresentationProfile.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Movement/TraversalComponent.h"
#include "Weapon/PlayerWeaponView.h"
#include "Weapon/PlayerWeaponADSResolver.h"
#include "Weapon/WeaponBase.h"

namespace
{
	const FName PlayerWeaponHandSocket(TEXT("ik_hand_gun"));

	bool ResolvedSightsEqual(
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

	bool IsUsableOpticDefinition(
		const UPlayerWeaponPresentationProfile& Profile,
		const UPlayerWeaponAttachmentDefinition* Definition)
	{
		return IsValid(Definition)
			&& !Definition->AttachmentId.IsNone()
			&& Definition->Slot == EPlayerWeaponAttachmentSlot::Optic
			&& Profile.IsAttachmentCompatible(
				Definition->AttachmentId, EPlayerWeaponAttachmentSlot::Optic)
			&& !Definition->ViewClass.IsNull()
			&& Definition->ViewClass.ToSoftObjectPath().IsValid();
	}

	void DestroyAttachmentView(APlayerWeaponAttachmentView* View)
	{
		if (!IsValid(View)) return;
		View->SetActorHiddenInGame(true);
		View->ReleaseView();
		View->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		View->Destroy();
	}

	bool IsUsableWeaponView(const APlayerWeaponView* View)
	{
		return IsValid(View) && !View->IsActorBeingDestroyed();
	}
}

UPlayerWeaponPresentationComponent::UPlayerWeaponPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UPlayerWeaponPresentationComponent::BeginPlay()
{
	Super::BeginPlay();

	PlayerOwner = Cast<AExtractionPlayer>(GetOwner());
	DiscoverAndBindWeaponComponent();
	BindVisibilitySources();
	RefreshPresentation();
}

void UPlayerWeaponPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetPresentationActive(false);
	CancelPendingOpticViewLoad();
	CancelPendingWeaponViewLoad();
	UnbindVisibilitySources();
	UnbindWeaponComponent();
	PlayerOwner.Reset();
	CurrentWeapon.Reset();
	LastRoutedWeapon.Reset();
	bHasRoutedAiming = false;
	CachedLoadedViewClass = nullptr;
	CachedLoadedViewClassPath.Reset();
	SightWeapon.Reset();
	SightProfile.Reset();
	RequestedOpticId = NAME_None;
	SelectedOpticId = NAME_None;
	ResolvedSight = FPlayerWeaponResolvedSight();
	ResetCachedViewPlacement();
#if WITH_DEV_AUTOMATION_TESTS
	WeaponViewCreatedHookForTesting = {};
#endif

	Super::EndPlay(EndPlayReason);
}

void UPlayerWeaponPresentationComponent::RefreshPresentation()
{
	DiscoverAndBindWeaponComponent();

	if (!CanRouteLocalPresentation())
	{
		SetPresentationActive(false);
		LastRoutedWeapon.Reset();
		bHasRoutedAiming = false;
		return;
	}

	if (!bPresentationActive)
	{
		SetPresentationActive(true);
		PublishPresentationSnapshot();
		return;
	}

	RouteLegacyWeaponChanged(CurrentWeapon.Get());
	RouteLegacyAimingChanged(bIsAiming);
	SynchronizeWeaponView();
}

void UPlayerWeaponPresentationComponent::DiscoverAndBindWeaponComponent()
{
	AExtractionPlayer* Player = PlayerOwner.Get();
	if (!IsValid(Player))
	{
		Player = Cast<AExtractionPlayer>(GetOwner());
		PlayerOwner = Player;
	}
	if (!IsValid(Player)) return;

	UWeaponComponent* FoundComponent = Player->GetWeaponComponent();
	if (bBoundToWeaponComponent && WeaponComponent == FoundComponent) return;

	UnbindWeaponComponent();
	if (!IsValid(FoundComponent)) return;

	WeaponComponent = FoundComponent;
	WeaponComponent->OnCurrentWeaponChangedNative.AddUObject(
		this, &UPlayerWeaponPresentationComponent::HandleWeaponChanged);
	WeaponComponent->OnAimingChangedNative.AddUObject(
		this, &UPlayerWeaponPresentationComponent::HandleAimingChanged);
	WeaponComponent->OnTriggerChangedNative.AddUObject(
		this, &UPlayerWeaponPresentationComponent::HandleTriggerChanged);
	WeaponComponent->OnWeaponShotNative.AddUObject(
		this, &UPlayerWeaponPresentationComponent::HandleShot);
	WeaponComponent->OnWeaponAmmoChangedNative.AddUObject(
		this, &UPlayerWeaponPresentationComponent::HandleAmmoChanged);
	WeaponComponent->OnWeaponReloadPhaseChangedNative.AddUObject(
		this, &UPlayerWeaponPresentationComponent::HandleReloadPhaseChanged);
	bBoundToWeaponComponent = true;

	HandleWeaponChanged(WeaponComponent->GetCurrentWeapon());
	HandleAimingChanged(WeaponComponent->IsAiming());
	HandleTriggerChanged(WeaponComponent->IsTriggerHeld());
}

void UPlayerWeaponPresentationComponent::UnbindWeaponComponent()
{
	if (WeaponComponent)
	{
		WeaponComponent->OnCurrentWeaponChangedNative.RemoveAll(this);
		WeaponComponent->OnAimingChangedNative.RemoveAll(this);
		WeaponComponent->OnTriggerChangedNative.RemoveAll(this);
		WeaponComponent->OnWeaponShotNative.RemoveAll(this);
		WeaponComponent->OnWeaponAmmoChangedNative.RemoveAll(this);
		WeaponComponent->OnWeaponReloadPhaseChangedNative.RemoveAll(this);
	}

	bBoundToWeaponComponent = false;
	WeaponComponent = nullptr;
}

void UPlayerWeaponPresentationComponent::HandleWeaponChanged(AWeaponBase* Weapon)
{
	AWeaponBase* ValidWeapon = IsValid(Weapon) ? Weapon : nullptr;
	const bool bSameWeapon = IsValid(ValidWeapon)
		? CurrentWeapon.Get() == ValidWeapon
		: !CurrentWeapon.IsValid() && !CurrentWeapon.IsStale();

	CurrentAmmo = IsValid(ValidWeapon) ? ValidWeapon->GetCurrentAmmo() : 0;
	ReserveAmmo = IsValid(ValidWeapon) ? ValidWeapon->GetReserveAmmo() : 0;
	if (bSameWeapon)
	{
		if (bPresentationActive) SynchronizeWeaponView();
		return;
	}

	CurrentWeapon = ValidWeapon;
	CancelPendingOpticViewLoad();
	DestroyOpticView();
	SightWeapon = ValidWeapon;
	SightProfile.Reset();
	RequestedOpticId = NAME_None;
	SelectedOpticId = NAME_None;
	CachedLoadedViewClass = nullptr;
	CachedLoadedViewClassPath.Reset();
	ResetCachedViewPlacement();
	LastReloadPhase = EWeaponReloadPhase::Completed;
	bHasObservedReloadPhase = false;
	bHasRoutedAiming = false;

	if (!bPresentationActive)
	{
		ResolvedSight = FPlayerWeaponResolvedSight();
		return;
	}

	SynchronizeWeaponView();
	OnPresentedWeaponChangedNative.Broadcast(ValidWeapon);
	RouteLegacyWeaponChanged(ValidWeapon);
	SynchronizeWeaponView();
	if (bIsAiming && IsValid(ValidWeapon))
	{
		OnPresentedAimingChangedNative.Broadcast(true);
		RouteLegacyAimingChanged(true);
	}
}

void UPlayerWeaponPresentationComponent::HandleAimingChanged(bool bNewAiming)
{
	if (bIsAiming == bNewAiming) return;

	bIsAiming = bNewAiming;
	if (!bPresentationActive) return;

	OnPresentedAimingChangedNative.Broadcast(bIsAiming);
	RouteLegacyAimingChanged(bIsAiming);
}

void UPlayerWeaponPresentationComponent::HandleTriggerChanged(bool bNewTriggerHeld)
{
	if (bTriggerHeld == bNewTriggerHeld) return;

	bTriggerHeld = bNewTriggerHeld;
	if (!bPresentationActive) return;

	OnPresentedTriggerChangedNative.Broadcast(bTriggerHeld);
}

void UPlayerWeaponPresentationComponent::HandleShot()
{
	if (!bPresentationActive) return;

	OnPresentedShotNative.Broadcast();
}

void UPlayerWeaponPresentationComponent::HandleAmmoChanged(int32 NewCurrentAmmo, int32 NewReserveAmmo)
{
	if (CurrentAmmo == NewCurrentAmmo && ReserveAmmo == NewReserveAmmo) return;

	CurrentAmmo = NewCurrentAmmo;
	ReserveAmmo = NewReserveAmmo;
	if (!bPresentationActive) return;

	OnPresentedAmmoChangedNative.Broadcast(CurrentAmmo, ReserveAmmo);
}

void UPlayerWeaponPresentationComponent::HandleReloadPhaseChanged(EWeaponReloadPhase Phase)
{
	LastReloadPhase = Phase;
	bHasObservedReloadPhase = true;
	if (!bPresentationActive) return;

	OnPresentedReloadPhaseChangedNative.Broadcast(LastReloadPhase);
}

bool UPlayerWeaponPresentationComponent::CanRouteLocalPresentation() const
{
	const UWorld* World = GetWorld();
	const AExtractionPlayer* Player = PlayerOwner.Get();
	return IsValid(World)
		&& World->GetNetMode() != NM_DedicatedServer
		&& IsValid(Player)
		&& Player->IsLocallyControlled();
}

void UPlayerWeaponPresentationComponent::SetPresentationActive(bool bNewActive)
{
	if (bPresentationActive == bNewActive) return;

	bPresentationActive = bNewActive;
	if (bPresentationActive)
		SynchronizeWeaponView();
	else
	{
		CancelPendingOpticViewLoad();
		CancelPendingWeaponViewLoad();
		DestroyWeaponView();
		ClearResolvedSight();
		ClearLegacyWeaponPresentation();
	}
	OnPresentationActiveChangedNative.Broadcast(bPresentationActive);
}

void UPlayerWeaponPresentationComponent::PublishPresentationSnapshot()
{
	if (!bPresentationActive) return;

	AWeaponBase* Weapon = CurrentWeapon.Get();
	OnPresentedWeaponChangedNative.Broadcast(Weapon);
	OnPresentedAimingChangedNative.Broadcast(bIsAiming);
	OnPresentedTriggerChangedNative.Broadcast(bTriggerHeld);
	OnPresentedAmmoChangedNative.Broadcast(CurrentAmmo, ReserveAmmo);
	if (bHasObservedReloadPhase)
		OnPresentedReloadPhaseChangedNative.Broadcast(LastReloadPhase);

	RouteLegacyWeaponChanged(Weapon);
	RouteLegacyAimingChanged(bIsAiming);
	SynchronizeWeaponView();
}

void UPlayerWeaponPresentationComponent::RouteLegacyWeaponChanged(AWeaponBase* Weapon)
{
	AExtractionPlayer* Player = PlayerOwner.Get();
	if (!CanRouteLocalPresentation() || !IsValid(Player)) return;
	if (!IsValid(Weapon))
	{
		ClearLegacyWeaponPresentation();
		return;
	}
	if (LastRoutedWeapon.Get() == Weapon) return;

	ClearLegacyWeaponPresentation();
	if (Player->GetIsDBNO()) return;

	LastRoutedWeapon = Weapon;
	Player->OnWeaponEquipped(Weapon);
	bLegacyPresentationCleared = false;
}

void UPlayerWeaponPresentationComponent::ClearLegacyWeaponPresentation()
{
	if (bLegacyPresentationCleared) return;
	if (AExtractionPlayer* Player = PlayerOwner.Get())
		Player->ClearLegacyWeaponPresentation();
	LastRoutedWeapon.Reset();
	bLegacyPresentationCleared = true;
}

void UPlayerWeaponPresentationComponent::RouteLegacyAimingChanged(bool bNewAiming)
{
	AExtractionPlayer* Player = PlayerOwner.Get();
	if (!CanRouteLocalPresentation() || !IsValid(Player)) return;
	if (bHasRoutedAiming && bLastRoutedAiming == bNewAiming) return;

	bLastRoutedAiming = bNewAiming;
	bHasRoutedAiming = true;
	Player->OnADSChanged(bNewAiming);
}

bool UPlayerWeaponPresentationComponent::ResolveViewPlacement(
	const APlayerWeaponView& View,
	EPlayerWeaponSeatPolicy SeatPolicy,
	FTransform& OutPlacement)
{
	OutPlacement = FTransform::Identity;
	if (SeatPolicy == EPlayerWeaponSeatPolicy::LegacyViewRoot) return true;
	if (SeatPolicy != EPlayerWeaponSeatPolicy::WeaponSeatMarker) return false;

	const USceneComponent* Root = View.GetRootComponent();
	const UPlayerWeaponMarkerComponent* Seat = View.GetWeaponSeatMarker();
	if (!IsValid(Root) || !IsValid(Seat)
		|| Seat->GetMarkerKind() != EPlayerWeaponMarkerKind::WeaponSeat)
		return false;

	const FTransform SeatRelative = Seat->GetComponentTransform().GetRelativeTransform(
		Root->GetComponentTransform());
	if (!Root->GetComponentScale().Equals(FVector::OneVector, KINDA_SMALL_NUMBER)
		|| !SeatRelative.GetScale3D().Equals(
			FVector::OneVector, KINDA_SMALL_NUMBER))
		return false;

	const FTransform RigidSeat(
		SeatRelative.GetRotation().GetNormalized(),
		SeatRelative.GetLocation(),
		FVector::OneVector);
	const FTransform InverseSeat = RigidSeat.Inverse();
	if (InverseSeat.ContainsNaN()) return false;
	OutPlacement = FTransform(
		InverseSeat.GetRotation(), InverseSeat.GetLocation(), FVector::OneVector);
	return true;
}

UPlayerWeaponPresentationProfile*
UPlayerWeaponPresentationComponent::ResolveProfile(const AWeaponBase* Weapon) const
{
	if (!IsValid(Weapon)) return nullptr;
	const UWeaponDataAsset* Data = Weapon->GetWeaponData();
	return IsValid(Data) ? Data->GetPlayerPresentationProfile() : nullptr;
}

void UPlayerWeaponPresentationComponent::ResetCachedViewPlacement()
{
	CachedPlacementViewClassPath.Reset();
	CachedViewPlacement = FTransform::Identity;
	bHasCachedViewPlacement = false;
}

bool UPlayerWeaponPresentationComponent::AttachWeaponView(
	APlayerWeaponView& View) const
{
	const AExtractionPlayer* Player = PlayerOwner.Get();
	USkeletalMeshComponent* Mesh =
		IsValid(Player) ? Player->GetMesh() : nullptr;
	if (!IsValid(Mesh) || !Mesh->DoesSocketExist(PlayerWeaponHandSocket))
	{
		UE_LOG(LogExtraction, Warning,
			TEXT("%s cannot seat a player weapon view: socket %s is missing."),
			*GetNameSafe(Player), *PlayerWeaponHandSocket.ToString());
		return false;
	}
	if (!View.AttachToComponent(
		Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		PlayerWeaponHandSocket))
		return false;

	USceneComponent* Root = View.GetRootComponent();
	if (!IsValid(Root)) return false;
	Root->SetRelativeLocationAndRotation(
		CachedViewPlacement.GetLocation(), CachedViewPlacement.GetRotation());
	return true;
}
bool UPlayerWeaponPresentationComponent::SpawnWeaponView(
	AWeaponBase& Weapon,
	const UPlayerWeaponPresentationProfile& Profile,
	TSubclassOf<APlayerWeaponView> ViewClass,
	const FSoftObjectPath& ViewClassPath)
{
	APlayerWeaponView* View = CreateWeaponView(ViewClass);
	if (!IsValid(View)) return false;
	if (!CacheViewPlacement(*View, Profile, ViewClassPath)
		|| !AttachWeaponView(*View))
	{
		View->Destroy();
		return false;
	}

	ActiveWeaponView = View;
	ViewWeapon = &Weapon;
	Weapon.SetFirstPersonMuzzle(View->GetMuzzleMarker());
	ApplyWeaponViewVisibility();
	OnPresentedWeaponViewChangedNative.Broadcast(View);
	SynchronizeWeaponSight();
	return true;
}

APlayerWeaponView* UPlayerWeaponPresentationComponent::CreateWeaponView(
	TSubclassOf<APlayerWeaponView> ViewClass)
{
	UWorld* World = GetWorld();
	AExtractionPlayer* Player = PlayerOwner.Get();
	if (!IsValid(World) || !IsValid(Player) || !ViewClass) return nullptr;

	FActorSpawnParameters Params;
	Params.Owner = Player;
	Params.Instigator = Player;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlayerWeaponView* View = World->SpawnActor<APlayerWeaponView>(
		ViewClass, FTransform::Identity, Params);
#if WITH_DEV_AUTOMATION_TESTS
	if (IsValid(View) && WeaponViewCreatedHookForTesting)
		WeaponViewCreatedHookForTesting(*View);
#endif
	if (IsValid(View) && View->InitializeView()) return View;
	if (IsValid(View)) View->Destroy();
	return nullptr;
}

bool UPlayerWeaponPresentationComponent::CacheViewPlacement(
	const APlayerWeaponView& View,
	const UPlayerWeaponPresentationProfile& Profile,
	const FSoftObjectPath& ViewClassPath)
{
	if (bHasCachedViewPlacement) return true;
	if (!ResolveViewPlacement(View, Profile.SeatPolicy, CachedViewPlacement))
		return false;
	CachedPlacementViewClassPath = ViewClassPath;
	bHasCachedViewPlacement = true;
	return true;
}

void UPlayerWeaponPresentationComponent::DestroyWeaponView()
{
	CancelPendingOpticViewLoad();
	DestroyOpticView();
	SelectedOpticId = NAME_None;
	ClearResolvedSight();
	const bool bHadView = ActiveWeaponView != nullptr;
	if (AWeaponBase* Weapon = ViewWeapon.Get())
		Weapon->SetFirstPersonMuzzle(nullptr);
	ViewWeapon.Reset();

	if (IsValid(ActiveWeaponView))
	{
		ActiveWeaponView->SetActorHiddenInGame(true);
		ActiveWeaponView->ReleaseView();
		ActiveWeaponView->DetachFromActor(
			FDetachmentTransformRules::KeepWorldTransform);
		ActiveWeaponView->Destroy();
	}
	ActiveWeaponView = nullptr;
	if (bHadView)
		OnPresentedWeaponViewChangedNative.Broadcast(nullptr);
}

void UPlayerWeaponPresentationComponent::SynchronizeWeaponView()
{
	AWeaponBase* Weapon = CurrentWeapon.Get();
	const UPlayerWeaponPresentationProfile* Profile = ResolveProfile(Weapon);
	if (!bPresentationActive || !IsValid(Weapon) || !IsValid(Profile)
		|| Profile->ViewClass.IsNull())
	{
		CancelPendingWeaponViewLoad();
		DestroyWeaponView();
		ClearResolvedSight();
		CachedLoadedViewClass = nullptr;
		CachedLoadedViewClassPath.Reset();
		return;
	}

	const FSoftObjectPath ViewClassPath = Profile->ViewClass.ToSoftObjectPath();
	if (CachedLoadedViewClassPath != ViewClassPath)
	{
		CachedLoadedViewClass = nullptr;
		CachedLoadedViewClassPath.Reset();
	}
	UClass* LoadedClass = IsValid(CachedLoadedViewClass)
		? CachedLoadedViewClass.Get()
		: Profile->ViewClass.Get();
	if (!IsValid(LoadedClass))
	{
		RequestWeaponViewLoad(*Weapon, *Profile);
		return;
	}
	if (!LoadedClass->IsChildOf(APlayerWeaponView::StaticClass()))
	{
		UE_LOG(LogExtraction, Warning,
			TEXT("%s resolved an invalid player weapon view class for %s."),
			*GetNameSafe(GetOwner()), *GetNameSafe(Weapon));
		CancelPendingWeaponViewLoad();
		DestroyWeaponView();
		ClearResolvedSight();
		CachedLoadedViewClass = nullptr;
		CachedLoadedViewClassPath.Reset();
		return;
	}
	CachedLoadedViewClass = LoadedClass;
	CachedLoadedViewClassPath = ViewClassPath;
	CancelPendingWeaponViewLoad();
	TSubclassOf<APlayerWeaponView> ViewClass = LoadedClass;

	if (IsValid(ActiveWeaponView)
		&& !ActiveWeaponView->IsActorBeingDestroyed()
		&& ViewWeapon.Get() == Weapon
		&& ActiveWeaponView->GetClass() == ViewClass.Get())
	{
		Weapon->SetFirstPersonMuzzle(ActiveWeaponView->GetMuzzleMarker());
		ApplyWeaponViewVisibility();
		SynchronizeWeaponSight();
		return;
	}

	DestroyWeaponView();
	if (CachedPlacementViewClassPath != ViewClassPath)
		ResetCachedViewPlacement();
	if (!SpawnWeaponView(*Weapon, *Profile, ViewClass, ViewClassPath))
		ClearResolvedSight();
}

void UPlayerWeaponPresentationComponent::RequestWeaponViewLoad(
	AWeaponBase& Weapon,
	const UPlayerWeaponPresentationProfile& Profile)
{
	const FSoftObjectPath ViewClassPath = Profile.ViewClass.ToSoftObjectPath();
	if (!ViewClassPath.IsValid())
	{
		CancelPendingWeaponViewLoad();
		DestroyWeaponView();
		return;
	}
	if (PendingViewLoad.IsValid()
		&& PendingViewLoadWeapon.Get() == &Weapon
		&& PendingViewClassPath == ViewClassPath)
		return;

	DestroyWeaponView();
	CancelPendingWeaponViewLoad();
	PendingViewLoadWeapon = &Weapon;
	PendingViewClassPath = ViewClassPath;
	const uint32 RequestGeneration = ViewLoadGeneration;
	PendingViewLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		ViewClassPath,
		FStreamableDelegate::CreateUObject(
			this,
			&UPlayerWeaponPresentationComponent::HandleWeaponViewClassLoaded,
			TWeakObjectPtr<AWeaponBase>(&Weapon),
			ViewClassPath,
			RequestGeneration));
	if (PendingViewLoad.IsValid()) return;

	PendingViewLoadWeapon.Reset();
	PendingViewClassPath.Reset();
	UE_LOG(LogExtraction, Warning,
		TEXT("%s could not request player weapon view class %s."),
		*GetNameSafe(GetOwner()), *ViewClassPath.ToString());
}

void UPlayerWeaponPresentationComponent::HandleWeaponViewClassLoaded(
	TWeakObjectPtr<AWeaponBase> RequestedWeapon,
	FSoftObjectPath RequestedClassPath,
	uint32 RequestGeneration)
{
	if (RequestGeneration != ViewLoadGeneration
		|| PendingViewLoadWeapon != RequestedWeapon
		|| PendingViewClassPath != RequestedClassPath)
		return;

	TSharedPtr<FStreamableHandle> CompletedHandle = PendingViewLoad;
	PendingViewLoad.Reset();
	PendingViewLoadWeapon.Reset();
	PendingViewClassPath.Reset();
	if (!CompletedHandle.IsValid()) return;

	AWeaponBase* Weapon = RequestedWeapon.Get();
	const UPlayerWeaponPresentationProfile* Profile = ResolveProfile(Weapon);
	if (!bPresentationActive || !CanRouteLocalPresentation()
		|| CurrentWeapon.Get() != Weapon
		|| !IsValid(Weapon) || !IsValid(Profile)
		|| Profile->ViewClass.ToSoftObjectPath() != RequestedClassPath)
		return;

	UClass* LoadedClass = Profile->ViewClass.Get();
	if (!IsValid(LoadedClass)
		|| !LoadedClass->IsChildOf(APlayerWeaponView::StaticClass()))
	{
		UE_LOG(LogExtraction, Warning,
			TEXT("%s failed to asynchronously load player weapon view class %s."),
			*GetNameSafe(GetOwner()), *RequestedClassPath.ToString());
		return;
	}

	CachedLoadedViewClass = LoadedClass;
	CachedLoadedViewClassPath = RequestedClassPath;
	TSubclassOf<APlayerWeaponView> ViewClass = LoadedClass;
	if (CachedPlacementViewClassPath != RequestedClassPath)
		ResetCachedViewPlacement();
	if (!SpawnWeaponView(*Weapon, *Profile, ViewClass, RequestedClassPath))
		ClearResolvedSight();
}

void UPlayerWeaponPresentationComponent::CancelPendingWeaponViewLoad()
{
	++ViewLoadGeneration;
	if (PendingViewLoad.IsValid())
		PendingViewLoad->CancelHandle();
	PendingViewLoad.Reset();
	PendingViewLoadWeapon.Reset();
	PendingViewClassPath.Reset();
}

void UPlayerWeaponPresentationComponent::InitializeSightSelection(
	AWeaponBase& Weapon,
	UPlayerWeaponPresentationProfile& Profile)
{
	if (SightWeapon.Get() == &Weapon && SightProfile.Get() == &Profile) return;

	CancelPendingOpticViewLoad();
	DestroyOpticView();
	SightWeapon = &Weapon;
	SightProfile = &Profile;
	RequestedOpticId = Profile.DefaultOpticId;
	SelectedOpticId = NAME_None;
	ResolvedSight = FPlayerWeaponResolvedSight();
	bForceNextSightPublish = true;
}

void UPlayerWeaponPresentationComponent::SynchronizeWeaponSight()
{
	AWeaponBase* Weapon = CurrentWeapon.Get();
	UPlayerWeaponPresentationProfile* Profile = ResolveProfile(Weapon);
	if (!bPresentationActive || !IsValid(Weapon) || !IsValid(Profile)
		|| !IsUsableWeaponView(ActiveWeaponView))
	{
		CancelPendingOpticViewLoad();
		DestroyOpticView();
		ClearResolvedSight();
		return;
	}

	InitializeSightSelection(*Weapon, *Profile);
	if (RequestedOpticId.IsNone())
	{
		if (!CommitIronSight(*Profile)) ClearResolvedSight();
		return;
	}

	const UPlayerWeaponAttachmentDefinition* Selected =
		Profile->FindAttachmentDefinition(RequestedOpticId);
	if (IsUsableOpticDefinition(*Profile, Selected))
	{
		if (!IsValid(Selected->ViewClass.Get()) && !ResolvedSight.bIsValid)
			CommitIronSightFallback(*Profile);
		if (SelectOpticDefinition(*Weapon, *Profile, *Selected)) return;
	}

	const UPlayerWeaponAttachmentDefinition* Default =
		Profile->FindAttachmentDefinition(Profile->DefaultOpticId);
	if (Profile->DefaultOpticId != RequestedOpticId
		&& IsUsableOpticDefinition(*Profile, Default))
	{
		RequestedOpticId = Profile->DefaultOpticId;
		if (!IsValid(Default->ViewClass.Get()) && !ResolvedSight.bIsValid)
			CommitIronSightFallback(*Profile);
		if (SelectOpticDefinition(*Weapon, *Profile, *Default)) return;
	}
	if (CommitIronSight(*Profile)) return;

	CancelPendingOpticViewLoad();
	DestroyOpticView();
	RequestedOpticId = NAME_None;
	SelectedOpticId = NAME_None;
	ClearResolvedSight();
}

bool UPlayerWeaponPresentationComponent::SetSelectedOpticId(FName OpticId)
{
	AWeaponBase* Weapon = CurrentWeapon.Get();
	UPlayerWeaponPresentationProfile* Profile = ResolveProfile(Weapon);
	if (!bPresentationActive || !IsValid(Weapon) || !IsValid(Profile)
		|| !IsUsableWeaponView(ActiveWeaponView))
		return false;

	InitializeSightSelection(*Weapon, *Profile);
	if (OpticId.IsNone()) return CommitIronSight(*Profile);

	const UPlayerWeaponAttachmentDefinition* Definition =
		Profile->FindAttachmentDefinition(OpticId);
	if (!IsUsableOpticDefinition(*Profile, Definition)) return false;

	const FName PreviousRequest = RequestedOpticId;
	RequestedOpticId = OpticId;
	if (SelectOpticDefinition(*Weapon, *Profile, *Definition)) return true;
	RequestedOpticId = PreviousRequest;
	if (!PreviousRequest.IsNone() && PreviousRequest != SelectedOpticId)
	{
		const UPlayerWeaponAttachmentDefinition* PreviousDefinition =
			Profile->FindAttachmentDefinition(PreviousRequest);
		if (!IsUsableOpticDefinition(*Profile, PreviousDefinition)
			|| !SelectOpticDefinition(
				*Weapon, *Profile, *PreviousDefinition))
			RecoverFromFailedOpticSelection(*Profile, PreviousRequest);
	}
	return false;
}

bool UPlayerWeaponPresentationComponent::CommitIronSight(
	const UPlayerWeaponPresentationProfile& Profile)
{
	FPlayerWeaponResolvedSight Candidate;
	if (!ResolveIronSight(Profile, Candidate)) return false;

	CancelPendingOpticViewLoad();
	DestroyOpticView();
	RequestedOpticId = NAME_None;
	SelectedOpticId = NAME_None;
	CommitResolvedSight(Candidate);
	return true;
}

bool UPlayerWeaponPresentationComponent::CommitIronSightFallback(
	const UPlayerWeaponPresentationProfile& Profile)
{
	FPlayerWeaponResolvedSight Candidate;
	if (!ResolveIronSight(Profile, Candidate)) return false;

	DestroyOpticView();
	SelectedOpticId = NAME_None;
	CommitResolvedSight(Candidate);
	return true;
}

bool UPlayerWeaponPresentationComponent::SelectOpticDefinition(
	AWeaponBase& Weapon,
	UPlayerWeaponPresentationProfile& Profile,
	const UPlayerWeaponAttachmentDefinition& Definition)
{
	const FSoftObjectPath ViewClassPath =
		Definition.ViewClass.ToSoftObjectPath();
	if (ActiveOpticView
		&& (!IsValid(ActiveOpticView)
			|| ActiveOpticView->IsActorBeingDestroyed()))
	{
		DestroyOpticView();
		SelectedOpticId = NAME_None;
		ClearResolvedSight();
		CommitIronSightFallback(Profile);
	}
	if (IsValid(ActiveOpticView)
		&& !ActiveOpticView->IsActorBeingDestroyed()
		&& SelectedOpticId == Definition.AttachmentId
		&& ActiveOpticClassPath == ViewClassPath
		&& ResolvedSight.bIsValid
		&& ResolvedSight.bUsesOptic
		&& ResolvedSight.OpticId == Definition.AttachmentId)
	{
		CancelPendingOpticViewLoad();
		return true;
	}

	UClass* LoadedClass = Definition.ViewClass.Get();
	if (!IsValid(LoadedClass))
	{
		RequestOpticViewLoad(Weapon, Profile, Definition);
		return PendingOpticLoad.IsValid();
	}
	if (!LoadedClass->IsChildOf(APlayerWeaponAttachmentView::StaticClass()))
		return false;

	CancelPendingOpticViewLoad();
	TSubclassOf<APlayerWeaponAttachmentView> ViewClass = LoadedClass;
	return SpawnAndCommitOptic(
		Weapon, Profile, Definition, ViewClass, ViewClassPath);
}

bool UPlayerWeaponPresentationComponent::SpawnAndCommitOptic(
	AWeaponBase& Weapon,
	UPlayerWeaponPresentationProfile& Profile,
	const UPlayerWeaponAttachmentDefinition& Definition,
	TSubclassOf<APlayerWeaponAttachmentView> ViewClass,
	const FSoftObjectPath& ViewClassPath)
{
	APlayerWeaponAttachmentView* Candidate = CreateOpticView(ViewClass);
	if (!IsValid(Candidate)) return false;

	FPlayerWeaponResolvedSight CandidateSight;
	if (!PlaceOpticView(*Candidate)
		|| !ResolveOpticSight(Definition, *Candidate, Profile, CandidateSight))
	{
		DestroyAttachmentView(Candidate);
		return false;
	}

	APlayerWeaponAttachmentView* Previous = ActiveOpticView;
	ActiveOpticView = Candidate;
	ActiveOpticClassPath = ViewClassPath;
	SightWeapon = &Weapon;
	SightProfile = &Profile;
	SelectedOpticId = Definition.AttachmentId;
	DestroyAttachmentView(Previous);
	ApplyWeaponViewVisibility();
	CommitResolvedSight(CandidateSight);
	return true;
}

APlayerWeaponAttachmentView*
UPlayerWeaponPresentationComponent::CreateOpticView(
	TSubclassOf<APlayerWeaponAttachmentView> ViewClass)
{
	UWorld* World = GetWorld();
	AExtractionPlayer* Player = PlayerOwner.Get();
	if (!IsValid(World) || !IsValid(Player) || !ViewClass) return nullptr;

	FActorSpawnParameters Params;
	Params.Owner = Player;
	Params.Instigator = Player;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlayerWeaponAttachmentView* View =
		World->SpawnActor<APlayerWeaponAttachmentView>(
			ViewClass, FTransform::Identity, Params);
	if (!IsValid(View)) return nullptr;
	View->SetActorHiddenInGame(true);
	if (View->InitializeView()) return View;
	DestroyAttachmentView(View);
	return nullptr;
}

bool UPlayerWeaponPresentationComponent::PlaceOpticView(
	APlayerWeaponAttachmentView& View) const
{
	if (!IsUsableWeaponView(ActiveWeaponView)) return false;
	USceneComponent* WeaponRoot = ActiveWeaponView->GetRootComponent();
	USceneComponent* AttachmentRoot = View.GetRootComponent();
	const UPlayerWeaponMarkerComponent* WeaponMount =
		ActiveWeaponView->GetOpticMountMarker();
	const UPlayerWeaponMarkerComponent* AttachmentMount =
		View.GetAttachmentMountMarker();
	if (!IsValid(WeaponRoot) || !IsValid(AttachmentRoot)
		|| !IsValid(WeaponMount) || !IsValid(AttachmentMount)
		|| WeaponMount->GetMarkerKind() != EPlayerWeaponMarkerKind::OpticMount
		|| AttachmentMount->GetMarkerKind()
			!= EPlayerWeaponMarkerKind::AttachmentMount)
		return false;

	FTransform Placement;
	if (!FPlayerWeaponADSResolver::ResolveAttachmentRootTransform(
		WeaponMount->GetComponentTransform().GetRelativeTransform(
			WeaponRoot->GetComponentTransform()),
		AttachmentMount->GetComponentTransform().GetRelativeTransform(
			AttachmentRoot->GetComponentTransform()),
		Placement))
		return false;
	if (!View.AttachToComponent(
		WeaponRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale))
		return false;
	AttachmentRoot->SetRelativeTransform(Placement);
	return true;
}

bool UPlayerWeaponPresentationComponent::ResolveIronSight(
	const UPlayerWeaponPresentationProfile& Profile,
	FPlayerWeaponResolvedSight& OutSight) const
{
	OutSight = FPlayerWeaponResolvedSight();
	if (!Profile.MarkerRequirements.bRequireIronRear
		|| !Profile.MarkerRequirements.bRequireIronFront
		|| !IsUsableWeaponView(ActiveWeaponView))
		return false;
	const UPlayerWeaponMarkerComponent* Rear =
		ActiveWeaponView->GetIronRearMarker();
	const UPlayerWeaponMarkerComponent* Front =
		ActiveWeaponView->GetIronFrontMarker();
	if (!IsValid(Rear) || !IsValid(Front)
		|| Rear->GetMarkerKind() != EPlayerWeaponMarkerKind::IronRear
		|| Front->GetMarkerKind() != EPlayerWeaponMarkerKind::IronFront)
		return false;

	FTransform HandSocketWorld;
	if (!GetHandSocketWorld(HandSocketWorld)) return false;
	return FPlayerWeaponADSResolver::ResolveIrons(
		Rear->GetComponentTransform().GetRelativeTransform(HandSocketWorld),
		Front->GetComponentTransform().GetRelativeTransform(HandSocketWorld),
		Profile.ADSDefaults,
		OutSight);
}

bool UPlayerWeaponPresentationComponent::ResolveOpticSight(
	const UPlayerWeaponAttachmentDefinition& Definition,
	const APlayerWeaponAttachmentView& View,
	const UPlayerWeaponPresentationProfile& Profile,
	FPlayerWeaponResolvedSight& OutSight) const
{
	OutSight = FPlayerWeaponResolvedSight();
	const UPlayerWeaponMarkerComponent* AimPoint = View.GetAimPointMarker();
	if (!IsValid(AimPoint)
		|| AimPoint->GetMarkerKind() != EPlayerWeaponMarkerKind::AimPoint)
		return false;

	FTransform HandSocketWorld;
	if (!GetHandSocketWorld(HandSocketWorld)) return false;
	return FPlayerWeaponADSResolver::ResolveOptic(
		Definition.AttachmentId,
		AimPoint->GetComponentTransform().GetRelativeTransform(HandSocketWorld),
		Profile.ADSDefaults,
		Definition.OpticOverride,
		OutSight);
}

bool UPlayerWeaponPresentationComponent::GetHandSocketWorld(
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	const AExtractionPlayer* Player = PlayerOwner.Get();
	const USkeletalMeshComponent* Mesh =
		IsValid(Player) ? Player->GetMesh() : nullptr;
	if (!IsValid(Mesh) || !Mesh->DoesSocketExist(PlayerWeaponHandSocket))
		return false;
	OutTransform = Mesh->GetSocketTransform(PlayerWeaponHandSocket, RTS_World);
	return !OutTransform.ContainsNaN();
}

bool UPlayerWeaponPresentationComponent::GetResolvedAimSourceWorld(
	FTransform& OutAimSourceWorld) const
{
	OutAimSourceWorld = FTransform::Identity;
	if (!ResolvedSight.bIsValid
		|| ResolvedSight.AimSourceInHandSpace.ContainsNaN())
		return false;

	FTransform HandSocketWorld;
	if (!GetHandSocketWorld(HandSocketWorld)) return false;
	OutAimSourceWorld =
		ResolvedSight.AimSourceInHandSpace * HandSocketWorld;
	return !OutAimSourceWorld.ContainsNaN();
}

void UPlayerWeaponPresentationComponent::CommitResolvedSight(
	const FPlayerWeaponResolvedSight& Sight)
{
	const bool bPublish = bForceNextSightPublish
		|| !ResolvedSightsEqual(ResolvedSight, Sight);
	ResolvedSight = Sight;
	bForceNextSightPublish = false;
	if (bPublish) OnPresentedSightChangedNative.Broadcast(ResolvedSight);
}

void UPlayerWeaponPresentationComponent::ClearResolvedSight()
{
	const FPlayerWeaponResolvedSight InvalidSight;
	const bool bPublish = bForceNextSightPublish
		|| !ResolvedSightsEqual(ResolvedSight, InvalidSight);
	ResolvedSight = InvalidSight;
	bForceNextSightPublish = false;
	if (bPublish) OnPresentedSightChangedNative.Broadcast(ResolvedSight);
}

void UPlayerWeaponPresentationComponent::RecoverFromFailedOpticSelection(
	const UPlayerWeaponPresentationProfile& Profile,
	FName FailedOpticId)
{
	if (RequestedOpticId != FailedOpticId) return;

	const UPlayerWeaponAttachmentDefinition* CommittedDefinition =
		Profile.FindAttachmentDefinition(SelectedOpticId);
	const bool bCanKeepCommittedOptic =
		IsValid(ActiveOpticView)
		&& !ActiveOpticView->IsActorBeingDestroyed()
		&& ResolvedSight.bIsValid
		&& ResolvedSight.bUsesOptic
		&& ResolvedSight.OpticId == SelectedOpticId
		&& IsUsableOpticDefinition(Profile, CommittedDefinition)
		&& ActiveOpticClassPath
			== CommittedDefinition->ViewClass.ToSoftObjectPath();
	if (bCanKeepCommittedOptic)
	{
		RequestedOpticId = SelectedOpticId;
		return;
	}

	DestroyOpticView();
	RequestedOpticId = NAME_None;
	SelectedOpticId = NAME_None;
	if (!CommitIronSightFallback(Profile)) ClearResolvedSight();
}

void UPlayerWeaponPresentationComponent::DestroyOpticView()
{
	APlayerWeaponAttachmentView* Previous = ActiveOpticView;
	ActiveOpticView = nullptr;
	ActiveOpticClassPath.Reset();
	DestroyAttachmentView(Previous);
}

void UPlayerWeaponPresentationComponent::RequestOpticViewLoad(
	AWeaponBase& Weapon,
	UPlayerWeaponPresentationProfile& Profile,
	const UPlayerWeaponAttachmentDefinition& Definition)
{
	const FSoftObjectPath ClassPath = Definition.ViewClass.ToSoftObjectPath();
	if (PendingOpticLoad.IsValid()
		&& PendingOpticLoadWeapon.Get() == &Weapon
		&& PendingOpticLoadProfile.Get() == &Profile
		&& PendingOpticId == Definition.AttachmentId
		&& PendingOpticClassPath == ClassPath)
		return;

	CancelPendingOpticViewLoad();
	PendingOpticLoadWeapon = &Weapon;
	PendingOpticLoadProfile = &Profile;
	PendingOpticId = Definition.AttachmentId;
	PendingOpticClassPath = ClassPath;
	const uint32 RequestGeneration = OpticLoadGeneration;
	PendingOpticLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		ClassPath,
		FStreamableDelegate::CreateUObject(
			this,
			&UPlayerWeaponPresentationComponent::HandleOpticViewClassLoaded,
			TWeakObjectPtr<AWeaponBase>(&Weapon),
			TWeakObjectPtr<UPlayerWeaponPresentationProfile>(&Profile),
			Definition.AttachmentId,
			ClassPath,
			RequestGeneration));
	if (PendingOpticLoad.IsValid()) return;
	PendingOpticLoadWeapon.Reset();
	PendingOpticLoadProfile.Reset();
	PendingOpticId = NAME_None;
	PendingOpticClassPath.Reset();
	UE_LOG(LogExtraction, Warning,
		TEXT("%s could not request optic view class %s for %s."),
		*GetNameSafe(GetOwner()), *ClassPath.ToString(),
		*Definition.AttachmentId.ToString());
}

void UPlayerWeaponPresentationComponent::HandleOpticViewClassLoaded(
	TWeakObjectPtr<AWeaponBase> RequestedWeapon,
	TWeakObjectPtr<UPlayerWeaponPresentationProfile> RequestedProfile,
	FName LoadedOpticId,
	FSoftObjectPath RequestedClassPath,
	uint32 RequestGeneration)
{
	if (RequestGeneration != OpticLoadGeneration
		|| PendingOpticLoadWeapon != RequestedWeapon
		|| PendingOpticLoadProfile != RequestedProfile
		|| PendingOpticId != LoadedOpticId
		|| PendingOpticClassPath != RequestedClassPath)
		return;

	TSharedPtr<FStreamableHandle> CompletedHandle = PendingOpticLoad;
	PendingOpticLoad.Reset();
	PendingOpticLoadWeapon.Reset();
	PendingOpticLoadProfile.Reset();
	PendingOpticId = NAME_None;
	PendingOpticClassPath.Reset();
	AWeaponBase* Weapon = RequestedWeapon.Get();
	UPlayerWeaponPresentationProfile* Profile = RequestedProfile.Get();
	if (!CompletedHandle.IsValid() || !bPresentationActive
		|| !CanRouteLocalPresentation() || CurrentWeapon.Get() != Weapon
		|| !IsValid(Weapon) || !IsValid(Profile)
		|| SightProfile.Get() != Profile
		|| !IsUsableWeaponView(ActiveWeaponView))
		return;

	const UPlayerWeaponAttachmentDefinition* Definition =
		IsValid(Profile)
			? Profile->FindAttachmentDefinition(LoadedOpticId)
			: nullptr;
	if (!IsUsableOpticDefinition(*Profile, Definition)
		|| Definition->ViewClass.ToSoftObjectPath() != RequestedClassPath)
	{
		UE_LOG(LogExtraction, Warning,
			TEXT("%s rejected stale or invalid optic definition %s after load."),
			*GetNameSafe(GetOwner()), *LoadedOpticId.ToString());
		RecoverFromFailedOpticSelection(*Profile, LoadedOpticId);
		return;
	}
	UClass* LoadedClass = Definition->ViewClass.Get();
	if (!IsValid(LoadedClass)
		|| !LoadedClass->IsChildOf(APlayerWeaponAttachmentView::StaticClass()))
	{
		UE_LOG(LogExtraction, Warning,
			TEXT("%s failed to asynchronously load optic view class %s."),
			*GetNameSafe(GetOwner()), *RequestedClassPath.ToString());
		RecoverFromFailedOpticSelection(*Profile, LoadedOpticId);
		return;
	}
	TSubclassOf<APlayerWeaponAttachmentView> ViewClass = LoadedClass;
	if (!SpawnAndCommitOptic(
		*Weapon, *Profile, *Definition, ViewClass, RequestedClassPath))
	{
		UE_LOG(LogExtraction, Warning,
			TEXT("%s could not place or resolve optic %s."),
			*GetNameSafe(GetOwner()), *LoadedOpticId.ToString());
		RecoverFromFailedOpticSelection(*Profile, LoadedOpticId);
	}
}

void UPlayerWeaponPresentationComponent::CancelPendingOpticViewLoad()
{
	++OpticLoadGeneration;
	if (PendingOpticLoad.IsValid()) PendingOpticLoad->CancelHandle();
	PendingOpticLoad.Reset();
	PendingOpticLoadWeapon.Reset();
	PendingOpticLoadProfile.Reset();
	PendingOpticId = NAME_None;
	PendingOpticClassPath.Reset();
}

void UPlayerWeaponPresentationComponent::SetWeaponViewHidden(bool bHidden)
{
	SetWeaponViewSuppressed(
		EPlayerWeaponViewSuppressionReason::Manual, bHidden);
}

void UPlayerWeaponPresentationComponent::SetWeaponViewSuppressed(
	EPlayerWeaponViewSuppressionReason Reason, bool bSuppressed)
{
	if (Reason == EPlayerWeaponViewSuppressionReason::None) return;
	if (bSuppressed)
		ViewSuppressionReasons |= Reason;
	else
		ViewSuppressionReasons &= ~Reason;
	ApplyWeaponViewVisibility();
}

void UPlayerWeaponPresentationComponent::ApplyWeaponViewVisibility()
{
	const bool bHidden =
		ViewSuppressionReasons != EPlayerWeaponViewSuppressionReason::None;
	if (IsValid(ActiveWeaponView))
		ActiveWeaponView->SetActorHiddenInGame(bHidden);
	if (IsValid(ActiveOpticView))
		ActiveOpticView->SetActorHiddenInGame(bHidden);
}

void UPlayerWeaponPresentationComponent::BindVisibilitySources()
{
	if (bVisibilitySourcesBound) return;
	AExtractionPlayer* Player = PlayerOwner.Get();
	if (!IsValid(Player)) return;

	if (UTraversalComponent* Traversal = Player->GetTraversalComponent())
	{
		Traversal->OnTraversalStarted.AddUObject(
			this, &UPlayerWeaponPresentationComponent::HandleTraversalStarted);
		Traversal->OnTraversalEnded.AddUObject(
			this, &UPlayerWeaponPresentationComponent::HandleTraversalEnded);
	}
	Player->OnDBNOStateChanged.AddDynamic(
		this, &UPlayerWeaponPresentationComponent::HandleDBNOStateChanged);
	bVisibilitySourcesBound = true;
	SetWeaponViewSuppressed(
		EPlayerWeaponViewSuppressionReason::DBNO, Player->GetIsDBNO());
}

void UPlayerWeaponPresentationComponent::UnbindVisibilitySources()
{
	if (!bVisibilitySourcesBound) return;
	if (AExtractionPlayer* Player = PlayerOwner.Get())
	{
		if (UTraversalComponent* Traversal = Player->GetTraversalComponent())
		{
			Traversal->OnTraversalStarted.RemoveAll(this);
			Traversal->OnTraversalEnded.RemoveAll(this);
		}
		Player->OnDBNOStateChanged.RemoveDynamic(
			this, &UPlayerWeaponPresentationComponent::HandleDBNOStateChanged);
	}
	bVisibilitySourcesBound = false;
}

void UPlayerWeaponPresentationComponent::HandleTraversalStarted(
	ETraversalType /*Type*/, float /*PlayRate*/,
	FVector /*ObstacleLocation*/, FVector /*LandingLocation*/)
{
	SetWeaponViewSuppressed(
		EPlayerWeaponViewSuppressionReason::Traversal, true);
}

void UPlayerWeaponPresentationComponent::HandleTraversalEnded()
{
	SetWeaponViewSuppressed(
		EPlayerWeaponViewSuppressionReason::Traversal, false);
}

void UPlayerWeaponPresentationComponent::HandleDBNOStateChanged(
	bool bNewIsDBNO, float /*BleedoutDuration*/)
{
	SetWeaponViewSuppressed(
		EPlayerWeaponViewSuppressionReason::DBNO, bNewIsDBNO);
	if (bNewIsDBNO)
		ClearLegacyWeaponPresentation();
	else if (bPresentationActive)
		RouteLegacyWeaponChanged(CurrentWeapon.Get());
}
