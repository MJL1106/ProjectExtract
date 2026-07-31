// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "Movement/TraversalTypes.h"
#include "ExtractionTypes.generated.h"

// --- Gameplay Tags ---
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Enemy);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Companion);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Player);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_BT_EnemyCombat);

/**
 * Weapon type categories.
 * Add new entries here when introducing new weapon classes.
 */
UENUM(BlueprintType)
enum class EWeaponType : uint8
{
	Unarmed		UMETA(DisplayName = "Unarmed"),
	Pistol		UMETA(DisplayName = "Pistol"),
	Rifle		UMETA(DisplayName = "Rifle"),
};

/**
 * Identifies which transition animation to play when entering prone.
 * Selected at runtime based on the character's current movement state.
 */
UENUM(BlueprintType)
enum class EProneTransitionType : uint8
{
	None		UMETA(DisplayName = "None"),
	FromIdle	UMETA(DisplayName = "From Idle"),
	FromWalk	UMETA(DisplayName = "From Walk"),
	FromSprint	UMETA(DisplayName = "From Sprint"),
	FromCrouch	UMETA(DisplayName = "From Crouch"),
};

/**
 * Hit region categories for hitbox-based damage multipliers.
 * Mapped from skeleton bone names via BoneToHitRegionMap on the character.
 */
UENUM(BlueprintType)
enum class EHitRegion : uint8
{
	Head	UMETA(DisplayName = "Head"),
	Torso	UMETA(DisplayName = "Torso"),
	Arms	UMETA(DisplayName = "Arms"),
	Legs	UMETA(DisplayName = "Legs"),
};

/**
 * Current weapon operational state.
 * Drives animation selection and input gating.
 */
UENUM(BlueprintType)
enum class EWeaponState : uint8
{
	Idle		UMETA(DisplayName = "Idle"),
	Firing		UMETA(DisplayName = "Firing"),
	Reloading	UMETA(DisplayName = "Reloading"),
	Equipping	UMETA(DisplayName = "Equipping"),
};

/**
 * Identifies which weapon slot the player is currently holding.
 * Moved here from WeaponComponent.h so the checkpoint snapshot structs can
 * reference it without pulling in the component header.
 */
UENUM(BlueprintType)
enum class EWeaponSlot : uint8
{
	None		UMETA(DisplayName = "None"),
	Primary		UMETA(DisplayName = "Primary"),
	Secondary	UMETA(DisplayName = "Secondary"),
};

/**
 * Attachment slot categories, mirroring the kit ST_Attachments fields.
 * Used by attachment DataAssets and world attachment pickups to identify which
 * slot an attachment occupies.
 */
UENUM(BlueprintType)
enum class EAttachmentSlot : uint8
{
	Sight		UMETA(DisplayName = "Sight"),
	Muzzle		UMETA(DisplayName = "Muzzle"),
	Laser		UMETA(DisplayName = "Laser"),
	Grip		UMETA(DisplayName = "Grip"),
	Handguard	UMETA(DisplayName = "Handguard"),
};

/**
 * The kit Blueprint's ENUM_AttachmentSlot byte values, which are NOT in the same order as
 * EAttachmentSlot above. Kit 2 is Muzzle where C++ 2 is Laser, so casting one to the other
 * silently reads the WRONG slot -- world attachment pickups carry the KIT byte, so every
 * kit-facing entry point translates through ToAttachmentSlot and NOWHERE ELSE.
 *
 * REORDERING ENUM_AttachmentSlot IN THE KIT BLUEPRINT SILENTLY BREAKS THIS TABLE: the stat
 * preview would quote another slot's numbers and the pickup would fit into the wrong slot,
 * with no error anywhere. If a slot is added or moved there, this table moves with it.
 */
namespace KitAttachmentSlots
{
	inline constexpr uint8 Sights     = 0;
	inline constexpr uint8 Laser      = 1;
	inline constexpr uint8 Muzzle     = 2;
	inline constexpr uint8 LeftHand   = 3;
	inline constexpr uint8 Handguards = 4;
	inline constexpr uint8 Barrels    = 5;

	/** Translates a raw kit slot byte to its C++ slot. False when the byte carries no gameplay
	 *  slot at all -- kit Barrels (no stat array on UWeaponDataAsset and no field on
	 *  FWeaponAttachmentSelection), or an unknown byte. Callers read false as "cosmetic only". */
	inline bool ToAttachmentSlot(uint8 KitSlotByte, EAttachmentSlot& OutSlot)
	{
		switch (KitSlotByte)
		{
		case Sights:     OutSlot = EAttachmentSlot::Sight;     return true;
		case Laser:      OutSlot = EAttachmentSlot::Laser;     return true;
		case Muzzle:     OutSlot = EAttachmentSlot::Muzzle;    return true;
		case LeftHand:   OutSlot = EAttachmentSlot::Grip;      return true;
		case Handguards: OutSlot = EAttachmentSlot::Handguard; return true;
		default:         return false;
		}
	}
}

/**
 * Per-slot attachment selection for a weapon — raw kit enum bytes as written into
 * ST_Attachments by the loadout UI / attachment pickups. 0 = the slot's first kit
 * enum value (typically empty/ironsight), which resolves to no stat modifiers.
 */
USTRUCT(BlueprintType)
struct FWeaponAttachmentSelection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachments")
	uint8 Sight = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachments")
	uint8 Muzzle = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachments")
	uint8 Laser = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachments")
	uint8 Grip = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachments")
	uint8 Handguard = 0;
};

// ---------------------------------------------------------------------------
// Checkpoint loadout snapshot — persisted on UExtractionGameInstance across
// OpenLevel so a mission-fail restart restores the player's exact loadout.
// ---------------------------------------------------------------------------

class AWeaponBase;

/**
 * Snapshot of one item slot at checkpoint time. All six attachment bytes are
 * stored even though the C++ SetAttachmentSelection consumes only five — the
 * sixth (Barrel) lives in the kit ST_Attachments and the BP restore path
 * writes it back through the same slot-record flow that the pickup path uses.
 */
USTRUCT(BlueprintType)
struct FCheckpointSlotSnapshot
{
	GENERATED_BODY()

	/** False when the slot was empty at capture time — all other fields are meaningless. */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	bool bValid = false;

	/** The kit visual class stored in the slot record's WeaponData field (e.g. BP_Item_AK). */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	TSubclassOf<UObject> KitVisualClass;

	/** The gameplay weapon class (AWeaponBase subclass) that ReplaceSlotWeapon spawns. */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	TSubclassOf<AWeaponBase> WeaponClass;

	/** Current magazine ammo. */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	int32 Ammo = 0;

	/** Reserve ammo (rounds not in the magazine). */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	int32 ReserveAmmo = 0;

	// --- Attachment bytes (all six, mirroring ST_Attachments) ---

	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	uint8 Sight = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	uint8 Muzzle = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	uint8 Laser = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	uint8 Grip = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	uint8 Handguard = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	uint8 Barrel = 0;
};

/**
 * Full player loadout at checkpoint time. Stored on UExtractionGameInstance,
 * keyed by level name (mirrors the objective checkpoint keying).
 */
USTRUCT(BlueprintType)
struct FCheckpointLoadoutSnapshot
{
	GENERATED_BODY()

	/** False until a capture has run — prevents restoring uninitialised data. */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	bool bValid = false;

	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	FCheckpointSlotSnapshot PrimarySlot;

	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	FCheckpointSlotSnapshot SecondarySlot;

	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	FCheckpointSlotSnapshot ThrowableSlot;

	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	FCheckpointSlotSnapshot MeleeSlot;

	/** Which slot was active at capture — raw uint8 matching the kit's ENUM_ItemSlots.
	 *  BP uses this for the throwable/melee/hands case where EWeaponSlot is None. */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	uint8 ActiveSlotIndex = 0;

	/** Which C++ weapon slot was active at capture. Authoritative for Primary/Secondary
	 *  (read from WeaponComponent::GetActiveWeaponSlot, immune to the kit mirror desync).
	 *  None when the player held a throwable, melee, or hands — BP falls back to
	 *  ActiveSlotIndex for those. Populated by C++ in CommitLoadoutSnapshot, not by BP. */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	EWeaponSlot ActiveWeaponSlot = EWeaponSlot::None;

	/** Stim count at capture. */
	UPROPERTY(BlueprintReadWrite, Category = "Checkpoint")
	int32 StimCount = 0;
};

/**
 * Per-weapon recoil pattern data.
 * Points define camera offset per shot (X=yaw, Y=pitch).
 */
USTRUCT(BlueprintType)
struct FRecoilPattern
{
	GENERATED_BODY()

	/** Recoil offset per shot index (X=yaw, Y=pitch in degrees) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	TArray<FVector2D> Points;

	/** Time after last shot before pattern index resets to 0 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0.01"))
	float ResetDelay = 0.15f;

	/** Duration for camera to recover to pre-recoil rotation after firing stops */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0.01"))
	float RecoveryTime = 0.4f;

	/** Recoil scale multiplier while aiming down sights (1.0 = no reduction) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ADSMultiplier = 0.7f;
};
