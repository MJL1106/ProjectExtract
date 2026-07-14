// UBarkFeedWidget — subscribes the subtitle feed to the bark subsystem and owns the line lifetime.

#include "BarkFeedWidget.h"
#include "BarkSubsystem.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UBarkFeedWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UWorld* World = GetWorld())
		if (UBarkSubsystem* Barks = World->GetSubsystem<UBarkSubsystem>())
			Barks->OnBarkRequested.AddUniqueDynamic(this, &UBarkFeedWidget::HandleBarkRequested);

	// Nothing is speaking yet — the design-time preview text must not show in game.
	ClearBarkLine();
}

void UBarkFeedWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		if (UBarkSubsystem* Barks = World->GetSubsystem<UBarkSubsystem>())
			Barks->OnBarkRequested.RemoveDynamic(this, &UBarkFeedWidget::HandleBarkRequested);

		World->GetTimerManager().ClearTimer(DisplayTimerHandle);
		World->GetTimerManager().ClearTimer(ClearTimerHandle);
	}

	Super::NativeDestruct();
}

void UBarkFeedWidget::HandleBarkRequested(FText SpeakerName, FText Line)
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Newest bark owns the single slot — cancel any in-flight expiry/fade so the replacement
	// isn't clipped by the previous bark's timers.
	World->GetTimerManager().ClearTimer(DisplayTimerHandle);
	World->GetTimerManager().ClearTimer(ClearTimerHandle);

	if (BarkLineText)
	{
		BarkLineText->SetText(Line);
		BarkLineText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (BarkSpeakerText)
	{
		BarkSpeakerText->SetText(SpeakerName);
		BarkSpeakerText->SetVisibility(SpeakerName.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	OnBarkReceived(SpeakerName, Line);

	const float Duration = FMath::Clamp(
		BaseSeconds + Line.ToString().Len() * PerCharacterSeconds, MinDisplaySeconds, MaxDisplaySeconds);
	World->GetTimerManager().SetTimer(DisplayTimerHandle, this, &UBarkFeedWidget::HandleBarkExpired, Duration, false);
}

void UBarkFeedWidget::HandleBarkExpired()
{
	OnBarkExpiring(FadeOutSeconds);

	UWorld* World = GetWorld();
	if (!World || FadeOutSeconds <= 0.f)
	{
		ClearBarkLine();
		return;
	}
	World->GetTimerManager().SetTimer(ClearTimerHandle, this, &UBarkFeedWidget::ClearBarkLine, FadeOutSeconds, false);
}

void UBarkFeedWidget::ClearBarkLine()
{
	if (BarkLineText)
	{
		BarkLineText->SetText(FText::GetEmpty());
		BarkLineText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (BarkSpeakerText)
	{
		BarkSpeakerText->SetText(FText::GetEmpty());
		BarkSpeakerText->SetVisibility(ESlateVisibility::Collapsed);
	}
	OnBarkCleared();
}
