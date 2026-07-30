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
#include "Audio/GameAudioSubsystem.h"
#include "Audio/SurfaceAudioBank.h"
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
	DOREPLIFETIME(UWeaponComponent, PrimaryWeapon);
	DOREPLIFETIME(UWeaponComponent, SecondaryWeapon);
	DOREPLIFETIME_CONDITION(UWeaponComponent, bIsAiming, COND_SkipOwner);
}

void UWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerActor = GetOwner();
	if (!IsValid(OwnerActor)) return;
	OwnerIface = Cast<IExtractionPlayerInterface>(OwnerActor);
	if (!OwnerIface) return;

	// Server spawns both slot weapons; primary starts in hand
	if (OwnerActor->HasAuthority())
	{
		if (DefaultWeaponClass)
			PrimaryWeapon = SpawnWeaponActor(DefaultWeaponClass);
		if (DefaultSecondaryWeaponClass)
			SecondaryWeapon = SpawnWeaponActor(DefaultSecondaryWeaponClass);

		if (IsValid(PrimaryWeapon))
			SetActiveWeapon(PrimaryWeapon);
		else if (IsValid(SecondaryWeapon))
			SetActiveWeapon(SecondaryWeapon);
	}
}

void UWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bNextShotStealthExempt = false;

	if (IsValid(CurrentWeapon))
		CurrentWeapon->OnWeaponFired.RemoveAll(this);
	if (IsValid(PrimaryWeapon))
		PrimaryWeapon->OnWeaponFired.RemoveAll(this);
	if (IsValid(SecondaryWeapon))
		SecondaryWeapon->OnWeaponFired.RemoveAll(this);

	OwnerIface = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ---- Weapon Control ----

void UWeaponComponent::EquipWeapon(TSubclassOf<AWeaponBase> WeaponClass)
{
	if (!OwnerIface) return;
	if (!IsValid(OwnerActor) || !OwnerActor->HasAuthority()) return;

	bNextShotStealthExempt = false;

	// Replace the primary-slot weapon
	if (IsValid(PrimaryWeapon))
	{
		if (CurrentWeapon == PrimaryWeapon)
			CurrentWeapon = nullptr;
		PrimaryWeapon->Destroy();
		PrimaryWeapon = nullptr;
	}

	if (!WeaponClass) return;

	PrimaryWeapon = SpawnWeaponActor(WeaponClass);
	if (IsValid(PrimaryWeapon))
		SetActiveWeapon(PrimaryWeapon);
}

AWeaponBase* UWeaponComponent::ReplaceSlotWeapon(bool bPrimarySlot, TSubclassOf<AWeaponBase> NewWeaponClass, int32 Mag, int32 Reserve)
{
	if (!OwnerIface) return nullptr;
	if (!IsValid(OwnerActor) || !OwnerActor->HasAuthority()) return nullptr;
	if (!NewWeaponClass) return nullptr;
	if (OwnerIface->GetIsDBNO() || OwnerIface->IsInTakedown()) return nullptr;

	bNextShotStealthExempt = false;

	TObjectPtr<AWeaponBase>& Slot = bPrimarySlot ? PrimaryWeapon : SecondaryWeapon;
	const bool bSlotWasHeld = !IsValid(CurrentWeapon) || CurrentWeapon == Slot;

	// Only cancel the held weapon's reload/fire when the replaced slot IS the held one.
	// Picking up into the stowed slot must not disturb the gun in your hands.
	if (bSlotWasHeld && IsValid(CurrentWeapon))
		CurrentWeapon->AbortFireAndReload();

	if (IsValid(Slot))
	{
		if (CurrentWeapon == Slot)
			CurrentWeapon = nullptr;
		Slot->Destroy();
		Slot = nullptr;
	}

	Slot = SpawnWeaponActor(NewWeaponClass);
	if (!IsValid(Slot)) return nullptr;

	if (Mag >= 0 || Reserve >= 0)
		Slot->SetAmmoState(Mag, Reserve);

	if (bSlotWasHeld)
		SetActiveWeapon(Slot);

	// Corpse-gun grab — layered under SetActiveWeapon's handling foley when the slot was held.
	{
		const UWorld* World = GetWorld();
		UGameAudioSubsystem* AudioSys = World ? World->GetSubsystem<UGameAudioSubsystem>() : nullptr;
		if (AudioSys && AudioSys->GetBank())
			AudioSys->PlayFoleyFor(OwnerActor, AudioSys->GetBank()->PickupWeapon);
	}

	return Slot;
}

AWeaponBase* UWeaponComponent::FindWeaponByAmmoCategory(EEnemyWeaponAnimType Category) const
{
	AWeaponBase* const Candidates[] = { CurrentWeapon.Get(), PrimaryWeapon.Get(), SecondaryWeapon.Get() };
	for (AWeaponBase* Candidate : Candidates)
	{
		if (!IsValid(Candidate)) continue;
		const UWeaponDataAsset* Data = Candidate->GetWeaponData();
		if (Data && Data->EnemyWeaponAnimType == Category)
			return Candidate;
	}
	return nullptr;
}

EWeaponSlot UWeaponComponent::GetActiveWeaponSlot() const
{
	if (!IsValid(CurrentWeapon)) return EWeaponSlot::None;
	if (CurrentWeapon == PrimaryWeapon) return EWeaponSlot::Primary;
	if (CurrentWeapon == SecondaryWeapon) return EWeaponSlot::Secondary;
	return EWeaponSlot::None;
}

AWeaponBase* UWeaponComponent::GetStowedWeapon() const
{
	if (!IsValid(CurrentWeapon)) return nullptr;
	if (CurrentWeapon == PrimaryWeapon) return SecondaryWeapon;
	if (CurrentWeapon == SecondaryWeapon) return PrimaryWeapon;
	return nullptr;
}

AWeaponBase* UWeaponComponent::SpawnWeaponActor(TSubclassOf<AWeaponBase> WeaponClass)
{
	if (!WeaponClass) return nullptr;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerActor;
	SpawnParams.Instigator = Cast<APawn>(OwnerActor);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AWeaponBase* Weapon = World->SpawnActor<AWeaponBase>(WeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!IsValid(Weapon)) return nullptr;

	// Attach to the kit IK-rig gun bone (ik_hand_gun) so AC_ProceduralAnimation drives the weapon transform
	ACharacter* OwnerChar = Cast<ACharacter>(OwnerActor);
	if (IsValid(OwnerChar))
	{
		USkeletalMeshComponent* BodyMesh = OwnerChar->GetMesh();
		if (IsValid(BodyMesh))
		{
			Weapon->AttachToComponent(
				BodyMesh,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				KitWeaponAttachSocket
			);
		}
	}

	Weapon->InitializeAmmo();
	Weapon->SetWeaponHidden(true); // slots spawn holstered; SetActiveWeapon unhides
	return Weapon;
}

void UWeaponComponent::SetActiveWeapon(AWeaponBase* NewWeapon)
{
	if (!OwnerIface || !IsValid(OwnerActor) || !OwnerActor->HasAuthority()) return;
	if (!IsValid(NewWeapon)) return;
	// Same-weapon re-equip is only allowed to restore the kit gun visual after a throwable —
	// NotifyWeaponEquipped below respawns the kit item over the grenade.
	if (NewWeapon == CurrentWeapon && !bThrowableEquipped) return;

	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->AbortFireAndReload();
		CurrentWeapon->OnWeaponFired.RemoveAll(this);
		CurrentWeapon->SetWeaponHidden(true);
	}

	// Genuine hand-swap (or gun restore after a throwable) — first equip at spawn stays silent.
	const bool bAudibleSwap = IsValid(CurrentWeapon);

	bNextShotStealthExempt = false;
	bThrowableEquipped = false;
	CurrentWeapon = NewWeapon;
	CurrentWeapon->SetWeaponHidden(false);
	CurrentWeapon->SetOwnerIsAiming(bIsAiming);
	SeatWeaponGripSocket();

	if (bAudibleSwap)
	{
		const UWorld* World = GetWorld();
		UGameAudioSubsystem* AudioSys = World ? World->GetSubsystem<UGameAudioSubsystem>() : nullptr;
		if (AudioSys && AudioSys->GetBank())
			AudioSys->PlayFoleyFor(OwnerActor, AudioSys->GetBank()->WeaponSwitchFoley);
	}

	// Bind weapon fire delegate to multicast for 3P effects
	if (!CurrentWeapon->OnWeaponFired.IsAlreadyBound(this, &UWeaponComponent::OnWeaponFiredCallback))
		CurrentWeapon->OnWeaponFired.AddDynamic(this, &UWeaponComponent::OnWeaponFiredCallback);

	// Notify owning-client BP to drive AC_ProceduralAnimation::NewHandPose + kit item respawn.
	// Skip on dedicated server (no visuals) and when downed.
	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (IsValid(OwnerPawn) && OwnerPawn->IsLocallyControlled() && !OwnerIface->GetIsDBNO())
	{
		OwnerIface->NotifyWeaponEquipped(CurrentWeapon);
		CurrentWeapon->ResyncVisualAmmo(); // fresh kit item starts with BP-default counts otherwise
	}
}

void UWeaponComponent::SwitchToPrimary()
{
	if (!IsValid(OwnerActor)) return;
	if (OwnerIface && (OwnerIface->GetIsDBNO() || OwnerIface->IsInTakedown())) return;

	if (OwnerActor->HasAuthority())
		SetActiveWeapon(PrimaryWeapon);
	else
		Server_SwitchWeapon(0);
}

void UWeaponComponent::SwitchToSecondary()
{
	if (!IsValid(OwnerActor)) return;
	if (OwnerIface && (OwnerIface->GetIsDBNO() || OwnerIface->IsInTakedown())) return;

	if (OwnerActor->HasAuthority())
		SetActiveWeapon(SecondaryWeapon);
	else
		Server_SwitchWeapon(1);
}

void UWeaponComponent::Server_SwitchWeapon_Implementation(uint8 SlotIndex)
{
	if (OwnerIface && (OwnerIface->GetIsDBNO() || OwnerIface->IsInTakedown())) return;
	SetActiveWeapon(SlotIndex == 0 ? PrimaryWeapon.Get() : SecondaryWeapon.Get());
}

bool UWeaponComponent::SetThrowableEquipped(bool bEquipped)
{
	if (bEquipped == bThrowableEquipped) return true;

	if (bEquipped)
	{
		if (OwnerIface && (OwnerIface->GetIsDBNO() || OwnerIface->IsInTakedown())) return false;
		if (IsValid(CurrentWeapon))
			CurrentWeapon->AbortFireAndReload();
		if (bIsAiming)
			SetAiming(false);
	}

	bThrowableEquipped = bEquipped;
	return true;
}

void UWeaponComponent::StartFire(bool bAuthorityTakedownSnapshot)
{
	if (!IsValid(OwnerActor)) return;
	if (bThrowableEquipped) return; // FireStart diverts to the kit item before reaching here — belt-and-braces

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

	bNextShotStealthExempt = false;

	if (!OwnerActor->HasAuthority() && IsValid(CurrentWeapon))
		CurrentWeapon->StopFiring();

	Server_StopFire();
}

void UWeaponComponent::StartReload()
{
	if (!IsValid(OwnerActor)) return;
	if (bThrowableEquipped) return; // R with the grenade out must not reload the stowed gun
	if (!OwnerActor->HasAuthority() && IsValid(CurrentWeapon))
		CurrentWeapon->Reload();

	Server_Reload();
}

void UWeaponComponent::SetAiming(bool bNewAiming)
{
	if (bThrowableEquipped && bNewAiming) return; // no ADS on the throwable; clearing is allowed

	bIsAiming = bNewAiming;

	if (IsValid(CurrentWeapon))
		CurrentWeapon->SetOwnerIsAiming(bNewAiming);

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

	if (!IsValid(CurrentWeapon) || !IsValid(OwnerActor) || !OwnerIface) return;

	// Detach previous weapon if the server swapped without destroying the old one
	if (IsValid(PreviousWeapon) && PreviousWeapon != CurrentWeapon)
		PreviousWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	PreviousWeapon = CurrentWeapon;

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

		// Notify owning-client BP to drive AC_ProceduralAnimation::NewHandPose.
		// Skip when downed — arms aren't visible and the pose system may not be ready.
		if (IsValid(OwnerActor) && OwnerIface && !OwnerIface->GetIsDBNO())
		{
			OwnerIface->NotifyWeaponEquipped(CurrentWeapon);
			CurrentWeapon->ResyncVisualAmmo();
		}
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

	// Client-side mirror of SetActiveWeapon's hide/show: only the held weapon renders.
	AWeaponBase* Slots[] = { PrimaryWeapon.Get(), SecondaryWeapon.Get() };
	for (AWeaponBase* Slot : Slots)
		if (IsValid(Slot) && Slot != CurrentWeapon)
			Slot->SetWeaponHidden(true);
	CurrentWeapon->SetWeaponHidden(false);
}

void UWeaponComponent::OnRep_IsAiming()
{
	// Proxies can blend to ADS pose here
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
