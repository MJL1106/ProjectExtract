// Screen-space overhead widget component with wall occlusion and distance scaling.

#include "UI/OverheadWidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

static TAutoConsoleVariable<int32> CVarOverheadOcclusion(
	TEXT("ui.OverheadOcclusion"),
	1,
	TEXT("If 0, overhead widgets skip the wall-occlusion hide entirely (diagnostic kill-switch)."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarOverheadDebug(
	TEXT("ui.OverheadWidgetDebug"),
	0,
	TEXT("If non-zero, log each overhead widget's occlusion trace result and applied render scale."),
	ECVF_Cheat);

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

	const bool bOcclusionEnabled = bHideWhenOccluded && CVarOverheadOcclusion.GetValueOnGameThread() != 0;

	TimeSinceOcclusionTrace += DeltaTime;
	if (bOcclusionEnabled && TimeSinceOcclusionTrace >= OcclusionTraceInterval)
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

		// Only visible, sight-blocking geometry occludes. Invisible gameplay volumes
		// (takedown OverlapBoxes) and hidden helper meshes (cover-slot markers) share the
		// WorldDynamic/WorldStatic object types but must not hide the widget — skip past them.
		bOccluded = false;
		FHitResult OcclusionHit;
		constexpr int32 MaxSkips = 8;
		for (int32 Skip = 0; Skip < MaxSkips; ++Skip)
		{
			if (!GetWorld()->LineTraceSingleByObjectType(OcclusionHit, CameraLocation, WidgetLocation, ObjectParams, TraceParams))
				break;

			const UPrimitiveComponent* HitComp = OcclusionHit.GetComponent();
			const AActor* HitActor = OcclusionHit.GetActor();
			const bool bRealOccluder = IsValid(HitComp)
				&& HitComp->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block
				&& HitComp->IsVisible() && !HitComp->bHiddenInGame
				&& (!IsValid(HitActor) || !HitActor->IsHidden());
			if (bRealOccluder)
			{
				bOccluded = true;
				break;
			}
			TraceParams.AddIgnoredComponent(HitComp);
		}

		if (CVarOverheadDebug.GetValueOnGameThread() != 0)
			UE_LOG(LogTemp, Log, TEXT("[OverheadWidget] %s occluded=%d hitActor=%s hitComp=%s widgetLoc=%s camDist=%.0f"),
				*GetNameSafe(GetOwner()), (int32)bOccluded,
				*GetNameSafe(OcclusionHit.GetActor()), *GetNameSafe(OcclusionHit.GetComponent()),
				*WidgetLocation.ToCompactString(), FVector::Dist(CameraLocation, WidgetLocation));
	}

	float Scale = 0.f;
	if (!bOccluded || !bOcclusionEnabled)
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
