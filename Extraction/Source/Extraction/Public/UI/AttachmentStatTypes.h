// Attachment stat-preview row types -- shared by the preview panel, its row widget, and any future
// loadout UI that wants the same "what would change if I fitted this" numbers.

#pragma once

#include "CoreMinimal.h"
#include "AttachmentStatTypes.generated.h"

/**
 * The stat rows an attachment comparison can produce, in the exact order
 * UAttachmentStatPreviewWidget::BuildStatDeltas emits them. The builder always returns one entry
 * per value below whenever the slot resolves, so a caller can index the result with this enum to
 * override a label or read a single stat. Filtering (dropping sub-epsilon rows) is the caller's job.
 *
 * Rows name the MEASURED QUANTITY, never the desirability, so the sign is never ambiguous:
 * "ADS Time" (a lower transition multiplier is less time -- a green negative), not "ADS Speed".
 */
UENUM(BlueprintType)
enum class EAttachmentStatRow : uint8
{
	Damage,
	VerticalRecoil,
	HorizontalRecoil,
	ADSTime,
	ADSMoveSpeed,
	HipSpread,
	Noise,
	NoiseRange,
	EffectiveRange,
	Zoom,
	Count UMETA(Hidden)
};

/** Number of rows BuildStatDeltas emits for a slot that carries gameplay options. */
inline constexpr int32 AttachmentStatRowCount = static_cast<int32>(EAttachmentStatRow::Count);

/**
 * One line of the attachment stat preview: how a candidate attachment compares to whatever is
 * ALREADY fitted in the same slot, as a signed percentage of the fitted value.
 */
USTRUCT(BlueprintType)
struct EXTRACTION_API FAttachmentStatDelta
{
	GENERATED_BODY()

	/** Display label for the measured quantity, e.g. "Vertical Recoil". */
	UPROPERTY(BlueprintReadOnly, Category = "Attachments|Stats")
	FText Label;

	/** Signed change vs the fitted option, in percent. -15 = fifteen percent LESS of the measured
	 *  quantity, whether that is good (recoil) or bad (damage). */
	UPROPERTY(BlueprintReadOnly, Category = "Attachments|Stats")
	float PercentDelta = 0.f;

	/** True when the change helps the player. Direction is per-stat: less recoil is better, more
	 *  damage is better -- so this, not the sign, drives the colour. */
	UPROPERTY(BlueprintReadOnly, Category = "Attachments|Stats")
	bool bIsImprovement = false;

	/** True when the old multiplier was effectively zero and the new one is not (or vice versa).
	 *  A ratio against zero is unbounded, so PercentDelta is meaningless -- the row widget renders
	 *  this as a distinct string rather than a misleading "+999%" or "+0%". */
	UPROPERTY(BlueprintReadOnly, Category = "Attachments|Stats")
	bool bUnboundedChange = false;
};
