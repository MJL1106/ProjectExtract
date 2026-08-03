// ULootNotificationWidget implementation.

#include "UI/LootNotificationWidget.h"
#include "Game/MissionInventorySubsystem.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"

void ULootNotificationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::Collapsed);

	// Capture WBP-authored Info styling once -- NativeConstruct re-runs on the same instance
	// (EnsureOnPlayerScreen re-adds rather than rebuilds), and re-capturing on a later pass
	// could grab a leftover Warning/Alert tint instead of the true authored style.
	if (!bCapturedAuthoredStyle)
	{
		bCapturedAuthoredStyle = true;
		if (MessageText)
			AuthoredTextColor = MessageText->GetColorAndOpacity();
		if (Backing)
			AuthoredBackingColor = Backing->GetBrushColor();
	}

	UWorld* World = GetWorld();
	UMissionInventorySubsystem* Subsystem = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!IsValid(Subsystem)) return;

	CachedSubsystem = Subsystem;

	if (!Subsystem->OnLootNotify.IsAlreadyBound(this, &ULootNotificationWidget::HandleLootNotify))
		Subsystem->OnLootNotify.AddDynamic(this, &ULootNotificationWidget::HandleLootNotify);

	if (!Subsystem->OnToastNotify.IsAlreadyBound(this, &ULootNotificationWidget::HandleToastNotify))
		Subsystem->OnToastNotify.AddDynamic(this, &ULootNotificationWidget::HandleToastNotify);
}

void ULootNotificationWidget::NativeDestruct()
{
	if (UMissionInventorySubsystem* Subsystem = CachedSubsystem.Get())
	{
		Subsystem->OnLootNotify.RemoveDynamic(this, &ULootNotificationWidget::HandleLootNotify);
		Subsystem->OnToastNotify.RemoveDynamic(this, &ULootNotificationWidget::HandleToastNotify);
	}

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(HideTimerHandle);

	Super::NativeDestruct();
}

void ULootNotificationWidget::HandleLootNotify(const FText& Message)
{
	ShowToast(Message, EToastSeverity::Info);
}

void ULootNotificationWidget::HandleToastNotify(const FText& Message, EToastSeverity Severity)
{
	ShowToast(Message, Severity);
}

void ULootNotificationWidget::ShowToast(const FText& Message, EToastSeverity Severity)
{
	// Alert holds priority: a lower-severity message landing while a higher one is still
	// showing (e.g. a loot pickup mid-firefight, right after Escalated trips the alarm) is
	// dropped rather than truncating/repainting the toast that explains why the fight started.
	if (GetVisibility() != ESlateVisibility::Collapsed && Severity < ActiveSeverity)
		return;

	ActiveSeverity = Severity;

	if (MessageText)
	{
		MessageText->SetText(Message);
		MessageText->SetColorAndOpacity(GetSeverityTextColor(Severity));
	}

	if (Backing)
		Backing->SetBrushColor(GetSeverityBackingColor(Severity));

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	OnMessageShown(Severity);

	USoundBase* Sound = GetSeveritySound(Severity);
	if (IsValid(Sound))
		UGameplayStatics::PlaySound2D(this, Sound);

	// Latest (accepted) message wins — restart the hide timer.
	if (UWorld* World = GetWorld())
		World->GetTimerManager().SetTimer(HideTimerHandle, this,
			&ULootNotificationWidget::HideNotification, GetDisplayDuration(Severity), false);
}

void ULootNotificationWidget::HideNotification()
{
	SetVisibility(ESlateVisibility::Collapsed);
	ActiveSeverity = EToastSeverity::Info;
}

float ULootNotificationWidget::GetDisplayDuration(EToastSeverity Severity) const
{
	switch (Severity)
	{
	case EToastSeverity::Warning: return WarningDisplayDuration;
	case EToastSeverity::Alert:   return AlertDisplayDuration;
	default:                      return DisplayDuration;
	}
}

FSlateColor ULootNotificationWidget::GetSeverityTextColor(EToastSeverity Severity) const
{
	switch (Severity)
	{
	case EToastSeverity::Warning: return FSlateColor(WarningColor);
	case EToastSeverity::Alert:   return FSlateColor(AlertColor);
	default:                      return AuthoredTextColor; // Restore, never hardcode.
	}
}

FLinearColor ULootNotificationWidget::GetSeverityBackingColor(EToastSeverity Severity) const
{
	if (Severity == EToastSeverity::Info)
		return AuthoredBackingColor; // Restore, never hardcode.

	// Darkened/alpha'd variant behind the text -- full-saturation severity colour behind
	// white/tinted text would fight the message rather than support it.
	FLinearColor BackingColor = (Severity == EToastSeverity::Warning ? WarningColor : AlertColor) * 0.35f;
	BackingColor.A = 0.85f;
	return BackingColor;
}

USoundBase* ULootNotificationWidget::GetSeveritySound(EToastSeverity Severity) const
{
	switch (Severity)
	{
	case EToastSeverity::Warning: return WarningSound;
	case EToastSeverity::Alert:   return AlertSound;
	default:                      return nullptr; // Info plays nothing.
	}
}
