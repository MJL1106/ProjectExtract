// UDamageNumberWidget / UDamageNumberEntryWidget implementation.

#include "UI/DamageNumberWidget.h"
#include "ExtractionPlayerController.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetLayoutLibrary.h"

// ---- Entry ----

void UDamageNumberEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SetRenderOpacity(0.f);
}

void UDamageNumberEntryWidget::SetValue(int32 Value, const FLinearColor& Color)
{
	if (!TotalText) return;
	TotalText->SetText(FText::AsNumber(Value));
	TotalText->SetColorAndOpacity(FSlateColor(Color));
}

// ---- Layer ----

void UDamageNumberWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	BuildPool();

	AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer());
	if (IsValid(PC) && !PC->OnDamageDealt.IsAlreadyBound(this, &UDamageNumberWidget::HandleDamageDealt))
		PC->OnDamageDealt.AddDynamic(this, &UDamageNumberWidget::HandleDamageDealt);
}

void UDamageNumberWidget::NativeDestruct()
{
	if (AExtractionPlayerController* PC = Cast<AExtractionPlayerController>(GetOwningPlayer()))
		PC->OnDamageDealt.RemoveDynamic(this, &UDamageNumberWidget::HandleDamageDealt);

	Super::NativeDestruct();
}

void UDamageNumberWidget::BuildPool()
{
	if (!NumberCanvas || !EntryWidgetClass || Entries.Num() > 0) return;

	Entries.Reserve(PoolSize);
	for (int32 i = 0; i < PoolSize; ++i)
	{
		UDamageNumberEntryWidget* Entry = CreateWidget<UDamageNumberEntryWidget>(this, EntryWidgetClass);
		if (!Entry) continue;

		// Anchored top-left, centred alignment, auto-size — RenderTranslation (set per tick)
		// then places the entry's centre at its stack position (ObjectiveMarkerLayer pattern).
		UCanvasPanelSlot* CanvasSlot = NumberCanvas->AddChildToCanvas(Entry);
		if (CanvasSlot)
		{
			CanvasSlot->SetAutoSize(true);
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CanvasSlot->SetPosition(FVector2D::ZeroVector);
		}

		FNumberEntry& PoolEntry = Entries.AddDefaulted_GetRef();
		PoolEntry.Widget = Entry;
	}
}

void UDamageNumberWidget::HandleDamageDealt(AActor* Victim, float Damage, float HeadshotDamage, bool bKilled, FVector WorldLocation)
{
	// Claim a free entry, or recycle the stalest one; push every other active number one line up.
	FNumberEntry* Claimed = nullptr;
	for (FNumberEntry& E : Entries)
	{
		if (!E.Widget) continue;
		if (!E.bActive) { Claimed = &E; break; }
		if (!Claimed || E.TimeSinceHit > Claimed->TimeSinceHit) Claimed = &E;
	}
	if (!Claimed) return;

	for (FNumberEntry& E : Entries)
	{
		if (E.bActive && &E != Claimed)
			E.StackOffset += LineHeight;
	}

	Claimed->bActive = true;
	Claimed->TimeSinceHit = 0.f;
	Claimed->StackOffset = 0.f;
	Claimed->Widget->SetValue(FMath::RoundToInt(Damage),
		HeadshotDamage > 0.f ? HeadshotNumberColor : HitNumberColor);
	Claimed->Widget->SetRenderOpacity(1.f);
}

void UDamageNumberWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	// Fixed readout anchor: viewport center + designer offset (widget-space units, DPI-safe).
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC).GetLocalSize();
	if (ViewportSize.X <= KINDA_SMALL_NUMBER) return;
	const FVector2D AnchorPos = ViewportSize * 0.5f + NumberScreenOffset;

	for (FNumberEntry& E : Entries)
	{
		if (!E.bActive || !E.Widget) continue;

		E.TimeSinceHit += InDeltaTime;

		float Opacity = 1.f;
		if (E.TimeSinceHit > LingerDuration)
		{
			const float FadeT = (E.TimeSinceHit - LingerDuration) / FadeDuration;
			if (FadeT >= 1.f)
			{
				E.bActive = false;
				E.Widget->SetRenderOpacity(0.f);
				continue;
			}
			Opacity = 1.f - FadeT;
		}

		E.Widget->SetRenderTranslation(AnchorPos - FVector2D(0.f, E.StackOffset));
		E.Widget->SetRenderOpacity(Opacity);
	}
}
