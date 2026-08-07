// ULootMarkerWidget -- C++ base for the world-space loot marker widget rendered by
// ULootMarkerComponent's WidgetComponent. No projection or occlusion logic -- the parent
// component drives visibility/scale and pushes kind/label/proximity updates.
//
// The WBP owns its own layout; there are no BindWidget requirements.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/LootMarkerComponent.h"
#include "LootMarkerWidget.generated.h"

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API ULootMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Which silhouette/icon the marker should present. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Marker")
	ELootMarkerKind MarkerKind = ELootMarkerKind::Container;

	/** Resolved label text (override or interface prompt). Empty when neither is available. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Marker")
	FText MarkerLabel;

	/** Distance from the local player, in metres. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Marker")
	float DistanceMeters = 0.f;

	/** True while inside the component's NearDistance -- the WBP swaps to its expanded
	 *  key-prompt state on this flag. */
	UPROPERTY(BlueprintReadOnly, Category = "Loot|Marker")
	bool bIsNear = false;

	/** Sets the marker's kind and label. Fires OnMarkerInfoUpdated only when the label actually
	 *  changes (kind is set unconditionally -- it never changes after construction in practice). */
	UFUNCTION(BlueprintCallable, Category = "Loot|Marker")
	void SetMarkerInfo(ELootMarkerKind InKind, const FText& InLabel);

	/** Updates distance every call; fires OnProximityChanged only on the bIsNear edge. */
	UFUNCTION(BlueprintCallable, Category = "Loot|Marker")
	void SetProximity(float InDistanceMeters, bool bInIsNear);

	/** Fired whenever MarkerKind or MarkerLabel changes. Hook for icon/text rebinds. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot|Marker")
	void OnMarkerInfoUpdated();

	/** Fired on the near/far edge. Hook for the caret-shrink + key-prompt swap. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot|Marker")
	void OnProximityChanged(bool bNear);
};
