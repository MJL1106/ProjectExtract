// Player-owned bridge between authoritative weapon state and first-person presentation.

#include "Components/PlayerWeaponPresentationComponent.h"

#include "Character/ExtractionPlayer.h"
#include "Components/WeaponComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Weapon/WeaponBase.h"

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
	RefreshPresentation();
}

void UPlayerWeaponPresentationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetPresentationActive(false);
	UnbindWeaponComponent();
	PlayerOwner.Reset();
	CurrentWeapon.Reset();
	LastRoutedWeapon.Reset();
	bHasRoutedAiming = false;

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
	if (bSameWeapon) return;

	CurrentWeapon = ValidWeapon;
	LastReloadPhase = EWeaponReloadPhase::Completed;
	bHasObservedReloadPhase = false;
	bHasRoutedAiming = false;

	if (!IsValid(ValidWeapon))
		LastRoutedWeapon.Reset();

	if (!bPresentationActive) return;

	OnPresentedWeaponChangedNative.Broadcast(ValidWeapon);
	RouteLegacyWeaponChanged(ValidWeapon);
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
}

void UPlayerWeaponPresentationComponent::RouteLegacyWeaponChanged(AWeaponBase* Weapon)
{
	AExtractionPlayer* Player = PlayerOwner.Get();
	if (!CanRouteLocalPresentation() || !IsValid(Player) || Player->GetIsDBNO() || !IsValid(Weapon)) return;
	if (LastRoutedWeapon.Get() == Weapon) return;

	LastRoutedWeapon = Weapon;
	Player->OnWeaponEquipped(Weapon);
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
