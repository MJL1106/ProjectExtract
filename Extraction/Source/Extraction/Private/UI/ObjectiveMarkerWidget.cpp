// UObjectiveMarkerWidget implementation -- off-screen edge indicator only.

#include "UI/ObjectiveMarkerWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

FVector2D UObjectiveMarkerWidget::InterpolateScreenPosition(const FVector2D& Current,
	const FVector2D& Target, float DeltaTime, float Speed)
{
	if (DeltaTime <= 0.f) return Current;
	if (Speed <= 0.f) return Target;
	const float Alpha = 1.f - FMath::Exp(-Speed * DeltaTime);
	return FMath::Lerp(Current, Target, Alpha);
}

FVector2D UObjectiveMarkerWidget::ClampToViewport(const FVector2D& Position,
	const FVector2D& ViewportSize, float Padding)
{
	const float SafePadding = FMath::Max(0.f, FMath::Min(Padding,
		FMath::Min(ViewportSize.X, ViewportSize.Y) * 0.5f));
	return FVector2D(
		FMath::Clamp(Position.X, SafePadding, ViewportSize.X - SafePadding),
		FMath::Clamp(Position.Y, SafePadding, ViewportSize.Y - SafePadding));
}

void UObjectiveMarkerWidget::SetObjective(const FObjectiveMarker& InObjective)
{
	Objective = InObjective;
	bHasSmoothedScreenPosition = false;
	bLastPositionWasValidProjection = false;
}

void UObjectiveMarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Hide distance and label -- world-space widget handles those now.
	if (DistanceText)
		DistanceText->SetVisibility(ESlateVisibility::Collapsed);
	if (LabelText)
		LabelText->SetVisibility(ESlateVisibility::Collapsed);
}

void UObjectiveMarkerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	const FVector WorldLocation = Objective.ResolveLocation();
	const FGeometry PlayerGeometry = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC);
	const FVector2D ViewportSize = PlayerGeometry.GetLocalSize();
	if (ViewportSize.X <= KINDA_SMALL_NUMBER || ViewportSize.Y <= KINDA_SMALL_NUMBER) return;

	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector LocalDirection = CameraRotation.UnrotateVector(WorldLocation - CameraLocation);
	const bool bBehind = LocalDirection.X < 0.f;

	FVector2D ScreenPosition = FVector2D::ZeroVector;
	const bool bProjected = PC->ProjectWorldLocationToScreen(WorldLocation, ScreenPosition, true);

	// Determine if the objective is truly on-screen (within the inset region).
	bool bOnScreen = false;
	if (bProjected && !bBehind)
	{
		const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
		if (ViewportScale > KINDA_SMALL_NUMBER)
		{
			const FVector2D ScaledPos = ScreenPosition / ViewportScale;
			const float InsetX = ViewportSize.X * OnScreenInsetFraction;
			const float InsetY = ViewportSize.Y * OnScreenInsetFraction;
			bOnScreen = ScaledPos.X >= InsetX && ScaledPos.X <= (ViewportSize.X - InsetX)
				&& ScaledPos.Y >= InsetY && ScaledPos.Y <= (ViewportSize.Y - InsetY);
		}
	}

	// Hide when the world-space billboard is visible (on-screen).
	if (bOnScreen)
	{
		SetRenderOpacity(0.f);
		bHasSmoothedScreenPosition = false;
		return;
	}

	SetRenderOpacity(1.f);

	// Off-screen / behind: edge-clamp the indicator.
	const FVector2D Center = ViewportSize * 0.5f;
	FVector2D Bearing(LocalDirection.Y, -LocalDirection.Z);
	if (!Bearing.Normalize())
		Bearing = FVector2D(0.f, 1.f);
	ScreenPosition = Center + Bearing * (Center.X + Center.Y);
	FVector2D ClampedPosition = ClampToViewport(ScreenPosition, ViewportSize, EdgePadding);

	if (!bHasSmoothedScreenPosition || bLastPositionWasValidProjection)
		SmoothedScreenPosition = ClampedPosition;
	else
		SmoothedScreenPosition = InterpolateScreenPosition(
			SmoothedScreenPosition, ClampedPosition, InDeltaTime, InterpolationSpeed);

	bHasSmoothedScreenPosition = true;
	bLastPositionWasValidProjection = false;

	SmoothedScreenPosition = ClampToViewport(SmoothedScreenPosition, ViewportSize, EdgePadding);
	SetRenderTranslation(SmoothedScreenPosition);
}
