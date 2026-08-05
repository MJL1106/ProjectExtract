// UPickupToastStackWidget -- the HUD's pickup toast stack: up to MaxVisibleToasts rows, newest at
// the BOTTOM, oldest pushed upward and retired once the cap is exceeded. Two independent sources
// feed it:
//   * UMissionInventorySubsystem::OnLootGranted -- every successful ammo/stim/keycard acquisition.
//   * ShowAttachmentPickup -- attachments have NO C++ acquisition signal at all (the whole pickup
//     is kit-Blueprint-side), so the kit calls this directly as a BlueprintCallable entry point
//     rather than the widget subscribing to anything.
// Both funnel through ShowPickup, the generic escape hatch, which also does the consolidation: a
// repeated pickup of the same thing UPDATES the existing row's quantity instead of adding a second
// one. See ShowPickup's comment for how the consolidation key is built and why an ammo pool that
// shares its display icon/label with another (Rifle/LMG/Shotgun all render as "RIFLE AMMUNITION")
// must still NOT consolidate with it.
//
// SELF-REGISTRATION: like UAttachmentStatPreviewWidget, this widget registers itself with the
// owning AExtractionPlayerController on construct (RegisterPickupToastStack) and clears that
// registration on destruct (ClearPickupToastStack) rather than the controller creating/owning it.
// The controller lives outside the HUD module tree and cannot name a widget inside it without a
// hardcoded asset path, so the widget hands the controller a live pointer instead -- that pointer
// is how the kit's attachment-pickup Blueprint reaches GetPickupToastStack()->ShowAttachmentPickup.
//
// POOLING / POSITIONING: rows are a fixed pool of MaxVisibleToasts UPickupToastRowWidget instances,
// created once into ToastContainer and reused for the life of the widget -- never destroyed. Visual
// stacking is driven entirely by each row's CANVAS SLOT position, computed from its index in
// VisibleOrder (see RepositionVisibleRows) -- NOT by reordering ToastContainer's children, and NOT
// by the row's render transform.
//
// UPanelWidget::ShiftChild was tried first and does NOT work here: it only reorders UMG's Slots
// array and requests a re-layout, but the live SVerticalBox's child order is built exactly once, in
// UVerticalBox::RebuildWidget, from UVerticalBoxSlot::BuildSlot's unconditional AddSlot() append --
// nothing after that initial build ever re-reads the Slots array's order back into Slate. Every
// other engine caller of ShiftChild is editor-only for exactly this reason. Do not reinstate it.
// ToastContainer is a UCanvasPanel instead so C++ can position each row directly: the spec's exact
// geometry (420x58 rows, 6px gaps, newest row's top at a fixed offset) is already an index-to-Y
// computation, so driving it through the canvas slot is not a workaround, it is the design.
//
// Position is written through the CANVAS SLOT (UCanvasPanelSlot::SetPosition), never the row's
// render transform: a root Render Transform -> Translation track is exactly what UPickupToastRowWidget's
// own header tells the designer to author for the enter/exit slide, and that track overwrites the
// property every frame it plays and then holds its final keyframe -- C++ writing the SAME channel
// would either get overwritten while the animation plays or silently lose every future update once
// it finishes. The canvas slot is an independent channel a render-transform animation composes on
// top of instead of fighting, which is why repositioning through it is correct rather than merely
// convenient (see UObjectiveMarkerWidget::bDriveRenderTransform for the general form of this trap).
//
// A canvas's children are positionally independent, so nothing closes the gap a retiring row leaves
// behind on its own -- RepositionVisibleRows only ever runs when something calls it. ShowPickup
// covers the arrival case; UPickupToastRowWidget reports its OWN retirement back through
// NotifyRowRetired (both natural expiry and HideImmediately eviction) so the common case -- a row's
// dwell simply running out with no new pickup involved -- also closes the stack up rather than
// leaving a permanent hole. NotifyRowRetired guards against re-entrancy: ShowPickup's own eviction
// path calls a row's HideImmediately synchronously, which reports back into this same function
// while ShowPickup is still mid-update.
//
// BindWidget contract:
//   UCanvasPanel "ToastContainer" -- MUST be left EMPTY in the WBP. Rows are created into it at
//                                    runtime and reused; every row's canvas slot is authored ONCE
//                                    by ClaimFreeRow (anchors top-left, alignment (0,0), size
//                                    RowWidth x RowHeight) and its position is then owned entirely
//                                    by RepositionVisibleRows. The row's root Render Transform is
//                                    left completely free for the designer's enter/exit animation --
//                                    see UPickupToastRowWidget's header for why that channel is safe.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Enemy/EnemyTypes.h"
#include "Game/MissionInventorySubsystem.h"
#include "PickupToastStackWidget.generated.h"

class UAttachmentIconSet;
class UCanvasPanel;
class UPickupToastRowWidget;
class UTexture2D;

DECLARE_LOG_CATEGORY_EXTERN(LogPickupToast, Log, All);

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UPickupToastStackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The kit's attachment-pickup Blueprint calls this directly -- see the class comment for why
	 *  attachments have no subscription path. KitSlotByte/OptionByte are the raw kit
	 *  ENUM_AttachmentSlot/option bytes, resolved through AttachmentIcons exactly like
	 *  UAttachmentStatPreviewWidget resolves the same pair for the world focus prompt. */
	UFUNCTION(BlueprintCallable, Category = "UI|PickupToast")
	void ShowAttachmentPickup(uint8 KitSlotByte, uint8 OptionByte, int32 Quantity = 1);

	/**
	 * Generic entry point every specific pickup funnels through. ConsolidationKey is the identity
	 * a repeat pickup of the SAME thing must share -- e.g. two "5.56 NATO" grants must produce the
	 * same key so the second one updates the first row's quantity instead of adding a new one, while
	 * two DIFFERENT ammo pools that happen to render the same category label/icon (Rifle vs LMG vs
	 * Shotgun) must produce DIFFERENT keys so they never consolidate. NAME_None never matches
	 * anything, including another NAME_None call -- passing it means "always a new row", not "match
	 * whatever else forgot to key itself".
	 */
	UFUNCTION(BlueprintCallable, Category = "UI|PickupToast")
	void ShowPickup(const FText& Category, const FText& ItemName, int32 Quantity, UTexture2D* Icon, FName ConsolidationKey);

	/** Called by a row (never by BP) when it retires itself -- natural dwell/exit expiry, or a
	 *  forced HideImmediately. A canvas's children are positionally independent, so without this a
	 *  retiring row would leave a permanent gap: nothing else would ever prune VisibleOrder and
	 *  reposition the rest. Re-entrancy-guarded: ShowPickup's own eviction path calls a row's
	 *  HideImmediately synchronously, which reports back in here while ShowPickup is still
	 *  mid-update, and safe to call while this widget is itself tearing down (both PruneVisibleOrder
	 *  and RepositionVisibleRows degrade to a no-op against an empty/reset VisibleOrder). */
	UFUNCTION(BlueprintCallable, Category = "UI|PickupToast")
	void NotifyRowRetired();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// --- Bound widget (designer wires in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> ToastContainer;

	// --- Designer configuration ---

	/** WBP row class created into ToastContainer. Nothing renders until this is assigned. */
	UPROPERTY(EditAnywhere, Category = "PickupToast")
	TSubclassOf<UPickupToastRowWidget> RowWidgetClass;

	UPROPERTY(EditAnywhere, Category = "PickupToast", meta = (ClampMin = "1"))
	int32 MaxVisibleToasts = 3;

	/** Row height in slate units -- also the height every row's canvas slot is authored with, so
	 *  the vertical step RepositionVisibleRows computes can never drift from the slot's own size. */
	UPROPERTY(EditAnywhere, Category = "PickupToast", meta = (ClampMin = "0.0"))
	float RowHeight = 58.f;

	/** Gap between stacked rows in slate units. */
	UPROPERTY(EditAnywhere, Category = "PickupToast", meta = (ClampMin = "0.0"))
	float RowGap = 6.f;

	UPROPERTY(EditAnywhere, Category = "PickupToast|Ammo")
	TObjectPtr<UTexture2D> RifleAmmoIcon;

	UPROPERTY(EditAnywhere, Category = "PickupToast|Ammo")
	TObjectPtr<UTexture2D> SmgAmmoIcon;

	UPROPERTY(EditAnywhere, Category = "PickupToast|Ammo")
	TObjectPtr<UTexture2D> PistolAmmoIcon;

	UPROPERTY(EditAnywhere, Category = "PickupToast|Ammo")
	TObjectPtr<UTexture2D> SniperAmmoIcon;

	UPROPERTY(EditAnywhere, Category = "PickupToast|Ammo")
	FText RifleAmmoCategoryText = NSLOCTEXT("PickupToast", "RifleAmmo", "RIFLE AMMUNITION");

	UPROPERTY(EditAnywhere, Category = "PickupToast|Ammo")
	FText SmgAmmoCategoryText = NSLOCTEXT("PickupToast", "SmgAmmo", "SMG AMMUNITION");

	UPROPERTY(EditAnywhere, Category = "PickupToast|Ammo")
	FText PistolAmmoCategoryText = NSLOCTEXT("PickupToast", "PistolAmmo", "PISTOL AMMUNITION");

	UPROPERTY(EditAnywhere, Category = "PickupToast|Ammo")
	FText SniperAmmoCategoryText = NSLOCTEXT("PickupToast", "SniperAmmo", "SNIPER AMMUNITION");

	UPROPERTY(EditAnywhere, Category = "PickupToast|Attachments")
	FText AttachmentCategoryText = NSLOCTEXT("PickupToast", "AttachmentAcquired", "ATTACHMENT ACQUIRED");

	/** Same asset UAttachmentStatPreviewWidget reads (its AttachmentIcons) -- the one source of
	 *  attachment name/icon identity, so the focus prompt and this toast can't disagree. */
	UPROPERTY(EditAnywhere, Category = "PickupToast|Attachments")
	TObjectPtr<UAttachmentIconSet> AttachmentIcons;

	UPROPERTY(EditAnywhere, Category = "PickupToast|Keycard")
	FText KeycardCategoryText = NSLOCTEXT("PickupToast", "KeycardAcquired", "KEYCARD ACQUIRED");

	UPROPERTY(EditAnywhere, Category = "PickupToast|Keycard")
	TObjectPtr<UTexture2D> KeycardIcon;

	UPROPERTY(EditAnywhere, Category = "PickupToast|Stim")
	FText StimCategoryText = NSLOCTEXT("PickupToast", "StimAcquired", "STIM ACQUIRED");

	UPROPERTY(EditAnywhere, Category = "PickupToast|Stim")
	TObjectPtr<UTexture2D> StimIcon;

private:
	UFUNCTION()
	void HandleLootGranted(ELootType Type, int32 Amount, const FText& Label, EEnemyWeaponAnimType AmmoCategory);

	/** Rifle, LMG and Shotgun all resolve to the RIFLE icon/label below -- they're rifle-calibre
	 *  pools with no icon of their own in this pack, and a generic ammo-box fallback would read as
	 *  a broken row. This is a DISPLAY fallback only; BuildAmmoConsolidationKey below still keys on
	 *  the real category, so an LMG pool and a Rifle pool never consolidate just because they look
	 *  the same on screen. */
	UTexture2D* ResolveAmmoIcon(EEnemyWeaponAnimType AmmoCategory) const;
	const FText& ResolveAmmoCategoryText(EEnemyWeaponAnimType AmmoCategory) const;

	/** Keyed on the raw AmmoCategory value, never on the (possibly shared) display category --
	 *  see ResolveAmmoIcon's comment for why two different pools must not consolidate. */
	static FName BuildAmmoConsolidationKey(EEnemyWeaponAnimType AmmoCategory);

	/** Keyed on the (KitSlotByte, OptionByte) PAIR together -- the kit's option enum is a SEPARATE
	 *  enum per slot, so OptionByte 1 means a different attachment under every slot (e.g.
	 *  "Holosight" under the sight slot, "Laser" under the laser slot). Keying on either byte alone
	 *  would consolidate (or mis-icon) two entirely unrelated attachments that happen to share one
	 *  byte value -- this key, like UAttachmentIconSet::FindEntry, only ever compares the pair. */
	static FName BuildAttachmentConsolidationKey(uint8 KitSlotByte, uint8 OptionByte);

	/** Drops any VisibleOrder entry whose row has already retired itself (dwell/exit timers with no
	 *  push notification back to the stack -- see UPickupToastRowWidget::IsActive). Run before every
	 *  scan so the capacity check and the oldest-row lookup both see the true live count. */
	void PruneVisibleOrder();

	/** Linear scan over VisibleOrder for a row already showing Key. NAME_None never matches -- see
	 *  ShowPickup's comment on ConsolidationKey. */
	UPickupToastRowWidget* FindVisibleRow(FName Key) const;

	/** First pooled row that is not currently active, creating and adding one to ToastContainer
	 *  only when the pool has not yet grown to MaxVisibleToasts. A freshly created row's canvas
	 *  slot is authored once here (anchors top-left, alignment (0,0), offset 0,0, size RowWidth x
	 *  RowHeight) -- every row's POSITION is then owned purely by RepositionVisibleRows via that
	 *  same slot, never by touching the slot's anchors/alignment/size again and never by the row's
	 *  render transform (see the class comment for why the render transform is off limits). Null
	 *  when RowWidgetClass is unset or the pool is exhausted (should not happen: ShowPickup always
	 *  evicts before claiming). */
	UPickupToastRowWidget* ClaimFreeRow();

	/** Writes each VisibleOrder row's canvas slot position (UCanvasPanelSlot::SetPosition) from its
	 *  index: the newest (last element) sits at Y = 0, and each row above it steps up by
	 *  (RowHeight + RowGap) -- negative Y, since the stack grows upward from a bottom-anchored
	 *  container. Replaces reordering ToastContainer's children entirely -- see the class comment on
	 *  why UPanelWidget::ShiftChild cannot do this job -- and deliberately does NOT use the row's
	 *  render transform, which the designer's enter/exit animation owns (see the class comment).
	 *  Pooled rows not currently in VisibleOrder are left wherever their slot position last was --
	 *  they're collapsed, so it has no visual effect. Called from ShowPickup (arrival) and
	 *  NotifyRowRetired (retirement) -- see the class comment for why both call sites are needed. */
	void RepositionVisibleRows();

	/** Canvas slot width every row is authored with -- see ClaimFreeRow. Not a UPROPERTY: unlike
	 *  RowHeight, nothing else needs to read this value, so there is no drift to guard against by
	 *  making it designer-tunable. */
	static constexpr float RowWidth = 420.f;

	/** Re-entrancy latch for NotifyRowRetired -- see that function's comment. */
	bool bHandlingRowRetirement = false;

	TWeakObjectPtr<UMissionInventorySubsystem> CachedSubsystem;

	/** Every row widget ever created, in creation order -- the reuse pool. Never shrinks. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPickupToastRowWidget>> RowPool;

	/** Rows currently showing a live entry, oldest first / newest last. A SUBSET of RowPool, never
	 *  longer than MaxVisibleToasts. Repositioned by RepositionVisibleRows on every change, and
	 *  reset wholesale in NativeDestruct -- see that override's comment for why. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPickupToastRowWidget>> VisibleOrder;
};
