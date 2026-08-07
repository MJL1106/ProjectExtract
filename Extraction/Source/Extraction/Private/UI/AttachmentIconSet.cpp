// UAttachmentIconSet -- see header.

#include "UI/AttachmentIconSet.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UTexture2D* UAttachmentIconSet::FindIcon(uint8 KitSlotByte, uint8 OptionByte) const
{
	const FAttachmentIconEntry* Entry = FindEntry(KitSlotByte, OptionByte);
	return Entry ? Entry->Icon : nullptr;
}

FText UAttachmentIconSet::FindDisplayName(uint8 KitSlotByte, uint8 OptionByte) const
{
	const FAttachmentIconEntry* Entry = FindEntry(KitSlotByte, OptionByte);
	return Entry ? Entry->DisplayName : FText::GetEmpty();
}

const FAttachmentIconEntry* UAttachmentIconSet::FindEntry(uint8 KitSlotByte, uint8 OptionByte) const
{
	return Entries.FindByPredicate([KitSlotByte, OptionByte](const FAttachmentIconEntry& Entry)
	{
		return Entry.KitSlotByte == KitSlotByte && Entry.OptionByte == OptionByte;
	});
}

#if WITH_EDITOR
EDataValidationResult UAttachmentIconSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FAttachmentIconEntry& Entry = Entries[Index];

		// FindIcon's null-on-miss contract is for an ABSENT entry -- one that IS in the list but
		// never got an icon assigned is an authoring mistake, not a legitimate "no icon" case.
		if (!IsValid(Entry.Icon))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("AttachmentIconSet", "MissingIcon", "Entry {0} (Slot {1}, Option {2}) has no Icon assigned."),
				FText::AsNumber(Index), FText::AsNumber(Entry.KitSlotByte), FText::AsNumber(Entry.OptionByte)));
			Result = EDataValidationResult::Invalid;
		}

		// O(n^2) over ~9 entries -- editor-only validation, not a runtime lookup.
		for (int32 Other = Index + 1; Other < Entries.Num(); ++Other)
		{
			if (Entries[Other].KitSlotByte != Entry.KitSlotByte || Entries[Other].OptionByte != Entry.OptionByte) continue;

			Context.AddError(FText::Format(
				NSLOCTEXT("AttachmentIconSet", "DuplicateEntry",
					"Entries {0} and {1} both key on (Slot {2}, Option {3}) -- FindEntry would silently resolve to whichever comes first."),
				FText::AsNumber(Index), FText::AsNumber(Other), FText::AsNumber(Entry.KitSlotByte), FText::AsNumber(Entry.OptionByte)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif
