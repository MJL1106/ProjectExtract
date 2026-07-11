// Screen-space overhead widget component with wall occlusion and distance scaling.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "OverheadWidgetComponent.generated.h"

class UCoverPoseComponent;

/**
 * Screen-space UWidgetComponent for overhead meters/icons (enemy awareness, companion health/mode).
 * Fixes two stock screen-space behaviours:
 *  - draws through walls -> line trace from the camera hides it while occluded
 *  - constant pixel size at any range -> render scale shrinks with distance
 * Both are applied via the user widget's RenderScale so they never fight widgets
 * that drive their own RenderOpacity (e.g. the awareness meter's fade).
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UOverheadWidgetComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UOverheadWidgetComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	/** Distance (cm) at which the widget renders at full scale; closer clamps to MaxScale, further shrinks. */
	UPROPERTY(EditAnywhere, Category = "Overhead", meta = (ClampMin = "1.0"))
	float ReferenceDistance = 400.f;

	/** Smallest scale the widget shrinks to at long range. */
	UPROPERTY(EditAnywhere, Category = "Overhead", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinScale = 0.5f;

	/** Largest scale when closer than ReferenceDistance. */
	UPROPERTY(EditAnywhere, Category = "Overhead", meta = (ClampMin = "0.1"))
	float MaxScale = 1.f;

	/** Hide the widget while a visibility-blocking wall sits between the camera and the widget. */
	UPROPERTY(EditAnywhere, Category = "Overhead")
	bool bHideWhenOccluded = true;

	/** Seconds between occlusion line traces (distance scale still updates every tick). */
	UPROPERTY(EditAnywhere, Category = "Overhead", meta = (ClampMin = "0.02"))
	float OcclusionTraceInterval = 0.1f;

	/** While the owner is in crouch-height cover, an occluder hit within this distance (cm) of the
	 *  widget doesn't hide it — it's the enemy's own cover chunk. Stand/lean cover keeps the full
	 *  hide (the head-height widget clears the chunk, so an occlusion there means a real wall), and
	 *  hits further away than this still hide too. */
	UPROPERTY(EditAnywhere, Category = "Overhead", meta = (ClampMin = "0.0"))
	float CoverOccluderMaxDistance = 300.f;

private:
	float TimeSinceOcclusionTrace = 0.f;
	bool bOccluded = false;
	FVector2D LastAppliedScale = FVector2D(1.f, 1.f);

	/** One-shot lazy lookup — owner components are fixed after spawn. */
	TWeakObjectPtr<UCoverPoseComponent> CachedCoverPose;
	bool bCoverPoseLookupDone = false;
};
