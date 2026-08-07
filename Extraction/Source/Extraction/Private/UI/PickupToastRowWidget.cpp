// UPickupToastRowWidget -- see header.

#include "UI/PickupToastRowWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "UI/PickupToastStackWidget.h"

void UPickupToastRowWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DwellTimerHandle);
		World->GetTimerManager().ClearTimer(ExitTimerHandle);
	}

	// This widget instance is RE-ADDED rather than rebuilt across a HUD teardown (NativeConstruct
	// re-runs on the SAME instance -- see e.g. ULootNotificationWidget.cpp's captured-style guard),
	// so a row that was active when the teardown happened would otherwise come back with bActive
	// still true, Visibility still un-collapsed, and no timer left running to ever retire it --
	// permanently wedging both the stack's cap check and its consolidation scan against a phantom
	// entry. Same three lines FinishExit runs on a normal expiry.
	SetVisibility(ESlateVisibility::Collapsed);
	bActive = false;
	RowConsolidationKey = NAME_None;

	Super::NativeDestruct();
}

void UPickupToastRowWidget::ShowEntry(FName Key, const FText& Category, const FText& ItemName, int32 Quantity, UTexture2D* Icon)
{
	// Defensive against reclaiming a row the stack retired via HideImmediately before its own
	// exit timer would have fired -- without this, that stale ExitTimerHandle could still land and
	// collapse the row this call just claimed.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DwellTimerHandle);
		World->GetTimerManager().ClearTimer(ExitTimerHandle);
	}

	// CreateWidget always parents a row to the WidgetTree of the stack that created it, which in
	// turn is owned by that stack -- GetTypedOuter walks both hops. Re-resolved every ShowEntry
	// rather than cached once at construct purely out of caution: nothing currently re-parents a
	// pooled row, but re-resolving costs nothing measurable against a lookup this infrequent.
	if (!OwningStack.IsValid())
		OwningStack = GetTypedOuter<UPickupToastStackWidget>();

	RowConsolidationKey = Key;
	RunningQuantity = Quantity;
	bActive = true;

	if (IsValid(CategoryText)) CategoryText->SetText(Category);
	if (IsValid(ItemNameText)) ItemNameText->SetText(ItemName);
	if (IsValid(ItemIcon)) ItemIcon->SetBrushFromTexture(Icon, true);
	RefreshQuantityText();

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	OnToastEnter();
	RestartDwellTimer();
}

void UPickupToastRowWidget::AddQuantity(int32 Extra)
{
	// The stack should never call this on a free row, but a stale reference landing here must not
	// resurrect a collapsed row with nothing on screen to attach the quantity to.
	if (!bActive) return;

	RunningQuantity += Extra;
	RefreshQuantityText();

	// No OnToastEnter replay -- see the header comment on this function.
	RestartDwellTimer();
}

void UPickupToastRowWidget::HideImmediately()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DwellTimerHandle);
		World->GetTimerManager().ClearTimer(ExitTimerHandle);
	}

	SetVisibility(ESlateVisibility::Collapsed);
	bActive = false;
	RowConsolidationKey = NAME_None;

	NotifyStackOfRetirement();
}

void UPickupToastRowWidget::RestartDwellTimer()
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	World->GetTimerManager().SetTimer(DwellTimerHandle, this, &UPickupToastRowWidget::BeginExit,
		FMath::Max(DwellDuration, MinDwellDuration), /*bLoop=*/ false);
}

void UPickupToastRowWidget::BeginExit()
{
	OnToastExit();

	UWorld* World = GetWorld();
	// A rate of zero never fires -- collapse right away rather than stranding the row mid-exit.
	if (!IsValid(World) || ExitDuration <= 0.f)
	{
		FinishExit();
		return;
	}

	World->GetTimerManager().SetTimer(ExitTimerHandle, this, &UPickupToastRowWidget::FinishExit, ExitDuration, /*bLoop=*/ false);
}

void UPickupToastRowWidget::FinishExit()
{
	SetVisibility(ESlateVisibility::Collapsed);
	bActive = false;
	RowConsolidationKey = NAME_None;

	NotifyStackOfRetirement();
}

void UPickupToastRowWidget::NotifyStackOfRetirement()
{
	if (UPickupToastStackWidget* Stack = OwningStack.Get())
		Stack->NotifyRowRetired();
}

void UPickupToastRowWidget::RefreshQuantityText()
{
	if (!IsValid(QuantityText)) return;

	QuantityText->SetText(FText::Format(
		NSLOCTEXT("PickupToast", "RowQuantityFormat", "+{0}"), FText::AsNumber(RunningQuantity)));
}
