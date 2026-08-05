// UAttachmentIconSet -- the single source of attachment display identity (name + icon), so an
// attachment can only ever look like one thing no matter where it's shown: the world focus prompt
// (UAttachmentStatPreviewWidget), the HUD pickup toast (UPickupToastStackWidget), and any future
// loadout entry. Without this, each of those widgets would resolve its own name/icon from whatever
// it happens to have on hand and could quietly drift apart.
//
// KitSlotByte / OptionByte are the RAW kit ENUM_AttachmentSlot / ST_Attachments bytes -- NOT
// EAttachmentSlot. Read the comment above `namespace KitAttachmentSlots` in Core/ExtractionTypes.h
// first: the kit enum's byte order differs from the C++ EAttachmentSlot order, and the project
// translates between the two in exactly one place (KitAttachmentSlots::ToAttachmentSlot, used by
// AWeaponBase::SetAttachmentSlotOption and UAttachmentStatPreviewWidget to resolve a GAMEPLAY
// effect). This asset does the opposite job -- naming/iconography for display only -- and a world
// attachment pickup only ever carries the raw kit byte, never a converted EAttachmentSlot, so the
// lookup key here is deliberately the raw kit byte with no conversion happening on this path at all.
//
// KitSlotByte and OptionByte MUST be looked up TOGETHER, never OptionByte alone: the kit's option
// enum is a SEPARATE enum per slot, so the same OptionByte value names a different attachment under
// every slot (e.g. OptionByte 1 is "Holosight" under the sight slot but "Laser" under the laser
// slot). FindEntry below only ever matches the (KitSlotByte, OptionByte) pair as a unit -- do not
// add a lookup path that indexes by OptionByte in isolation, it would silently return another
// slot's icon.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AttachmentIconSet.generated.h"

class UTexture2D;

/** One named, iconed attachment option, keyed by the raw kit bytes that identify it. */
USTRUCT(BlueprintType)
struct EXTRACTION_API FAttachmentIconEntry
{
	GENERATED_BODY()

	/** Raw kit ENUM_AttachmentSlot byte -- see the class comment for why this is not EAttachmentSlot. */
	UPROPERTY(EditAnywhere, Category = "Attachments")
	uint8 KitSlotByte = 0;

	/** Raw kit ST_Attachments option byte within that slot. */
	UPROPERTY(EditAnywhere, Category = "Attachments")
	uint8 OptionByte = 0;

	UPROPERTY(EditAnywhere, Category = "Attachments")
	FText DisplayName;

	/** Designer-assigned in the editor -- C++ never carries a /Game/ path. */
	UPROPERTY(EditAnywhere, Category = "Attachments")
	TObjectPtr<UTexture2D> Icon;
};

UCLASS(BlueprintType)
class EXTRACTION_API UAttachmentIconSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Null on a miss (unknown byte pair, or an entry never fielded an icon) -- a half-filled set
	 *  degrades to "no icon" for that entry, never to a wrong one. */
	UFUNCTION(BlueprintPure, Category = "Attachments")
	UTexture2D* FindIcon(uint8 KitSlotByte, uint8 OptionByte) const;

	/** Empty text on a miss, for the same reason as FindIcon. */
	UFUNCTION(BlueprintPure, Category = "Attachments")
	FText FindDisplayName(uint8 KitSlotByte, uint8 OptionByte) const;

#if WITH_EDITOR
	/** Errors on a duplicate (KitSlotByte, OptionByte) pair -- FindEntry would otherwise silently
	 *  resolve to whichever one comes first, a plausible copy-paste mistake in a designer-filled
	 *  list this small. Also errors on an entry with a null Icon: FindIcon's null-on-miss contract
	 *  is for an ABSENT entry, not a forgotten one that IS in the list. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	/** ~9 entries in practice -- one per fielded attachment across every slot this project ships.
	 *  A linear scan is cheap enough here: lookups happen on focus change and pickup, never per
	 *  frame, so there is nothing to gain from a map for a set this small. */
	UPROPERTY(EditAnywhere, Category = "Attachments")
	TArray<FAttachmentIconEntry> Entries;

private:
	const FAttachmentIconEntry* FindEntry(uint8 KitSlotByte, uint8 OptionByte) const;
};
