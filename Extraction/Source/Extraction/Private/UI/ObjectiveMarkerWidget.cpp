// UObjectiveMarkerWidget implementation.

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
	LastDisplayedDistanceMetres = INDEX_NONE;

	if (LabelText)
	{
		LabelText->SetText(Objective.Label);
		LabelText->SetVisibility(Objective.Label.IsEmpty()
			? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
}

void UObjectiveMarkerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
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
	const bool bValidProjection = bProjected && !bBehind;
	const FVector2D Center = ViewportSize * 0.5f;

	if (bValidProjection)
	{
		const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
		if (ViewportScale <= KINDA_SMALL_NUMBER) return;
		ScreenPosition /= ViewportScale;
		ScreenPosition.Y += VerticalScreenOffset;
		ScreenPosition = ClampToViewport(ScreenPosition, ViewportSize, EdgePadding);

		// A projection that follows an edge fallback starts a new smoothing track. This prevents
		// the marker flying through the viewport interior as an objective crosses the camera plane.
		if (!bHasSmoothedScreenPosition || !bLastPositionWasValidProjection)
			SmoothedScreenPosition = ScreenPosition;
		else
			SmoothedScreenPosition = InterpolateScreenPosition(
				SmoothedScreenPosition, ScreenPosition, InDeltaTime, InterpolationSpeed);
		bHasSmoothedScreenPosition = true;
		bLastPositionWasValidProjection = true;
	}
	else
	{
		FVector2D Bearing(LocalDirection.Y, -LocalDirection.Z);
		if (!Bearing.Normalize())
			Bearing = FVector2D(0.f, 1.f);
		ScreenPosition = Center + Bearing * (Center.X + Center.Y);
		SmoothedScreenPosition = ClampToViewport(ScreenPosition, ViewportSize, EdgePadding);
		bHasSmoothedScreenPosition = true;
		bLastPositionWasValidProjection = false;
	}

	SmoothedScreenPosition = ClampToViewport(SmoothedScreenPosition, ViewportSize, EdgePadding);
	SetRenderTranslation(SmoothedScreenPosition);
	UpdateDistanceText(WorldLocation);
}

void UObjectiveMarkerWidget::UpdateDistanceText(const FVector& WorldLocation)
{
	if (!DistanceText) return;
	const APawn* Pawn = GetOwningPlayerPawn();
	if (!IsValid(Pawn)) return;

	const int32 RoundedMetres = FMath::RoundToInt(
		FVector::Dist(Pawn->GetActorLocation(), WorldLocation) / 100.f);
	if (RoundedMetres == LastDisplayedDistanceMetres) return;

	LastDisplayedDistanceMetres = RoundedMetres;
	DistanceText->SetText(FText::FromString(FString::Printf(TEXT("%d m"), RoundedMetres)));
}