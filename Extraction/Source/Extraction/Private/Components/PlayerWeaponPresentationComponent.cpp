// Player-owned bridge between authoritative weapon state and first-person presentation.

#include "Components/PlayerWeaponPresentationComponent.h"

#include "Character/ExtractionPlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WeaponComponent.h"
#include "Core/Extraction.h"
#include "Data/PlayerWeaponPresentationProfile.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Movement/TraversalComponent.h"
#include "Weapon/PlayerWeaponView.h"
#include "Weapon/WeaponBase.h"

namespace
{
	const FName PlayerWeaponHandSocket(TEXT("ik_hand_gun"));
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
	CancelPendingWeaponViewLoad();
	UnbindVisibilitySources();
	UnbindWeaponComponent();
	PlayerOwner.Reset();
	CurrentWeapon.Reset();
	LastRoutedWeapon.Reset();
	bHasRoutedAiming = false;
	CachedLoadedViewClass = nullptr;
	CachedLoadedViewClassPath.Reset();
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
	CachedLoadedViewClass = nullptr;
	CachedLoadedViewClassPath.Reset();
	ResetCachedViewPlacement();
	LastReloadPhase = EWeaponReloadPhase::Completed;
	bHasObservedReloadPhase = false;
	bHasRoutedAiming = false;

	if (!bPresentationActive) return;

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
		CancelPendingWeaponViewLoad();
		DestroyWeaponView();
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

const UPlayerWeaponPresentationProfile*
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
		return;
	}

	DestroyWeaponView();
	if (CachedPlacementViewClassPath != ViewClassPath)
		ResetCachedViewPlacement();
	SpawnWeaponView(*Weapon, *Profile, ViewClass, ViewClassPath);
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
	SpawnWeaponView(*Weapon, *Profile, ViewClass, RequestedClassPath);
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
