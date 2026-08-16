// Screen-space overhead widget component with wall occlusion and distance scaling.

#include "UI/OverheadWidgetComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "AI/Cover/CoverPoseComponent.h"

static TAutoConsoleVariable<int32> CVarOverheadOcclusion(
	TEXT("ui.OverheadOcclusion"),
	1,
	TEXT("If 0, overhead widgets skip the wall-occlusion hide entirely (diagnostic kill-switch)."),
	ECVF_Cheat);

void UOverheadWidgetComponent::ResetOcclusionState()
{
	TimeSinceOcclusionTrace = OcclusionTraceInterval;
	bOccluded = false;
	// Sentinel ensures the next computed scale is unconditionally applied.
	LastAppliedScale = FVector2D(-1.f, -1.f);
}

UOverheadWidgetComponent::UOverheadWidgetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Force the first visible tick to trace immediately — otherwise a widget shown
	// behind a wall renders unoccluded for up to one trace interval.
	TimeSinceOcclusionTrace = OcclusionTraceInterval;
}

void UOverheadWidgetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_UI_OverheadWidgetTick);

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UUserWidget* UserWidget = GetUserWidgetObject();
	if (!IsValid(UserWidget) || !IsVisible()) return;

	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!IsValid(Camera)) return;

	const FVector CameraLocation = Camera->GetCameraLocation();
	const FVector WidgetLocation = GetComponentLocation();

	// Ejected/cinematic view (demo cams set a non-pawn view target): a wall or roof between the
	// shot camera and the actor is composition, not hidden information — skip the occlusion hide
	// so meters stay visible from top-down and exterior angles. Distance scaling still applies,
	// floored at MinScale so wide shots keep them readable.
	bool bCinematicView = false;
	if (const APlayerController* PC = Camera->GetOwningPlayerController())
	{
		const AActor* ViewTarget = PC->GetViewTarget();
		bCinematicView = IsValid(ViewTarget) && ViewTarget != PC->GetPawn();
	}

	const bool bOcclusionEnabled = !bCinematicView && bHideWhenOccluded && CVarOverheadOcclusion.GetValueOnGameThread() != 0;

	TimeSinceOcclusionTrace += DeltaTime;
	if (bOcclusionEnabled && TimeSinceOcclusionTrace >= OcclusionTraceInterval)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_UI_OverheadWidgetOcclusionTrace);

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
				if (!bCoverPoseLookupDone)
				{
					bCoverPoseLookupDone = true;
					if (const AActor* Owner = GetOwner())
						CachedCoverPose = Owner->FindComponentByClass<UCoverPoseComponent>();
				}

				const UCoverPoseComponent* CoverPose = CachedCoverPose.Get();
				const bool bCoverExempt = IsValid(CoverPose) && CoverPose->bInCover
					&& CoverPose->CoverHeight == ECoverHeight::Crouch
					&& FVector::Dist(OcclusionHit.ImpactPoint, WidgetLocation) <= CoverOccluderMaxDistance;

				// Near cover chunk sits between camera and widget — don't skip past it to find a
				// further real wall, the "show dimmed in cover" behaviour wants this hit ignored entirely.
				bOccluded = !bCoverExempt;
				break;
			}
			TraceParams.AddIgnoredComponent(HitComp);
		}
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
