// Screen-space overhead widget component with wall occlusion and distance scaling.

#include "UI/OverheadWidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

UOverheadWidgetComponent::UOverheadWidgetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Force the first visible tick to trace immediately — otherwise a widget shown
	// behind a wall renders unoccluded for up to one trace interval.
	TimeSinceOcclusionTrace = OcclusionTraceInterval;
}

void UOverheadWidgetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UUserWidget* UserWidget = GetUserWidgetObject();
	if (!IsValid(UserWidget) || !IsVisible()) return;

	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!IsValid(Camera)) return;

	const FVector CameraLocation = Camera->GetCameraLocation();
	const FVector WidgetLocation = GetComponentLocation();

	TimeSinceOcclusionTrace += DeltaTime;
	if (bHideWhenOccluded && TimeSinceOcclusionTrace >= OcclusionTraceInterval)
	{
		TimeSinceOcclusionTrace = 0.f;

		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(OverheadWidgetOcclusion), false);
		TraceParams.AddIgnoredActor(GetOwner());
		if (const APlayerController* PC = Camera->GetOwningPlayerController())
			TraceParams.AddIgnoredActor(PC->GetPawn());

		// World geometry only (walls, doors) — pawns must not occlude, or an enemy in
		// front flickers out the meters of enemies behind him during firefights.
		FCollisionObjectQueryParams ObjectParams;
		ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

		bOccluded = GetWorld()->LineTraceTestByObjectType(CameraLocation, WidgetLocation, ObjectParams, TraceParams);
	}

	float Scale = 0.f;
	if (!bOccluded || !bHideWhenOccluded)
	{
		const float Distance = FMath::Max(FVector::Dist(CameraLocation, WidgetLocation), 1.f);
		Scale = FMath::Clamp(ReferenceDistance / Distance, MinScale, MaxScale);
	}

	const FVector2D NewScale(Scale, Scale);
	if (!NewScale.Equals(LastAppliedScale, KINDA_SMALL_NUMBER))
	{
		UserWidget->SetRenderScale(NewScale);
		LastAppliedScale = NewScale;
	}
}
