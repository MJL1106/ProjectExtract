// UPingMarkerWidget implementation.

#include "UI/PingMarkerWidget.h"
#include "Components/CompanionCommandComponent.h"
#include "Components/Image.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/PlayerController.h"

void UPingMarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Start hidden until a ping fires.
	SetVisibility(ESlateVisibility::Collapsed);

	APawn* Pawn = GetOwningPlayerPawn();
	if (!IsValid(Pawn)) return;

	UCompanionCommandComponent* Comp = Pawn->FindComponentByClass<UCompanionCommandComponent>();
	if (!IsValid(Comp)) return;

	CachedCommandComp = Comp;
	if (!Comp->OnPingChanged.IsAlreadyBound(this, &UPingMarkerWidget::HandlePingChanged))
	{
		Comp->OnPingChanged.AddDynamic(this, &UPingMarkerWidget::HandlePingChanged);
	}

	// Sync to current state.
	HandlePingChanged(Comp->GetPendingCommand(), Comp->GetPingedTarget());
}

void UPingMarkerWidget::NativeDestruct()
{
	if (CachedCommandComp.IsValid())
	{
		CachedCommandComp->OnPingChanged.RemoveDynamic(this, &UPingMarkerWidget::HandlePingChanged);
	}

	Super::NativeDestruct();
}

void UPingMarkerWidget::HandlePingChanged(ECompanionCommand PendingCommand, AActor* PingedTarget)
{
	// TakeCover has no target actor -- track the resolved cover location instead.
	if (PendingCommand == ECompanionCommand::TakeCover && CachedCommandComp.IsValid())
	{
		TrackedTarget.Reset();
		TrackedLocation = CachedCommandComp->GetPendingCoverLocation();
		bTrackingLocation = true;
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		return;
	}

	bTrackingLocation = false;
	if (PendingCommand == ECompanionCommand::None || !IsValid(PingedTarget))
	{
		TrackedTarget.Reset();
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	TrackedTarget = PingedTarget;
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UPingMarkerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Skip projection work when no target is active.
	if (!TrackedTarget.IsValid() && !bTrackingLocation)
	{
		if (GetVisibility() != ESlateVisibility::Collapsed)
		{
			SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	const FVector WorldLoc = bTrackingLocation ? TrackedLocation : TrackedTarget->GetActorLocation();
	FVector2D ScreenPos;
	const bool bOnScreen = PC->ProjectWorldLocationToScreen(WorldLoc, ScreenPos, true);

	if (!bOnScreen)
	{
		if (GetVisibility() != ESlateVisibility::Collapsed)
		{
			SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	if (GetVisibility() != ESlateVisibility::SelfHitTestInvisible)
	{
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	// Convert from absolute screen coords to DPI-scaled widget coords.
	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
	if (ViewportScale > KINDA_SMALL_NUMBER)
	{
		ScreenPos /= ViewportScale;
	}

	ScreenPos.Y += VerticalScreenOffset;
	SetRenderTranslation(ScreenPos);
}
