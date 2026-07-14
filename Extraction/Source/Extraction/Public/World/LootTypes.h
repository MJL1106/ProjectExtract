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

	/** Keycard identity — doors reference the same id in their RequiredKeycardId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (EditCondition = "Type == ELootType::Keycard", EditConditionHides))
	FName KeycardId = NAME_None;

	/** Number of player health stims granted, clamped by the player's consumable capacity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (EditCondition = "Type == ELootType::Stim", EditConditionHides, ClampMin = "1"))
	int32 StimCount = 1;
};
