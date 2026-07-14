// ULootNotificationWidget implementation.

#include "UI/LootNotificationWidget.h"
#include "Game/MissionInventorySubsystem.h"
#include "Components/TextBlock.h"
#include "TimerManager.h"
#include "Engine/World.h"

void ULootNotificationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::Collapsed);

	UWorld* World = GetWorld();
	UMissionInventorySubsystem* Subsystem = World ? World->GetSubsystem<UMissionInventorySubsystem>() : nullptr;
	if (!IsValid(Subsystem)) return;

	CachedSubsystem = Subsystem;
	if (!Subsystem->OnLootNotify.IsAlreadyBound(this, &ULootNotificationWidget::HandleLootNotify))
		Subsystem->OnLootNotify.AddDynamic(this, &ULootNotificationWidget::HandleLootNotify);
}

void ULootNotificationWidget::NativeDestruct()
{
	if (UMissionInventorySubsystem* Subsystem = CachedSubsystem.Get())
		Subsystem->OnLootNotify.RemoveDynamic(this, &ULootNotificationWidget::HandleLootNotify);

	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(HideTimerHandle);

	Super::NativeDestruct();
}

void ULootNotificationWidget::HandleLootNotify(const FText& Message)
{
	if (MessageText)
		MessageText->SetText(Message);

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	OnMessageShown();

	// Latest message wins — restart the hide timer.
	if (UWorld* World = GetWorld())
		World->GetTimerManager().SetTimer(HideTimerHandle, this,
			&ULootNotificationWidget::HideNotification, DisplayDuration, false);
}

void ULootNotificationWidget::HideNotification()
{
	SetVisibility(ESlateVisibility::Collapsed);
}
