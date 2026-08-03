// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsumableWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ConsumableInventoryComponent.h"
#include "Character/ExtractionPlayer.h"

namespace
{
	/** Grenade count polling interval in seconds. A few times per second is ample
	 *  for a count that only changes on a throw. */
	constexpr float GrenadePollingRate = 0.25f;

	/** Maximum bind-retry attempts before the widget gives up waiting for an owning player.
	 *  At 0.5s per attempt this is a 10-second window. */
	constexpr int32 MaxBindRetryAttempts = 20;
}

void UConsumableWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(StimKeyText))
		StimKeyText->SetText(StimKeyLabel);
	if (IsValid(GrenadeKeyText))
		GrenadeKeyText->SetText(GrenadeKeyLabel);

	UpdateStimDisplay(0);
	UpdateGrenadeDisplay(0);

	InitializePlayerBinding();

	// Start grenade polling timer
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			GrenadePollingTimerHandle,
			FTimerDelegate::CreateUObject(this, &UConsumableWidget::PollGrenadeCount),
			GrenadePollingRate,
			true);
	}
}

void UConsumableWidget::NativeDestruct()
{
	// Unbind stim delegate
	if (CachedConsumableComponent.IsValid())
		CachedConsumableComponent->OnStimCountChanged.RemoveDynamic(this, &UConsumableWidget::OnStimCountChanged);

	// Unbind pawn-change listener
	if (APlayerController* PC = GetOwningPlayer())
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UConsumableWidget::OnPawnChanged);

	// Clear all timers (project hard rule: every FTimerHandle cleared on teardown)
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GrenadePollingTimerHandle);
		World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
	}

	Super::NativeDestruct();
}

// ---- Binding ----

bool UConsumableWidget::BindConsumableComponent()
{
	const APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return false;

	const AExtractionPlayer* Player = Cast<AExtractionPlayer>(PC->GetPawn());
	if (!IsValid(Player)) return false;

	UConsumableInventoryComponent* Consumables = Player->GetConsumableInventoryComponent();
	if (!IsValid(Consumables)) return false;

	// Unbind from previous component before caching the new one (respawn/revive path)
	if (CachedConsumableComponent.IsValid())
		CachedConsumableComponent->OnStimCountChanged.RemoveDynamic(this, &UConsumableWidget::OnStimCountChanged);

	CachedConsumableComponent = Consumables;

	Consumables->OnStimCountChanged.AddUniqueDynamic(this, &UConsumableWidget::OnStimCountChanged);

	// The initial stim grant fires from BeginPlay before any widget exists, so the broadcast
	// went into nothing. Pull the current value now.
	Consumables->BroadcastStimCountNow();

	return true;
}

void UConsumableWidget::InitializePlayerBinding()
{
	// Register the persistent pawn-change listener immediately if the PC exists,
	// otherwise start a capped retry timer.
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UConsumableWidget::OnPawnChanged);
		BindConsumableComponent();
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("ConsumableWidget: No owning player, starting bind retry timer"));
	BindRetryAttemptsRemaining = MaxBindRetryAttempts;

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			BindRetryTimerHandle,
			FTimerDelegate::CreateUObject(this, &UConsumableWidget::RetryBindConsumableComponent),
			0.5f,
			true);
	}
}

void UConsumableWidget::OnPawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	BindConsumableComponent();
}

void UConsumableWidget::RetryBindConsumableComponent()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UConsumableWidget::OnPawnChanged);

		if (BindConsumableComponent())
		{
			if (const UWorld* World = GetWorld())
				World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
			return;
		}
	}

	if (--BindRetryAttemptsRemaining <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ConsumableWidget: Bind retry exhausted after %d attempts, giving up"),
			MaxBindRetryAttempts);
		if (const UWorld* World = GetWorld())
			World->GetTimerManager().ClearTimer(BindRetryTimerHandle);
	}
}

// ---- Delegate / Polling Callbacks ----

void UConsumableWidget::OnStimCountChanged(int32 NewStimCount)
{
	UpdateStimDisplay(NewStimCount);
}

void UConsumableWidget::PollGrenadeCount()
{
	const int32 Count = GetCurrentGrenadeCount();
	if (Count == LastKnownGrenadeCount) return;

	LastKnownGrenadeCount = Count;
	UpdateGrenadeDisplay(Count);
}

int32 UConsumableWidget::GetCurrentGrenadeCount_Implementation() const
{
	// Base implementation returns -1 (unknown). Blueprint subclass overrides
	// this to read the kit chain's grenade actor property.
	return -1;
}

// ---- UI Helpers ----

void UConsumableWidget::UpdateStimDisplay(int32 NewCount)
{
	if (IsValid(StimCountText))
		StimCountText->SetText(FText::AsNumber(NewCount));

	OnStimCountUpdated(NewCount);
}

void UConsumableWidget::UpdateGrenadeDisplay(int32 NewCount)
{
	const int32 DisplayCount = FMath::Max(NewCount, 0);

	if (IsValid(GrenadeCountText))
		GrenadeCountText->SetText(FText::AsNumber(DisplayCount));

	OnGrenadeCountUpdated(DisplayCount);
}
