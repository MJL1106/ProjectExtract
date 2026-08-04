// UMissionInventorySubsystem — loot grants and keycard record.

#include "Game/MissionInventorySubsystem.h"
#include "Components/ConsumableInventoryComponent.h"
#include "Components/WeaponComponent.h"
#include "Weapon/WeaponBase.h"
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

	case ELootType::Stim:
		return GrantStims(Grant, ResolveRecipient(Recipient));

	default:
		return false;
	}
}

bool UMissionInventorySubsystem::GrantStims(const FLootGrant& Grant, APawn* Recipient)
{
	if (!IsValid(Recipient) || Grant.StimCount <= 0) return false;

	UConsumableInventoryComponent* Inventory = Recipient->FindComponentByClass<UConsumableInventoryComponent>();
	if (!IsValid(Inventory)) return false;

	const int32 Added = Inventory->AddStims(Grant.StimCount);
	if (Added <= 0)
	{
		OnLootNotify.Broadcast(NSLOCTEXT("Loot", "StimsFull", "Stims full"));
		return false;
	}

	const FText StimMessage = FText::Format(
		NSLOCTEXT("Loot", "StimsGranted", "+{0} Stim"), FText::AsNumber(Added));
	OnLootNotify.Broadcast(StimMessage);
	OnLootGranted.Broadcast(ELootType::Stim, Added, StimMessage);
	UE_LOG(LogMissionInventory, Log, TEXT("GrantStims: +%d to %s"), Added, *GetNameSafe(Recipient));
	return true;
}

bool UMissionInventorySubsystem::GrantAmmo(const FLootGrant& Grant, APawn* Recipient)
{
	if (!IsValid(Recipient) || Grant.AmmoAmount <= 0) return false;

	const UWeaponComponent* WeaponComp = Recipient->FindComponentByClass<UWeaponComponent>();
	AWeaponBase* Weapon = WeaponComp ? WeaponComp->FindWeaponByAmmoCategory(Grant.AmmoCategory) : nullptr;

	const FText CategoryText = UEnum::GetDisplayValueAsText(Grant.AmmoCategory);

	// No inventory in v1 — ammo that doesn't fit either carried weapon is reported and discarded.
	if (!Weapon)
	{
		OnLootNotify.Broadcast(FText::Format(
			NSLOCTEXT("Loot", "AmmoIncompatible", "Found {0} ammo (no compatible weapon)"), CategoryText));
		return false;
	}

	const int32 Added = Weapon->AddReserveAmmo(Grant.AmmoAmount);
	if (Added <= 0) return false;

	const FText AmmoMessage = FText::Format(
		NSLOCTEXT("Loot", "AmmoGranted", "+{0} {1} ammo"), Added, CategoryText);
	OnLootNotify.Broadcast(AmmoMessage);
	OnLootGranted.Broadcast(ELootType::Ammo, Added, AmmoMessage);
	UE_LOG(LogMissionInventory, Log, TEXT("GrantAmmo: +%d %s to %s"),
		Added, *CategoryText.ToString(), *GetNameSafe(Recipient));
	return true;
}

void UMissionInventorySubsystem::RecordKeycard(FName KeycardId, bool bSilent)
{
	if (KeycardId == NAME_None) return;

	bool bAlreadyHeld = false;
	HeldKeycards.Add(KeycardId, &bAlreadyHeld);
	if (bAlreadyHeld) return; // no duplicate toast for a card already held

	// Both announcement channels obey bSilent for the same reason: the checkpoint fast-forward
	// re-grants cards the player already earned, and re-announcing them at level start is the
	// exact bug bSilent exists to prevent. OnKeycardRecorded still fires — gating logic is not
	// an announcement.
	if (!bSilent)
	{
		const FText KeycardMessage = FText::Format(
			NSLOCTEXT("Loot", "KeycardAcquired", "Keycard acquired: {0}"), FText::FromName(KeycardId));
		OnLootNotify.Broadcast(KeycardMessage);
		OnLootGranted.Broadcast(ELootType::Keycard, 1, KeycardMessage);
	}
	OnKeycardRecorded.Broadcast(KeycardId);
	UE_LOG(LogMissionInventory, Log, TEXT("RecordKeycard: %s"), *KeycardId.ToString());
}

APawn* UMissionInventorySubsystem::ResolveRecipient(APawn* Recipient) const
{
	if (IsValid(Recipient)) return Recipient;
	return UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
}
