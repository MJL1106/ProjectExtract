// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeaponComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "Character/ExtractionPlayerInterface.h"
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
	if (IsValid(CurrentWeapon))
		CurrentWeapon->OnWeaponFired.RemoveAll(this);

	OwnerIface = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ---- Weapon Control ----

void UWeaponComponent::EquipWeapon(TSubclassOf<AWeaponBase> WeaponClass)
{
	if (!OwnerIface) return;
	if (!IsValid(OwnerActor) || !OwnerActor->HasAuthority()) return;

	// Destroy existing weapon
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->Destroy();
		CurrentWeapon = nullptr;
	}

	if (!WeaponClass) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	ACharacter* OwnerChar = Cast<ACharacter>(OwnerActor);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerActor;
	SpawnParams.Instigator = Cast<APawn>(OwnerActor);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	CurrentWeapon = World->SpawnActor<AWeaponBase>(WeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!IsValid(CurrentWeapon)) return;

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

	// Bind weapon fire delegate to multicast for 3P effects
	if (!CurrentWeapon->OnWeaponFired.IsAlreadyBound(this, &UWeaponComponent::OnWeaponFiredCallback))
		CurrentWeapon->OnWeaponFired.AddDynamic(this, &UWeaponComponent::OnWeaponFiredCallback);

	// Notify owning-client BP to drive AC_ProceduralAnimation::NewHandPose.
	// Skip on dedicated server (no visuals) and when downed.
	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	if (IsValid(CurrentWeapon) && IsValid(OwnerActor) && OwnerIface && IsValid(OwnerPawn) && OwnerPawn->IsLocallyControlled() && !OwnerIface->GetIsDBNO())
		OwnerIface->NotifyWeaponEquipped(CurrentWeapon);
}

void UWeaponComponent::StartFire()
{
	// Local prediction (skip if we're the server — RPC will execute locally)
	if (!IsValid(OwnerActor)) return;
	if (!OwnerActor->HasAuthority() && IsValid(CurrentWeapon))
		CurrentWeapon->StartFiring();

	Server_StartFire();
}

void UWeaponComponent::StopFire()
{
	if (!IsValid(OwnerActor)) return;
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
	bIsAiming = bNewAiming;

	if (IsValid(CurrentWeapon))
		CurrentWeapon->SetOwnerIsAiming(bNewAiming);

	Server_SetAiming(bNewAiming);
}

// ---- Server RPCs ----

void UWeaponComponent::Server_StartFire_Implementation()
{
	if (!IsValid(CurrentWeapon)) return;
	CurrentWeapon->StartFiring();
}

void UWeaponComponent::Server_StopFire_Implementation()
{
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
		Multicast_OnFired();
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
