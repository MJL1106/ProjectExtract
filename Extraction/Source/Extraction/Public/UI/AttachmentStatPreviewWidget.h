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
//   UTextBlock   "ItemNameText"  -- (optional) the focused attachment's or candidate weapon's name;
//                                  shown whenever it's known, including most message states -- blank
//                                  only when a message genuinely has no single item (e.g. same-weapon)
//   UTextBlock   "HeadingText"   -- (optional) "ATTACHMENT EFFECTS" or the weapon-compare heading;
//                                  stays populated across rows AND every message state; only
//                                  HidePreview blanks it, since that's when the panel goes away
//   UImage       "ItemIcon"      -- (optional) the attachment's icon, resolved through
//                                  AttachmentIcons (UAttachmentIconSet) -- the same asset the HUD
//                                  pickup toast reads, so an attachment can't look different in the
//                                  two places. Null on ShowForWeapon (no icon source for a weapon
//                                  comparison) and on any AttachmentIcons miss.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExtractionTypes.h"
#include "UI/AttachmentStatTypes.h"
#include "AttachmentStatPreviewWidget.generated.h"

class AWeaponBase;
class UAttachmentIconSet;
class UAttachmentStatRowWidget;
class UImage;
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
	 * ItemIcon is resolved through AttachmentIcons on every call, deliberately outside the row-rebuild
	 * cache below -- that cache tracks (weapon, selection, slot, option, compat) only, and the icon can
	 * change independently of all five (AttachmentIcons reassigned, or null on an early call and wired
	 * up later), so gating it behind the same cache would leave a stale icon on screen once those five
	 * happened to match again.
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

	/** The icon/name UAttachmentIconSet resolved for the attachment ShowForAttachment is CURRENTLY
	 *  previewing -- the world focus prompt (WBP_PickupFocus) has no icon of its own on the
	 *  interact path (see UExtractionHudBridgeComponent::OnPickupFocusChangedBP) and reads these
	 *  instead, via GetAttachmentStatPreview(). Null / empty whenever this panel is not showing an
	 *  attachment: before the first ShowForAttachment, after HidePreview, and while ShowForWeapon's
	 *  comparison is on screen instead -- a weapon comparison is not an attachment and must not
	 *  leave a stale attachment icon readable through these. Resolved unconditionally, same as
	 *  ItemIcon above, never gated behind the row-rebuild cache. */
	UFUNCTION(BlueprintPure, Category = "UI|Attachments")
	UTexture2D* GetFocusedAttachmentIcon() const { return FocusedAttachmentIcon; }

	UFUNCTION(BlueprintPure, Category = "UI|Attachments")
	FText GetFocusedAttachmentName() const { return FocusedAttachmentName; }

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

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HeadingText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ItemIcon;

	// --- Designer configuration ---

	/** WBP row class created into StatContainer. Nothing renders until this is assigned. */
	UPROPERTY(EditAnywhere, Category = "Attachments|Preview")
	TSubclassOf<UAttachmentStatRowWidget> RowWidgetClass;

	/** Same asset the HUD pickup toast reads (UPickupToastStackWidget::AttachmentIcons) -- the one
	 *  source of attachment icon identity, so this panel and the toast can't disagree. Null while
	 *  unassigned; ShowForAttachment then resolves no icon rather than a wrong one. */
	UPROPERTY(EditAnywhere, Category = "Attachments|Preview")
	TObjectPtr<UAttachmentIconSet> AttachmentIcons;

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

	/** Panel heading while comparing a candidate attachment. The same panel doubles as a weapon
	 *  comparison (see WeaponHeadingText below), so this cannot be baked into the WBP as static text. */
	UPROPERTY(EditDefaultsOnly, Category = "Attachments|Messages")
	FText AttachmentHeadingText = NSLOCTEXT("AttachmentStats", "Heading", "ATTACHMENT EFFECTS");

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

	/** Panel heading while comparing a candidate weapon — see AttachmentHeadingText above. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapons|Messages")
	FText WeaponHeadingText = NSLOCTEXT("WeaponCompare", "Heading", "WEAPON COMPARISON");

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

	/** Shows a single state message with no rows. Heading is the caller's mode heading
	 *  (AttachmentHeadingText or WeaponHeadingText) so the panel keeps its title even on a message
	 *  state; ItemName is the specific item the message is about when the caller has one to give
	 *  (left blank for a message with no single item, e.g. ShowForWeapon's SameWeapon). */
	void ShowMessageOnly(const FText& Message, const FText& Heading, const FText& ItemName = FText::GetEmpty());

	/** Names the panel and sets its mode heading. Called from every row-populated success path and
	 *  from ShowMessageOnly -- HidePreview is the only path that bypasses this, since the panel is
	 *  leaving the screen entirely rather than showing a different state. */
	void SetItemHeader(const FText& ItemName, const FText& Heading);

	/** Blanks ItemNameText/HeadingText. Only HidePreview calls this now -- everything else keeps the
	 *  heading up, so this exists purely for the panel-going-away case. */
	void ClearItemHeader();

	/** Builds the six weapon-vs-weapon rows. A member rather than a static because it reads this
	 *  instance's designer labels directly — the attachment path's static builder has to fix labels
	 *  up afterwards by row ordinal, which only works for one fixed row set. */
	TArray<FAttachmentStatDelta> BuildWeaponDeltas(const UWeaponDataAsset& Held, const UWeaponDataAsset& Candidate) const;

	/** Row widgets created into StatContainer, in creation order. Reused across shows as the
	 *  crosshair sweeps between pickups; surplus entries are collapsed, never destroyed. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAttachmentStatRowWidget>> RowWidgets;

	/** Backing store for GetFocusedAttachmentIcon/GetFocusedAttachmentName -- see those getters'
	 *  comment for the exact clear contract. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FocusedAttachmentIcon;

	FText FocusedAttachmentName;

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
