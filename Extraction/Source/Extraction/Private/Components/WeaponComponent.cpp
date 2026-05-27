// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeaponComponent.h"
#include "WeaponBase.h"
#include "WeaponDataAsset.h"
#include "ExtractionCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Extraction.h"

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

	OwnerCharacter = Cast<AExtractionCharacter>(GetOwner());
	if (!IsValid(OwnerCharacter)) return;

	// Warn loudly if the WeaponSpawn component is missing — weapon attachment will silently fail otherwise
	if (IsValid(OwnerCharacter) && !IsValid(OwnerCharacter->GetWeaponSpawn()))
		UE_LOG(LogExtraction, Warning, TEXT("WeaponComponent: OwnerCharacter has no WeaponSpawn component — weapons will not attach. Ensure the BP child class exposes one."));

	// Server spawns default weapon
	if (OwnerCharacter->HasAuthority() && DefaultWeaponClass)
		EquipWeapon(DefaultWeaponClass);
}

void UWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(CurrentWeapon))
		CurrentWeapon->OnWeaponFired.RemoveAll(this);

	Super::EndPlay(EndPlayReason);
}

// ---- Weapon Control ----

void UWeaponComponent::EquipWeapon(TSubclassOf<AWeaponBase> WeaponClass)
{
	if (!IsValid(OwnerCharacter)) return;
	if (!OwnerCharacter->HasAuthority()) return;

	// Destroy existing weapon
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->Destroy();
		CurrentWeapon = nullptr;
	}

	if (!WeaponClass) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = OwnerCharacter;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	CurrentWeapon = World->SpawnActor<AWeaponBase>(WeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!IsValid(CurrentWeapon)) return;

	// Attach to camera-space weapon spawn point (kit procedural anim drives hand IK from here)
	USceneComponent* WeaponSpawn = OwnerCharacter->GetWeaponSpawn();
	if (IsValid(WeaponSpawn))
	{
		CurrentWeapon->AttachToComponent(
			WeaponSpawn,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			NAME_None
		);
	}

	CurrentWeapon->InitializeAmmo();

	// Bind weapon fire delegate to multicast for 3P effects
	if (!CurrentWeapon->OnWeaponFired.IsAlreadyBound(this, &UWeaponComponent::OnWeaponFiredCallback))
		CurrentWeapon->OnWeaponFired.AddDynamic(this, &UWeaponComponent::OnWeaponFiredCallback);
}

void UWeaponComponent::StartFire()
{
	// Local prediction (skip if we're the server — RPC will execute locally)
	if (!IsValid(OwnerCharacter)) return;
	if (!OwnerCharacter->HasAuthority() && IsValid(CurrentWeapon))
		CurrentWeapon->StartFiring();

	Server_StartFire();
}

void UWeaponComponent::StopFire()
{
	if (!IsValid(OwnerCharacter)) return;
	if (!OwnerCharacter->HasAuthority() && IsValid(CurrentWeapon))
		CurrentWeapon->StopFiring();

	Server_StopFire();
}

void UWeaponComponent::StartReload()
{
	if (!IsValid(OwnerCharacter)) return;
	if (!OwnerCharacter->HasAuthority() && IsValid(CurrentWeapon))
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
	if (IsValid(OwnerCharacter) && OwnerCharacter->HasAuthority())
		Multicast_OnFired();
}

// ---- Multicast ----

void UWeaponComponent::Multicast_OnFired_Implementation()
{
	// 3P effects (muzzle flash, sound) go here
	// Skip on owning client (already handled locally)
	if (IsValid(OwnerCharacter) && OwnerCharacter->IsLocallyControlled()) return;
}

// ---- RepNotify ----

void UWeaponComponent::OnRep_CurrentWeapon()
{
	// Late-joiner safety: OwnerCharacter may not have been set if replication ordering delivered
	// CurrentWeapon before BeginPlay ran on this component.
	if (!IsValid(OwnerCharacter))
		OwnerCharacter = Cast<AExtractionCharacter>(GetOwner());

	if (!IsValid(CurrentWeapon) || !IsValid(OwnerCharacter)) return;

	// Fix 3: detach the previous weapon if the server swapped without destroying the old one
	if (IsValid(PreviousWeapon) && PreviousWeapon != CurrentWeapon)
		PreviousWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	PreviousWeapon = CurrentWeapon;

	if (OwnerCharacter->IsLocallyControlled())
	{
		// Owner path: attach to camera-space weapon spawn point (kit procedural anim drives hand IK from here)
		USceneComponent* WeaponSpawn = OwnerCharacter->GetWeaponSpawn();
		if (IsValid(WeaponSpawn))
		{
			CurrentWeapon->AttachToComponent(
				WeaponSpawn,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				NAME_None
			);
		}
	}
	else
	{
		// Proxy path: attach to 3P skeleton
		USkeletalMeshComponent* Mesh3P = OwnerCharacter->GetMesh();
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
