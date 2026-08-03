// Focus-outline helper for world pickups.
// The kit pickup BP only ever flagged its single display mesh for custom depth, so any extra
// mesh on the item (the rifle's Handguard, kit attachment slot meshes) fell outside the outline
// and the silhouette broke up. This walks the whole component tree instead.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PickupVisualLibrary.generated.h"

UCLASS()
class EXTRACTION_API UPickupVisualLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Toggles the custom-depth focus outline on every mesh of PickupActor and its attached
	 *  actors, so the outline traces the whole item rather than one component.
	 *  UWidgetComponent is skipped -- it derives from UMeshComponent, and outlining the
	 *  interaction prompt quad draws a rectangle across the item. */
	UFUNCTION(BlueprintCallable, Category = "Pickup|Visual")
	static void SetPickupOutline(AActor* PickupActor, bool bEnabled);
};
