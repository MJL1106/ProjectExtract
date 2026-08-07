// Shared loot item types — consumed by containers, pickups, and the mission inventory subsystem.

#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyTypes.h"
#include "LootTypes.generated.h"

/** What a loot grant contains. Extensible — Grenade / Weapon / Attachment are planned later. */
UENUM(BlueprintType)
enum class ELootType : uint8
{
	Ammo,
	Keycard,
	Stim,
};

/** One grantable item. Containers hold an array of these; ammo pickups build one on collect. */
USTRUCT(BlueprintType)
struct EXTRACTION_API FLootGrant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	ELootType Type = ELootType::Ammo;

	/** Ammo category — must match the recipient's current weapon's EnemyWeaponAnimType to be granted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (EditCondition = "Type == ELootType::Ammo", EditConditionHides))
	EEnemyWeaponAnimType AmmoCategory = EEnemyWeaponAnimType::Rifle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (EditCondition = "Type == ELootType::Ammo", EditConditionHides, ClampMin = "0"))
	int32 AmmoAmount = 30;

	/** Keycard identity — doors reference the same id in their RequiredKeycardId. Machine-only:
	 *  it is matched against, never shown. Author it for lookup, not for reading. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (EditCondition = "Type == ELootType::Keycard", EditConditionHides))
	FName KeycardId = NAME_None;

	/** Player-facing card name, shown BARE as the pickup toast's item name ("KEYCARD ACQUIRED /
	 *  Office Keycard / +1") — author it as a noun, not a sentence. This is the ONLY keycard string
	 *  the player ever sees; KeycardId stays the machine id doors, objective markers and objective
	 *  steps match on, and must never reach the HUD. Left empty, the row falls back to a generic
	 *  "Keycard" with no id in it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (EditCondition = "Type == ELootType::Keycard", EditConditionHides))
	FText KeycardDisplayName;

	/** Number of player health stims granted, clamped by the player's consumable capacity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (EditCondition = "Type == ELootType::Stim", EditConditionHides, ClampMin = "1"))
	int32 StimCount = 1;
};
