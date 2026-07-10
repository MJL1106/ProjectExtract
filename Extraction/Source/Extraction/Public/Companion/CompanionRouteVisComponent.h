// Editor-only marker component for ACompanionRoute. Carries no data of its own — attaching it lets
// FCompanionRouteVisualizer (ExtractionEditor module) register per-component-class and draw/edit
// the owning route's waypoints directly in the level viewport.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CompanionRouteVisComponent.generated.h"

UCLASS()
class EXTRACTION_API UCompanionRouteVisComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCompanionRouteVisComponent();
};
