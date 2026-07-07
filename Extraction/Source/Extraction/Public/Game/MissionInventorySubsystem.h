// World-scope record of mission items the player holds (keycards) and the single grant
// path for all loot — containers and ammo pickups both route through GrantLoot so every
// acquisition raises one OnLootNotify toast. World lifetime = resets per level load.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/LootTypes.h"
#include "MissionInventorySubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLootNotify, const FText&, Message);

DECLARE_LOG_CATEGORY_EXTERN(LogMissionInventory, Log, All);

UCLASS()
class EXTRACTION_API UMissionInventorySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Apply a loot grant. Ammo is added to the recipient's current weapon's reserve only when
	 * the categories match (no player inventory — mismatches are toast-reported and discarded).
	 * Keycards are recorded here. Recipient defaults to player pawn 0.
	 * Returns true when something was actually granted/recorded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Loot")
	bool GrantLoot(const FLootGrant& Grant, APawn* Recipient = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Loot|Keycard")
	void RecordKeycard(FName KeycardId);

	UFUNCTION(BlueprintPure, Category = "Loot|Keycard")
	bool HasKeycard(FName KeycardId) const { return KeycardId != NAME_None && HeldKeycards.Contains(KeycardId); }

	/** HUD toast subscribes here — one channel for every acquisition message. */
	UPROPERTY(BlueprintAssignable, Category = "Loot")
	FOnLootNotify OnLootNotify;

private:
	bool GrantAmmo(const FLootGrant& Grant, APawn* Recipient);

	/** Falls back to player pawn 0 when no explicit recipient is passed. */
	APawn* ResolveRecipient(APawn* Recipient) const;

	/** Keycard ids acquired this level. */
	TSet<FName> HeldKeycards;
};
