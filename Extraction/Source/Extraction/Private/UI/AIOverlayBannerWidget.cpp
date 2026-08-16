// UAIOverlayBannerWidget implementation.

#include "UI/AIOverlayBannerWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UAIOverlayBannerWidget::PlayEvent(const FSquadOverlayEvent& Event)
{
	CurrentKind = Event.Kind;
	bEmphasis = (Event.Kind == EOverlaySquadEventKind::OfficerDown);

	if (HeadlineText) HeadlineText->SetText(Event.Headline);

	if (DetailText)
	{
		DetailText->SetText(Event.Detail);
		DetailText->SetVisibility(Event.Detail.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (BannerRoot) BannerRoot->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	CurrentPhase = EBannerPhase::Holding;
	if (IsConstructed()) OnBannerEventShown(CurrentKind, Event.Headline, Event.Detail, bEmphasis);

	if (UWorld* World = GetWorld())
	{
		const float HoldSeconds = bEmphasis ? OfficerDownHoldDurationSeconds : HoldDurationSeconds;
		World->GetTimerManager().ClearTimer(PhaseTimerHandle);
		World->GetTimerManager().SetTimer(PhaseTimerHandle, this,
			&UAIOverlayBannerWidget::HandleHoldExpired, FMath::Max(HoldSeconds, 0.01f), false);
	}
}

void UAIOverlayBannerWidget::HandleHoldExpired()
{
	BeginFade();
}

void UAIOverlayBannerWidget::BeginFade()
{
	CurrentPhase = EBannerPhase::Fading;
	if (IsConstructed()) OnBannerEventFading();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PhaseTimerHandle);
		World->GetTimerManager().SetTimer(PhaseTimerHandle, this,
			&UAIOverlayBannerWidget::HandleFadeExpired, FMath::Max(FadeDurationSeconds, 0.01f), false);
	}
}

void UAIOverlayBannerWidget::HandleFadeExpired()
{
	EndCycle();
}

void UAIOverlayBannerWidget::EndCycle()
{
	CurrentPhase = EBannerPhase::Idle;
	bEmphasis = false;
	CurrentKind = EOverlaySquadEventKind::None;

	// Collapsed rather than merely faded -- an idle banner must not eat hit-tests or draw calls
	// between events, matching UAIOverlayCardWidget's own never-tick-while-hidden reasoning in
	// reverse (here it is fine to collapse: this widget runs no per-frame projection to lose).
	if (BannerRoot) BannerRoot->SetVisibility(ESlateVisibility::Collapsed);
	else SetVisibility(ESlateVisibility::Collapsed);

	if (IsConstructed()) OnBannerEventHidden();
}

void UAIOverlayBannerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ApplyStaticColors();
	EndCycle();
}

void UAIOverlayBannerWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(PhaseTimerHandle);

	Super::NativeDestruct();
}

void UAIOverlayBannerWidget::ApplyStaticColors()
{
	if (HeadlineText) HeadlineText->SetColorAndOpacity(FSlateColor(InkColor));
	if (DetailText) DetailText->SetColorAndOpacity(FSlateColor(InkColor));
}
