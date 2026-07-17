// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeaponComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "Character/ExtractionPlayerInterface.h"
#include "Companion/CompanionCharacter.h"
#include "Components/CompanionCommandComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Extraction.h"

namespace
{
	static const FName KitWeaponAttachSocket(TEXT("ik_hand_gun"));
}

UWeaponComponent::UWeaponComponent()
	: bIsAiming(false)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UWeaponComponent, CurrentWeapon);
	DOREPLIFETIME_CONDITION(UWeaponComponent, bIsAiming, COND_SkipOwner);
}

void UWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerActor = GetOwner();
	if (!IsValid(OwnerActor)) return;
	OwnerIface = Cast<IExtractionPlayerInterface>(OwnerActor);
	if (!OwnerIface) return;

	// Server spawns default weapon
	if (OwnerActor->HasAuthority() && DefaultWeaponClass)
		EquipWeapon(DefaultWeaponClass);
}

void UWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bNextShotStealthExempt = false;
	bTriggerHeld = false;

	UnbindWeaponEvents(CurrentWeapon);
	if (PreviousWeapon != CurrentWeapon)
		UnbindWeaponEvents(PreviousWeapon);

	OwnerIface = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ---- Weapon Control ----

void UWeaponComponent::EquipWeapon(TSubclassOf<AWeaponBase> WeaponClass)
{
	if (!OwnerIface) return;
	if (!IsValid(OwnerActor) || !OwnerActor->HasAuthority()) return;

	bNextShotStealthExempt = false;
	if (bTriggerHeld)
	{
		bTriggerHeld = false;
		OnTriggerChangedNative.Broadcast(false);
	}

	// Destroy existing weapon
	if (IsValid(CurrentWeapon))
	{
		UnbindWeaponEvents(CurrentWeapon);
		CurrentWeapon->Destroy();
		CurrentWeapon = nullptr;
	}

	if (!WeaponClass)
	{
		OnCurrentWeaponChangedNative.Broadcast(nullptr);
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		OnCurrentWeaponChangedNative.Broadcast(nullptr);
		return;
	}

	ACharacter* OwnerChar = Cast<ACharacter>(OwnerActor);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerActor;
	SpawnParams.Instigator = Cast<APawn>(OwnerActor);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	CurrentWeapon = World->SpawnActor<AWeaponBase>(WeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!IsValid(CurrentWeapon))
	{
		OnCurrentWeaponChangedNative.Broadcast(nullptr);
		return;
	}

	// Attach to the kit IK-rig gun bone (ik_hand_gun) so AC_ProceduralAnimation drives the weapon transform
	if (IsValid(OwnerChar))
	{
		USkeletalMeshComponent* BodyMesh = OwnerChar->GetMesh();
		if (IsValid(BodyMesh))
		{
			CurrentWeapon->AttachToComponent(
				BodyMesh,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				KitWeaponAttachSocket
			);
			SeatWeaponGripSocket();
		}
	}

	CurrentWeapon->InitializeAmmo();
	CurrentWeapon->SetOwnerIsAiming(bIsAiming);
	BindWeaponEvents(CurrentWeapon);
	OnCurrentWeaponChangedNative.Broadcast(CurrentWeapon);

	// Preserve the generic interface path for legacy pawns. AExtractionPlayer uses its
	// dedicated presentation component and inherits the interface's no-op implementation.
	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (IsValid(CurrentWeapon) && IsValid(OwnerActor) && OwnerIface && IsValid(OwnerPawn) && OwnerPawn->IsLocallyControlled() && !OwnerIface->GetIsDBNO())
		OwnerIface->NotifyWeaponEquipped(CurrentWeapon);
}

void UWeaponComponent::StartFire(bool bAuthorityTakedownSnapshot)
{
	if (!IsValid(OwnerActor)) return;
	if (!bTriggerHeld)
	{
		bTriggerHeld = true;
		OnTriggerChangedNative.Broadcast(true);
	}

	// Authority path: trust the caller's snapshot directly (ExtractionPlayer resolved it).
	if (OwnerActor->HasAuthority())
		bNextShotStealthExempt = bAuthorityTakedownSnapshot;

	// Local prediction (skip if we're the server — RPC will execute locally)
	if (!OwnerActor->HasAuthority() && IsValid(CurrentWeapon))
		CurrentWeapon->StartFiring();

	Server_StartFire();
}

void UWeaponComponent::StopFire()
{
	if (!IsValid(OwnerActor)) return;
	if (bTriggerHeld)
	{
		bTriggerHeld = false;
		OnTriggerChangedNative.Broadcast(false);
	}

	bNextShotStealthExempt = false;

	if (!OwnerActor->HasAuthority() && IsValid(CurrentWeapon))
		CurrentWeapon->StopFiring();

	Server_StopFire();
}

void UWeaponComponent::StartReload()
{
	if (!IsValid(OwnerActor)) return;
	if (!OwnerActor->HasAuthority() && IsValid(CurrentWeapon))
		CurrentWeapon->Reload();

	Server_Reload();
}

void UWeaponComponent::SetAiming(bool bNewAiming)
{
	if (bIsAiming == bNewAiming) return;
	bIsAiming = bNewAiming;

	if (IsValid(CurrentWeapon))
		CurrentWeapon->SetOwnerIsAiming(bNewAiming);
	OnAimingChangedNative.Broadcast(bNewAiming);

	Server_SetAiming(bNewAiming);
}

// ---- Server RPCs ----

void UWeaponComponent::Server_StartFire_Implementation()
{
	if (!IsValid(CurrentWeapon)) return;

	// Remote-client pawn: resolve the companion server-side (never trust a client-sent exemption).
	// IsLocallyControlled() is false for remote pawns even inside a Server RPC (HasAuthority()
	// would be true for ALL pawns here). Residual race: the takedown disarm travels via its own
	// RPC so arrival order vs Server_StartFire isn't guaranteed; resolving at RPC receipt is the
	// best server-authoritative approximation.
	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (OwnerPawn && !OwnerPawn->IsLocallyControlled())
		bNextShotStealthExempt = ResolveServerTakedownSnapshot();

	CurrentWeapon->StartFiring();
}

void UWeaponComponent::Server_StopFire_Implementation()
{
	bNextShotStealthExempt = false;

	if (!IsValid(CurrentWeapon)) return;
	CurrentWeapon->StopFiring();
}

void UWeaponComponent::Server_Reload_Implementation()
{
	if (!IsValid(CurrentWeapon)) return;
	CurrentWeapon->Reload();
}

void UWeaponComponent::Server_SetAiming_Implementation(bool bNewAiming)
{
	bIsAiming = bNewAiming;
	if (IsValid(CurrentWeapon))
		CurrentWeapon->SetOwnerIsAiming(bNewAiming);
}

// ---- Weapon Callbacks ----

void UWeaponComponent::OnWeaponFiredCallback()
{
	OnWeaponShotNative.Broadcast();

	// Only the server should multicast
	if (IsValid(OwnerActor) && OwnerActor->HasAuthority())
	{
		// Copy + clear the exemption: only the FIRST shot of a trigger pull is exempt.
		const bool bExempt = bNextShotStealthExempt;
		bNextShotStealthExempt = false;

		OnPlayerWeaponShot.Broadcast(bExempt);
		Multicast_OnFired();
	}
}

void UWeaponComponent::OnWeaponAmmoChangedCallback(int32 CurrentAmmo, int32 ReserveAmmo)
{
	OnWeaponAmmoChangedNative.Broadcast(CurrentAmmo, ReserveAmmo);
}

void UWeaponComponent::OnWeaponReloadPhaseChangedCallback(EWeaponReloadPhase Phase)
{
	OnWeaponReloadPhaseChangedNative.Broadcast(Phase);
}

void UWeaponComponent::BindWeaponEvents(AWeaponBase* Weapon)
{
	if (!IsValid(Weapon)) return;

	if (!Weapon->OnWeaponFired.IsAlreadyBound(this, &UWeaponComponent::OnWeaponFiredCallback))
		Weapon->OnWeaponFired.AddDynamic(this, &UWeaponComponent::OnWeaponFiredCallback);
	if (!Weapon->OnAmmoChanged.IsAlreadyBound(this, &UWeaponComponent::OnWeaponAmmoChangedCallback))
		Weapon->OnAmmoChanged.AddDynamic(this, &UWeaponComponent::OnWeaponAmmoChangedCallback);

	Weapon->OnReloadPhaseChangedNative.RemoveAll(this);
	Weapon->OnReloadPhaseChangedNative.AddUObject(this, &UWeaponComponent::OnWeaponReloadPhaseChangedCallback);
}

void UWeaponComponent::UnbindWeaponEvents(AWeaponBase* Weapon)
{
	if (!Weapon) return;

	Weapon->OnWeaponFired.RemoveDynamic(this, &UWeaponComponent::OnWeaponFiredCallback);
	Weapon->OnAmmoChanged.RemoveDynamic(this, &UWeaponComponent::OnWeaponAmmoChangedCallback);
	Weapon->OnReloadPhaseChangedNative.RemoveAll(this);
}

// ---- Multicast ----

void UWeaponComponent::Multicast_OnFired_Implementation()
{
	// 3P effects (muzzle flash, sound) go here
	// Skip on owning client (already handled locally)
	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (IsValid(OwnerPawn) && OwnerPawn->IsLocallyControlled()) return;
}

// ---- RepNotify ----

void UWeaponComponent::OnRep_CurrentWeapon()
{
	// Late-joiner safety: OwnerActor/OwnerIface may not have been set if replication
	// delivered CurrentWeapon before BeginPlay ran on this component.
	if (!IsValid(OwnerActor))
	{
		OwnerActor = GetOwner();
		OwnerIface = Cast<IExtractionPlayerInterface>(OwnerActor);
	}

	if (PreviousWeapon == CurrentWeapon) return;

	if (PreviousWeapon)
	{
		UnbindWeaponEvents(PreviousWeapon);
		if (IsValid(PreviousWeapon))
			PreviousWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}

	if (!IsValid(CurrentWeapon))
	{
		PreviousWeapon = nullptr;
		OnCurrentWeaponChangedNative.Broadcast(nullptr);
		return;
	}

	PreviousWeapon = CurrentWeapon;
	CurrentWeapon->SetOwnerIsAiming(bIsAiming);
	BindWeaponEvents(CurrentWeapon);

	if (!IsValid(OwnerActor) || !OwnerIface)
	{
		OnCurrentWeaponChangedNative.Broadcast(CurrentWeapon);
		return;
	}

	ACharacter* OwnerChar = Cast<ACharacter>(OwnerActor);
	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);

	if (IsValid(OwnerPawn) && OwnerPawn->IsLocallyControlled())
	{
		// Owner path: attach to the kit IK-rig gun bone (ik_hand_gun) so AC_ProceduralAnimation drives the weapon transform
		if (IsValid(OwnerChar))
		{
			USkeletalMeshComponent* BodyMesh = OwnerChar->GetMesh();
			if (IsValid(BodyMesh))
			{
				CurrentWeapon->AttachToComponent(
					BodyMesh,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale,
					KitWeaponAttachSocket
				);
				SeatWeaponGripSocket();
			}
		}

		// Preserve the generic interface path for legacy pawns.
		if (IsValid(OwnerActor) && OwnerIface && !OwnerIface->GetIsDBNO())
			OwnerIface->NotifyWeaponEquipped(CurrentWeapon);
	}
	else if (IsValid(OwnerChar))
	{
		// Proxy path: attach to 3P skeleton
		USkeletalMeshComponent* Mesh3P = OwnerChar->GetMesh();
		if (IsValid(Mesh3P))
		{
			CurrentWeapon->AttachToComponent(
				Mesh3P,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				FName("HandGrip_R")
			);
		}
	}

	OnCurrentWeaponChangedNative.Broadcast(CurrentWeapon);
}

void UWeaponComponent::OnRep_IsAiming()
{
	if (IsValid(CurrentWeapon))
		CurrentWeapon->SetOwnerIsAiming(bIsAiming);
	OnAimingChangedNative.Broadcast(bIsAiming);
}

// ---- Grip Re-seat ----

void UWeaponComponent::SeatWeaponGripSocket()
{
	if (!IsValid(CurrentWeapon)) return;

	USkeletalMeshComponent* Mesh = CurrentWeapon->GetWeaponMesh();
	if (!IsValid(Mesh)) return;

	static const FName GripSocketName(TEXT("GripSocket"));
	if (!Mesh->DoesSocketExist(GripSocketName)) return; // No grip socket authored — leave snapped at mesh origin (current behavior).

	// After SnapToTarget, the mesh origin sits on ik_hand_gun (relative transform = Identity).
	// Re-seat so GripSocket coincides with ik_hand_gun instead.
	// socketWorld = socketLocal * (relative * parentWorld); want socketWorld == parentWorld
	//   => socketLocal * relative = Identity  => relative = socketLocal.Inverse()
	// Preserve the weapon's existing relative scale (only set location + rotation).
	const FTransform GripLocal = Mesh->GetSocketTransform(GripSocketName, RTS_Component);
	const FTransform Inv = GripLocal.Inverse();
	CurrentWeapon->SetActorRelativeLocation(Inv.GetLocation());
	CurrentWeapon->SetActorRelativeRotation(Inv.GetRotation());
}

// ---- Stealth Exemption ----

bool UWeaponComponent::ResolveServerTakedownSnapshot()
{
	if (!IsValid(OwnerActor)) return false;

	UCompanionCommandComponent* CmdComp = OwnerActor->FindComponentByClass<UCompanionCommandComponent>();
	if (!IsValid(CmdComp)) return false;

	ACompanionCharacter* Companion = CmdComp->GetCompanion();
	if (!IsValid(Companion)) return false;

	return Companion->IsShootTakedownArmed();
}
