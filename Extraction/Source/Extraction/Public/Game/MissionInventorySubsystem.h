// World-scope record of mission items the player holds (keycards) and the single grant
// path for all loot — containers and ammo pickups both route through GrantLoot so every
// acquisition raises one OnLootNotify toast. World lifetime = resets per level load.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/LootTypes.h"
#include "MissionInventorySubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLootNotify, const FText&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKeycardRecorded, FName, KeycardId);

/** Severity for the OnToastNotify channel -- drives HUD toast duration/colour/sound. */
UENUM(BlueprintType)
enum class EToastSeverity : uint8
{
	Info,      // loot, objectives -- the existing look
	Warning,   // player is drifting into trouble
	Alert,     // the bad thing has happened
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnToastNotify, const FText&, Message, EToastSeverity, Severity);

/** Success-only acquisition channel. OnLootNotify carries refusals too ("Stims full", "no
 *  compatible weapon"), so a pickup display fed from it announces things the player never got. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnLootGranted, ELootType, Type, int32, Amount, const FText&, Label);

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

	/** bSilent suppresses the acquisition toast only — OnKeycardRecorded still fires, so anything
	 *  gating on the card stays correct. Used by the checkpoint fast-forward, which re-grants cards
	 *  the player already earned and must not re-announce them at level start. */
	UFUNCTION(BlueprintCallable, Category = "Loot|Keycard")
	void RecordKeycard(FName KeycardId, bool bSilent = false);

	UFUNCTION(BlueprintPure, Category = "Loot|Keycard")
	bool HasKeycard(FName KeycardId) const { return KeycardId != NAME_None && HeldKeycards.Contains(KeycardId); }

	/** HUD toast subscribes here — one channel for every acquisition message. */
	UPROPERTY(BlueprintAssignable, Category = "Loot")
	FOnLootNotify OnLootNotify;

	/** Second HUD toast channel for messages that need emphasis (stealth discipline etc).
	 *  OnLootNotify remains the plain acquisition channel -- this one carries a severity so
	 *  the toast can hold longer, tint distinctly and play a sting. */
	UPROPERTY(BlueprintAssignable, Category = "Loot")
	FOnToastNotify OnToastNotify;

	/** Fired once per NEWLY acquired keycard (not on duplicates). Objective markers with an
	 *  item criterion listen here. */
	UPROPERTY(BlueprintAssignable, Category = "Loot|Keycard")
	FOnKeycardRecorded OnKeycardRecorded;

	/** Raised only where something was actually added to the player. The HUD pickup display binds
	 *  here; OnLootNotify stays the plain message channel. */
	UPROPERTY(BlueprintAssignable, Category = "Loot")
	FOnLootGranted OnLootGranted;

private:
	bool GrantAmmo(const FLootGrant& Grant, APawn* Recipient);
	bool GrantStims(const FLootGrant& Grant, APawn* Recipient);

	/** Falls back to player pawn 0 when no explicit recipient is passed. */
	APawn* ResolveRecipient(APawn* Recipient) const;

	/** Keycard ids acquired this level. */
	TSet<FName> HeldKeycards;
};
