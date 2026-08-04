// UAttachmentStatPreviewWidget -- HUD panel shown while the player looks at a world attachment
// pickup. Lists ONLY the stats that would CHANGE if they took it, as signed percentages against
// whatever is already fitted in that same slot, green for better and red for worse.
//
// The Blueprint pickup calls ShowForAttachment with the two RAW kit bytes it already carries plus
// the compatibility answer it already computes; the panel resolves the held weapon itself. The kit
// ENUM_AttachmentSlot byte is translated to the C++ slot in ONE place -- see the mapping table at
// the top of AttachmentStatPreviewWidget.cpp.
//
// WBP must contain:
//   UPanelWidget "StatContainer" -- MUST be a multi-child panel (VerticalBox / ScrollBox); rows
//                                  are created into and reused inside this panel
//   UTextBlock   "MessageText"   -- (optional) one-line state message (incompatible / no change / cosmetic)

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionTypes.h"
#include "UI/AttachmentStatTypes.h"
#include "AttachmentStatPreviewWidget.generated.h"

class AWeaponBase;
class UAttachmentStatRowWidget;
class UPanelWidget;
class UTextBlock;
class UWeaponDataAsset;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UAttachmentStatPreviewWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/**
	 * Points the panel at a candidate attachment and rebuilds its rows in place.
	 * KitSlotByte / OptionByte are the RAW kit ENUM_AttachmentSlot and ST_Attachments bytes the
	 * pickup carries -- do NOT pre-convert them to EAttachmentSlot, the orders differ.
	 * bCompatible is the pickup's own answer (the weapon DA's Accepted*Options check).
	 * Cheap enough to call every frame the crosshair rests on a pickup: rows are reused, not rebuilt.
	 */
	UFUNCTION(BlueprintCallable, Category = "UI|Attachments")
	void ShowForAttachment(uint8 KitSlotByte, uint8 OptionByte, bool bCompatible);

	/**
	 * Points the panel at a candidate WEAPON and rebuilds its rows as a comparison against the
	 * weapon currently held — damage, fire rate, DPS, magazine, ADS time and recoil.
	 * Same panel, same row widgets; only the source of the numbers differs.
	 * Pass the candidate's UWeaponDataAsset (the weapon pickup knows its own data asset).
	 * Holding nothing, or looking at the weapon already held, collapses the panel.
	 */
	UFUNCTION(BlueprintCallable, Category = "UI|Weapons")
	void ShowForWeapon(UWeaponDataAsset* CandidateData);

	/** Collapses the panel and every row. Call when the crosshair leaves the pickup. */
	UFUNCTION(BlueprintCallable, Category = "UI|Attachments")
	void HidePreview();

	/**
	 * The maths with no UI attached, reusable by a future loadout screen.
	 * Returns AttachmentStatRowCount entries in EAttachmentStatRow order -- INCLUDING unchanged
	 * rows, so the result can be indexed by that enum -- or an empty array when the weapon/slot
	 * carries no gameplay options at all (kit Barrels, or a slot this weapon never authored).
	 * Labels come from this class's defaults; an instance overrides them with its own designer text.
	 */
	UFUNCTION(BlueprintPure, Category = "UI|Attachments")
	static TArray<FAttachmentStatDelta> BuildStatDeltas(AWeaponBase* Weapon, uint8 KitSlotByte, uint8 OptionByte);

	/** Designer-editable label for one row. Empty means "fall back to the built-in label".
	 *  Deliberately not a UFUNCTION — BP consumes labels through the rows, not one at a time. */
	const FText& GetRowLabel(EAttachmentStatRow Row) const;

protected:

	/** Also registers this panel with the owning AExtractionPlayerController, which is how the
	 *  Blueprint pickups reach it (GetAttachmentStatPreview). Self-registration rather than the
	 *  controller creating the widget: the panel now lives inside a HUD module the controller
	 *  neither owns nor can name without an asset path. */
	virtual void NativeConstruct() override;

	/** Releases the registration, but only if this instance still holds it — a module torn down
	 *  after its replacement was built must not clear the live panel. */
	virtual void NativeDestruct() override;

	// --- Bound widgets (designer wires these in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> StatContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MessageText;

	// --- Designer configuration ---

	/** WBP row class created into StatContainer. Nothing renders until this is assigned. */
	UPROPERTY(EditAnywhere, Category = "Attachments|Preview")
	TSubclassOf<UAttachmentStatRowWidget> RowWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Attachments|Preview")
	FLinearColor BetterColour = FLinearColor(0.35f, 0.85f, 0.40f, 1.f);

	UPROPERTY(EditAnywhere, Category = "Attachments|Preview")
	FLinearColor WorseColour = FLinearColor(0.90f, 0.32f, 0.28f, 1.f);

	/** Rows whose absolute percentage falls below this are dropped -- a 0.2% shift is noise the
	 *  player cannot act on. Floored at 0.5 because rows print whole percent: anything smaller
	 *  would render as a meaningless "+0%". Raise it to hide small changes too. */
	UPROPERTY(EditAnywhere, Category = "Attachments|Preview", meta = (ClampMin = "0.5"))
	float MinPercentToShow = 0.5f;

	// --- Designer-editable messages ---

	UPROPERTY(EditAnywhere, Category = "Attachments|Messages")
	FText IncompatibleMessage = NSLOCTEXT("AttachmentStats", "Incompatible", "Doesn't fit this weapon");

	UPROPERTY(EditAnywhere, Category = "Attachments|Messages")
	FText NoChangeMessage = NSLOCTEXT("AttachmentStats", "NoChange", "No stat change");

	UPROPERTY(EditAnywhere, Category = "Attachments|Messages")
	FText CosmeticOnlyMessage = NSLOCTEXT("AttachmentStats", "CosmeticOnly", "Cosmetic only");

	// --- Designer-editable stat labels (one per EAttachmentStatRow, same order) ---

	UPROPERTY(EditAnywhere, Category = "Attachments|Labels")
	FText DamageLabel = NSLOCTEXT("AttachmentStats", "Damage", "Damage");

	UPROPERTY(EditAnywhere, Category = "Attachments|Labels")
	FText VerticalRecoilLabel = NSLOCTEXT("AttachmentStats", "VerticalRecoil", "Vertical Recoil");

	UPROPERTY(EditAnywhere, Category = "Attachments|Labels")
	FText HorizontalRecoilLabel = NSLOCTEXT("AttachmentStats", "HorizontalRecoil", "Horizontal Recoil");

	/** Measured quantity is TIME, not speed -- a faster aim shows as a green negative. */
	UPROPERTY(EditAnywhere, Category = "Attachments|Labels")
	FText ADSTimeLabel = NSLOCTEXT("AttachmentStats", "ADSTime", "ADS Time");

	UPROPERTY(EditAnywhere, Category = "Attachments|Labels")
	FText ADSMoveSpeedLabel = NSLOCTEXT("AttachmentStats", "ADSMoveSpeed", "ADS Move Speed");

	UPROPERTY(EditAnywhere, Category = "Attachments|Labels")
	FText HipSpreadLabel = NSLOCTEXT("AttachmentStats", "HipSpread", "Hip Spread");

	UPROPERTY(EditAnywhere, Category = "Attachments|Labels")
	FText NoiseLabel = NSLOCTEXT("AttachmentStats", "Noise", "Noise");

	UPROPERTY(EditAnywhere, Category = "Attachments|Labels")
	FText NoiseRangeLabel = NSLOCTEXT("AttachmentStats", "NoiseRange", "Noise Range");

	UPROPERTY(EditAnywhere, Category = "Attachments|Labels")
	FText EffectiveRangeLabel = NSLOCTEXT("AttachmentStats", "EffectiveRange", "Effective Range");

	UPROPERTY(EditAnywhere, Category = "Attachments|Labels")
	FText ZoomLabel = NSLOCTEXT("AttachmentStats", "Zoom", "Zoom");

	// --- Weapon-vs-weapon comparison (ShowForWeapon) ---

	/** Shown when the candidate weapon IS the weapon already held — nothing to compare. */
	UPROPERTY(EditAnywhere, Category = "Weapons|Messages")
	FText SameWeaponMessage = NSLOCTEXT("WeaponCompare", "SameWeapon", "Already equipped");

	UPROPERTY(EditAnywhere, Category = "Weapons|Labels")
	FText FireRateLabel = NSLOCTEXT("WeaponCompare", "FireRate", "Fire Rate");

	/** Damage x fire rate x pellets — the headline number when comparing two guns. */
	UPROPERTY(EditAnywhere, Category = "Weapons|Labels")
	FText DPSLabel = NSLOCTEXT("WeaponCompare", "DPS", "DPS");

	UPROPERTY(EditAnywhere, Category = "Weapons|Labels")
	FText MagazineLabel = NSLOCTEXT("WeaponCompare", "Magazine", "Magazine");

	/** Mean magnitude of the recoil pattern's points — one comparable number out of a curve. */
	UPROPERTY(EditAnywhere, Category = "Weapons|Labels")
	FText WeaponRecoilLabel = NSLOCTEXT("WeaponCompare", "Recoil", "Recoil");

private:

	/** The player's held weapon, or null when nothing is in hand (nothing to compare against). */
	AWeaponBase* ResolveHeldWeapon() const;

	/** Fills rows from Deltas, skipping sub-epsilon entries, and collapses the surplus.
	 *  Returns how many rows ended up visible. */
	int32 PopulateRows(const TArray<FAttachmentStatDelta>& Deltas);

	/** Reuses RowWidgets[Index], creating and appending one only when the pool is short. */
	UAttachmentStatRowWidget* GetOrCreateRow(int32 Index);

	/** Collapses every pooled row from FirstIndex on -- reuse, never destroy. */
	void HideRowsFrom(int32 FirstIndex);

	/** Shows a single state message with no rows. */
	void ShowMessageOnly(const FText& Message);

	/** Builds the six weapon-vs-weapon rows. A member rather than a static because it reads this
	 *  instance's designer labels directly — the attachment path's static builder has to fix labels
	 *  up afterwards by row ordinal, which only works for one fixed row set. */
	TArray<FAttachmentStatDelta> BuildWeaponDeltas(const UWeaponDataAsset& Held, const UWeaponDataAsset& Candidate) const;

	/** Row widgets created into StatContainer, in creation order. Reused across shows as the
	 *  crosshair sweeps between pickups; surplus entries are collapsed, never destroyed. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAttachmentStatRowWidget>> RowWidgets;

	// --- Per-frame cache (ShowForAttachment is a pure function of these four) ---

	/** True when the content on screen matches the cached tuple below and no rebuild is needed. */
	bool IsCacheCurrent(AWeaponBase* Weapon, uint8 KitSlotByte, uint8 OptionByte, bool bCompatible) const;

	/** Invalidates the cache so the next ShowForAttachment rebuilds unconditionally. */
	void InvalidateCache();

	/** Weapon used for the last successful show. TWeakObjectPtr so a destroyed weapon cannot alias
	 *  a new one at the same address and silently skip a rebuild. */
	TWeakObjectPtr<AWeaponBase> CachedWeapon;

	/** The weapon's attachment selection at the time the cache was built. If the player fits
	 *  something while a pickup is still under the crosshair, this mismatches and forces a rebuild. */
	FWeaponAttachmentSelection CachedSelection;

	/** Candidate weapon data for the last ShowForWeapon. Doubles as the mode discriminator: the
	 *  attachment path clears it and the weapon path sets the slot bytes to 0xFF, so switching
	 *  between looking at an attachment and looking at a gun always misses the cache. */
	TWeakObjectPtr<const UWeaponDataAsset> CachedCandidateData;

	uint8 CachedKitSlotByte = 0xFF;
	uint8 CachedOptionByte = 0xFF;
	bool bCachedCompatible = false;
	bool bCacheValid = false;
};
