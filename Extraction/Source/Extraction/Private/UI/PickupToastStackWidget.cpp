// UPickupToastStackWidget -- see header.

#include "UI/PickupToastStackWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"

#include "Game/ExtractionPlayerController.h"
#include "UI/AttachmentIconSet.h"
#include "UI/PickupToastRowWidget.h"

DEFINE_LOG_CATEGORY(LogPickupToast);

void UPickupToastStackWidget::NativeConstruct()
{
	Super::NativeConstruct();

	const int32 Cap = FMath::Max(MaxVisibleToasts, 1);
	RowPool.Reserve(Cap);
	VisibleOrder.Reserve(Cap);

	UWorld* World = GetWorld();
	UMissionInventorySubsystem* Subsystem = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (IsValid(Subsystem))
	{
		CachedSubsystem = Subsystem;
		if (!Subsystem->OnLootGranted.IsAlreadyBound(this, &UPickupToastStackWidget::HandleLootGranted))
			Subsystem->OnLootGranted.AddDynamic(this, &UPickupToastStackWidget::HandleLootGranted);
	}

	// Self-registration, same shape as UAttachmentStatPreviewWidget: the controller lives outside
	// the HUD module tree and cannot name a widget inside it without a hardcoded asset path, so
	// this widget hands the controller a live pointer instead of the controller creating/owning it.
	AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer());
	UE_CLOG(!PC, LogPickupToast, Warning,
		TEXT("%s: no AExtractionPlayerController -- kit attachment pickups will not reach this stack"), *GetName());
	if (PC)
		PC->RegisterPickupToastStack(this);
}

void UPickupToastStackWidget::NativeDestruct()
{
	if (AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer()))
		PC->ClearPickupToastStack(this);

	if (UMissionInventorySubsystem* Subsystem = CachedSubsystem.Get())
		Subsystem->OnLootGranted.RemoveDynamic(this, &UPickupToastStackWidget::HandleLootGranted);

	// This widget instance is RE-ADDED rather than rebuilt across a HUD teardown (RestoreHUD re-adds
	// the same object; NativeConstruct then re-runs on it, same as ULootNotificationWidget's captured-
	// style guard). Every pooled row already forced itself Collapsed/inactive in its own NativeDestruct
	// for the same reason, but VisibleOrder is this widget's OWN bookkeeping and nothing else clears
	// it -- without this, a re-added stack would believe rows are still live that the teardown just
	// silently retired, wedging the cap check and the consolidation scan against phantom entries.
	VisibleOrder.Reset();

	Super::NativeDestruct();
}

void UPickupToastStackWidget::ShowAttachmentPickup(uint8 KitSlotByte, uint8 OptionByte, int32 Quantity)
{
	// Attachments have no C++ acquisition signal at all -- see the class comment. Same
	// AttachmentIcons lookup UAttachmentStatPreviewWidget uses for the world focus prompt, keyed on
	// the (slot, option) PAIR: the kit's option enum is a separate enum per slot, so the option
	// byte alone is not a unique identity.
	UTexture2D* Icon = IsValid(AttachmentIcons) ? AttachmentIcons->FindIcon(KitSlotByte, OptionByte) : nullptr;
	const FText ItemName = IsValid(AttachmentIcons) ? AttachmentIcons->FindDisplayName(KitSlotByte, OptionByte) : FText::GetEmpty();

	ShowPickup(AttachmentCategoryText, ItemName, Quantity, Icon, BuildAttachmentConsolidationKey(KitSlotByte, OptionByte));
}

void UPickupToastStackWidget::ShowPickup(const FText& Category, const FText& ItemName, int32 Quantity, UTexture2D* Icon, FName ConsolidationKey)
{
	if (!IsValid(ToastContainer) || !RowWidgetClass) return; // nothing to render into

	// A row's own dwell/exit timers can retire it with no push notification back here -- this is
	// the one place that must see the true live count before either the consolidation scan or the
	// capacity check below runs.
	PruneVisibleOrder();

	if (ConsolidationKey != NAME_None)
	{
		UPickupToastRowWidget* Existing = FindVisibleRow(ConsolidationKey);
		if (IsValid(Existing))
		{
			Existing->AddQuantity(Quantity);
			return;
		}
	}

	const int32 Cap = FMath::Max(MaxVisibleToasts, 1);
	if (VisibleOrder.Num() >= Cap)
	{
		// Retire the oldest (visual top) immediately, not via its own dwell/exit -- that row has no
		// dwell left to finish honestly once a fourth toast needs its slot right now.
		UPickupToastRowWidget* Oldest = VisibleOrder[0];
		VisibleOrder.RemoveAt(0);
		if (IsValid(Oldest)) Oldest->HideImmediately();
	}

	UPickupToastRowWidget* Row = ClaimFreeRow();
	if (!IsValid(Row)) return;

	Row->ShowEntry(ConsolidationKey, Category, ItemName, Quantity, Icon);
	VisibleOrder.Add(Row); // newest -- last in VisibleOrder is always the newest, see RepositionVisibleRows
	RepositionVisibleRows();
}

void UPickupToastStackWidget::HandleLootGranted(ELootType Type, int32 Amount, const FText& Label, EEnemyWeaponAnimType AmmoCategory)
{
	// Label is DELIBERATELY IGNORED for Ammo and Stim -- do not "fix" this by using it. For those two
	// types the subsystem broadcasts the ALERT-CHANNEL SENTENCE ("+30 Rifle ammo", "+1 Stim"), not a
	// name: two dedup layers pair a grant to its alert copy by FText::EqualTo against that exact
	// string (UExtractionHudBridgeComponent and ULootNotificationWidget), so it must stay a sentence
	// and therefore cannot also be the row's display name -- rendering it produced "RIFLE AMMUNITION /
	// +30 Rifle ammo / +30". The bare name comes from this widget's own designer-facing texts instead.
	// Keycards still use Label: that path already carries a real card name.
	switch (Type)
	{
	case ELootType::Ammo:
		ShowPickup(ResolveAmmoCategoryText(AmmoCategory), ResolveAmmoName(AmmoCategory), Amount, ResolveAmmoIcon(AmmoCategory), BuildAmmoConsolidationKey(AmmoCategory));
		break;

	case ELootType::Keycard:
		// NAME_None, deliberately -- keycards are unique by nature, and a container granting several
		// DIFFERENT cards in one call must not merge them into one row showing the first card's name
		// with the wrong running total. NAME_None never matches anything (see FindVisibleRow), so
		// every keycard grant always gets its own row.
		ShowPickup(KeycardCategoryText, Label, Amount, KeycardIcon, NAME_None);
		break;

	case ELootType::Stim:
		// A single shared key, unlike keycards above -- stims ARE fungible, so repeated grants
		// should consolidate into one running total.
		ShowPickup(StimCategoryText, StimNameText, Amount, StimIcon, FName(TEXT("PickupToastStim")));
		break;

	default:
		break;
	}
}

UTexture2D* UPickupToastStackWidget::ResolveAmmoIcon(EEnemyWeaponAnimType AmmoCategory) const
{
	switch (AmmoCategory)
	{
	case EEnemyWeaponAnimType::SMG:    return SmgAmmoIcon;
	case EEnemyWeaponAnimType::Pistol: return PistolAmmoIcon;
	case EEnemyWeaponAnimType::Sniper: return SniperAmmoIcon;
	// Rifle, LMG and Shotgun share the rifle silhouette -- see the header comment on this function.
	case EEnemyWeaponAnimType::Rifle:
	case EEnemyWeaponAnimType::LMG:
	case EEnemyWeaponAnimType::Shotgun:
	default:
		return RifleAmmoIcon;
	}
}

const FText& UPickupToastStackWidget::ResolveAmmoCategoryText(EEnemyWeaponAnimType AmmoCategory) const
{
	switch (AmmoCategory)
	{
	case EEnemyWeaponAnimType::SMG:    return SmgAmmoCategoryText;
	case EEnemyWeaponAnimType::Pistol: return PistolAmmoCategoryText;
	case EEnemyWeaponAnimType::Sniper: return SniperAmmoCategoryText;
	case EEnemyWeaponAnimType::Rifle:
	case EEnemyWeaponAnimType::LMG:
	case EEnemyWeaponAnimType::Shotgun:
	default:
		return RifleAmmoCategoryText;
	}
}

const FText& UPickupToastStackWidget::ResolveAmmoName(EEnemyWeaponAnimType AmmoCategory) const
{
	switch (AmmoCategory)
	{
	case EEnemyWeaponAnimType::SMG:    return SmgAmmoNameText;
	case EEnemyWeaponAnimType::Pistol: return PistolAmmoNameText;
	case EEnemyWeaponAnimType::Sniper: return SniperAmmoNameText;
	// Rifle, LMG and Shotgun share the rifle NAME, matching ResolveAmmoIcon/ResolveAmmoCategoryText
	// above exactly -- the three lines of a row must agree, so this grouping is not independently
	// tunable. Display-only: BuildAmmoConsolidationKey still keys on the real category.
	case EEnemyWeaponAnimType::Rifle:
	case EEnemyWeaponAnimType::LMG:
	case EEnemyWeaponAnimType::Shotgun:
	default:
		return RifleAmmoNameText;
	}
}

FName UPickupToastStackWidget::BuildAmmoConsolidationKey(EEnemyWeaponAnimType AmmoCategory)
{
	// Keyed on the raw enum value, never on the (possibly shared) display category -- Rifle, LMG
	// and Shotgun render identically via ResolveAmmoIcon/ResolveAmmoCategoryText above but are
	// different pools and must never consolidate into one row.
	return FName(*FString::Printf(TEXT("PickupToastAmmo_%d"), static_cast<int32>(AmmoCategory)));
}

FName UPickupToastStackWidget::BuildAttachmentConsolidationKey(uint8 KitSlotByte, uint8 OptionByte)
{
	// Both bytes together, deliberately -- the kit's option enum is a separate enum per slot, so
	// OptionByte alone is not a unique attachment identity (see ShowAttachmentPickup's comment).
	return FName(*FString::Printf(TEXT("PickupToastAttachment_%u_%u"), KitSlotByte, OptionByte));
}

void UPickupToastStackWidget::PruneVisibleOrder()
{
	VisibleOrder.RemoveAll([](const TObjectPtr<UPickupToastRowWidget>& Row)
	{
		return !IsValid(Row) || !Row->IsActive();
	});
}

UPickupToastRowWidget* UPickupToastStackWidget::FindVisibleRow(FName Key) const
{
	if (Key == NAME_None) return nullptr; // never consolidate an unkeyed pickup -- see ShowPickup's comment

	for (const TObjectPtr<UPickupToastRowWidget>& Row : VisibleOrder)
	{
		if (IsValid(Row) && Row->GetConsolidationKey() == Key) return Row;
	}
	return nullptr;
}

UPickupToastRowWidget* UPickupToastStackWidget::ClaimFreeRow()
{
	for (const TObjectPtr<UPickupToastRowWidget>& PooledRow : RowPool)
	{
		if (IsValid(PooledRow) && !PooledRow->IsActive()) return PooledRow;
	}

	// ShowPickup always evicts the oldest visible row before calling here, so the pool should never
	// be asked to grow past the cap -- this is a floor against a mis-set MaxVisibleToasts, not the
	// normal path.
	if (RowPool.Num() >= FMath::Max(MaxVisibleToasts, 1)) return nullptr;

	// A null class means a silently empty stack, so say so once rather than shipping a blank box.
	if (!ensureMsgf(RowWidgetClass != nullptr,
		TEXT("RowWidgetClass not assigned on %s -- the pickup toast stack will render nothing."), *GetName()))
		return nullptr;

	UPickupToastRowWidget* NewRow = CreateWidget<UPickupToastRowWidget>(this, RowWidgetClass);
	if (!IsValid(NewRow)) return nullptr;

	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(ToastContainer->AddChild(NewRow));
	if (!IsValid(CanvasSlot)) return nullptr;

	// Anchors/alignment/size authored once, identically, for every row -- POSITION is then owned
	// entirely by RepositionVisibleRows via this same slot's SetPosition, never touched here again
	// and never through the row's render transform (see the class comment for why that channel is
	// reserved for the designer's enter/exit animation).
	CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
	CanvasSlot->SetAlignment(FVector2D(0.f, 0.f));
	CanvasSlot->SetPosition(FVector2D(0.f, 0.f));
	CanvasSlot->SetSize(FVector2D(RowWidth, RowHeight));

	RowPool.Add(NewRow);
	return NewRow;
}

void UPickupToastStackWidget::RepositionVisibleRows()
{
	// Newest (last element) at Y = 0; each row above it steps up by (RowHeight + RowGap). Negative Y
	// because the stack grows upward from a bottom-anchored container. Written through the CANVAS
	// SLOT, never the row's render transform -- see the class comment for why the render transform
	// is reserved for the designer's enter/exit animation and would otherwise fight it.
	const float Step = RowHeight + RowGap;
	const int32 Count = VisibleOrder.Num();

	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (!IsValid(VisibleOrder[Index])) continue;

		UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(VisibleOrder[Index]->Slot);
		if (!IsValid(CanvasSlot)) continue;

		const int32 DistanceFromNewest = (Count - 1) - Index;
		CanvasSlot->SetPosition(FVector2D(0.f, -DistanceFromNewest * Step));
	}
}

void UPickupToastStackWidget::NotifyRowRetired()
{
	// ShowPickup's eviction path calls a row's HideImmediately synchronously (before it has added
	// the new row or repositioned anything), which reports back in here while ShowPickup itself is
	// still mid-update. Skipping the nested call is correct, not just safe: ShowPickup runs its own
	// PruneVisibleOrder + RepositionVisibleRows pass immediately afterward regardless, over the
	// fully up-to-date VisibleOrder -- redoing that work here first would be pure waste, not a
	// correctness fix.
	if (bHandlingRowRetirement) return;
	bHandlingRowRetirement = true;

	// Safe mid-teardown: with VisibleOrder already reset (NativeDestruct) both calls below are
	// no-ops over an empty array.
	PruneVisibleOrder();
	RepositionVisibleRows();

	bHandlingRowRetirement = false;
}
