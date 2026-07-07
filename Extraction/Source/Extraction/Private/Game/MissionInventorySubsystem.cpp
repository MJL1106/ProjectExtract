// UMissionInventorySubsystem — loot grants and keycard record.

#include "Game/MissionInventorySubsystem.h"
#include "Components/WeaponComponent.h"
#include "Weapon/WeaponBase.h"
#include "Data/WeaponDataAsset.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogMissionInventory);

bool UMissionInventorySubsystem::GrantLoot(const FLootGrant& Grant, APawn* Recipient)
{
	switch (Grant.Type)
	{
	case ELootType::Ammo:
		return GrantAmmo(Grant, ResolveRecipient(Recipient));

	case ELootType::Keycard:
		if (Grant.KeycardId == NAME_None) return false;
		RecordKeycard(Grant.KeycardId);
		return true;

	default:
		return false;
	}
}

bool UMissionInventorySubsystem::GrantAmmo(const FLootGrant& Grant, APawn* Recipient)
{
	if (!IsValid(Recipient) || Grant.AmmoAmount <= 0) return false;

	const UWeaponComponent* WeaponComp = Recipient->FindComponentByClass<UWeaponComponent>();
	AWeaponBase* Weapon = WeaponComp ? WeaponComp->GetCurrentWeapon() : nullptr;
	const UWeaponDataAsset* Data = Weapon ? Weapon->GetWeaponData() : nullptr;

	const FText CategoryText = UEnum::GetDisplayValueAsText(Grant.AmmoCategory);

	// No inventory in v1 — ammo that doesn't fit the current weapon is reported and discarded.
	if (!Data || Data->EnemyWeaponAnimType != Grant.AmmoCategory)
	{
		OnLootNotify.Broadcast(FText::Format(
			NSLOCTEXT("Loot", "AmmoIncompatible", "Found {0} ammo (no compatible weapon)"), CategoryText));
		return false;
	}

	const int32 Added = Weapon->AddReserveAmmo(Grant.AmmoAmount);
	if (Added <= 0) return false;

	OnLootNotify.Broadcast(FText::Format(
		NSLOCTEXT("Loot", "AmmoGranted", "+{0} {1} ammo"), Added, CategoryText));
	UE_LOG(LogMissionInventory, Log, TEXT("GrantAmmo: +%d %s to %s"),
		Added, *CategoryText.ToString(), *GetNameSafe(Recipient));
	return true;
}

void UMissionInventorySubsystem::RecordKeycard(FName KeycardId)
{
	if (KeycardId == NAME_None) return;

	bool bAlreadyHeld = false;
	HeldKeycards.Add(KeycardId, &bAlreadyHeld);
	if (bAlreadyHeld) return; // no duplicate toast for a card already held

	OnLootNotify.Broadcast(FText::Format(
		NSLOCTEXT("Loot", "KeycardAcquired", "Keycard acquired: {0}"), FText::FromName(KeycardId)));
	UE_LOG(LogMissionInventory, Log, TEXT("RecordKeycard: %s"), *KeycardId.ToString());
}

APawn* UMissionInventorySubsystem::ResolveRecipient(APawn* Recipient) const
{
	if (IsValid(Recipient)) return Recipient;
	return UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
}
