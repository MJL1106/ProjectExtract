// UObjectiveMarkerWidget implementation.

#include "UI/ObjectiveMarkerWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

void UObjectiveMarkerWidget::SetObjective(const FObjectiveMarker& InObjective)
{
	Objective = InObjective;

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

	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
	if (ViewportScale <= KINDA_SMALL_NUMBER) return;
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this) / ViewportScale;

	// Camera-space direction — drives the fallback bearing when projection fails (behind camera).
	FVector CamLoc; FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector LocalDir = CamRot.UnrotateVector(WorldLocation - CamLoc); // X fwd, Y right, Z up
	const bool bBehind = LocalDir.X < 0.f;

	FVector2D ScreenPos;
	const bool bProjected = PC->ProjectWorldLocationToScreen(WorldLocation, ScreenPos, true);
	ScreenPos /= ViewportScale;

	const FVector2D Center = ViewportSize * 0.5f;
	if (bBehind || !bProjected)
	{
		// Projection is unusable — synthesise a screen offset from the camera-space bearing and
		// push it far outside the viewport; the edge clamp below pulls it onto the border.
		FVector2D Bearing(LocalDir.Y, -LocalDir.Z);
		if (!Bearing.Normalize())
			Bearing = FVector2D(0.f, 1.f); // dead-astern — park the marker bottom-centre
		ScreenPos = Center + Bearing * (Center.X + Center.Y);
	}
	else
	{
		ScreenPos.Y += VerticalScreenOffset;
	}

	// Edge clamp — the marker never leaves the padded viewport rect.
	ScreenPos.X = FMath::Clamp(ScreenPos.X, EdgePadding, ViewportSize.X - EdgePadding);
	ScreenPos.Y = FMath::Clamp(ScreenPos.Y, EdgePadding, ViewportSize.Y - EdgePadding);

	SetRenderTranslation(ScreenPos);
	UpdateDistanceText(WorldLocation);
}

void UObjectiveMarkerWidget::UpdateDistanceText(const FVector& WorldLocation)
{
	if (!DistanceText) return;

	const APawn* Pawn = GetOwningPlayerPawn();
	if (!IsValid(Pawn)) return;

	const float Metres = FVector::Dist(Pawn->GetActorLocation(), WorldLocation) / 100.f;
	DistanceText->SetText(FText::FromString(FString::Printf(TEXT("%.0f m"), Metres)));
}
