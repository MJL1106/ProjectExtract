// World marker floating above lootable actors — a small ring/dot affordance hidden while
// a wall occludes line-of-sight to the camera. Subclasses UOverheadWidgetComponent to
// inherit occlusion tracing and distance scaling; adds availability polling via ILootable,
// IWorldInteractable, or a manual Blueprint toggle.

#pragma once

#include "CoreMinimal.h"
#include "UI/OverheadWidgetComponent.h"
#include "LootMarkerComponent.generated.h"

/** Optional native availability query an owner can bind. Return-value delegate, so it is
 *  single-bind by design; unbound is the default and costs nothing. */
DECLARE_DELEGATE_RetVal(bool, FLootMarkerAvailabilityQuery);

/**
 * Drop-in component for lootable actors. Drives visibility from interface availability
 * checks and a max-distance threshold; occlusion and scaling are inherited from the parent.
 * Assign the widget class via the inherited Widget Class property (User Interface category).
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API ULootMarkerComponent : public UOverheadWidgetComponent
{
	GENERATED_BODY()

public:
	ULootMarkerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Manually toggle availability for actors that implement neither ILootable nor IWorldInteractable. */
	UFUNCTION(BlueprintCallable, Category = "Loot Marker")
	void SetMarkerAvailable(bool bAvailable);

	/** Force an immediate availability refresh -- call after loot state changes. */
	UFUNCTION(BlueprintCallable, Category = "Loot Marker")
	void RefreshMarkerNow();

	/** Binds an owner-supplied availability query, evaluated on the EXISTING availability poll and
	 *  AND'd into whatever the interface/manual branches resolve. For owners whose "looted" state
	 *  fires no event at all (a pickup that only hides itself), where polling is the only signal.
	 *  Bind weak-safe -- CreateUObject or CreateWeakLambda, never a raw capturing lambda. */
	void SetAvailabilityQuery(const FLootMarkerAvailabilityQuery& InQuery);

	/** Drops any bound availability query, returning resolution to interface + manual state. */
	void ClearAvailabilityQuery();

	/** World-space Z offset (cm) from the attach parent's origin to the marker. Divided by the
	 *  parent's world scale at BeginPlay so a scaled volume does not inflate the clearance. */
	UPROPERTY(EditAnywhere, Category = "Loot Marker", meta = (ClampMin = "0.0"))
	float MarkerWorldZOffset = 40.f;

protected:
	/** Beyond this distance from the local player the marker hides entirely. */
	UPROPERTY(EditAnywhere, Category = "Loot Marker", meta = (ClampMin = "100.0"))
	float MaxVisibleDistance = 1500.f;

	/** Seconds between availability refresh polls. */
	UPROPERTY(EditAnywhere, Category = "Loot Marker", meta = (ClampMin = "0.05"))
	float AvailabilityPollInterval = 0.25f;

private:
	/** Fallback availability flag when no interface is found on the owner. */
	bool bMarkerAvailable = true;

	/** Unbound by default -- see SetAvailabilityQuery. */
	FLootMarkerAvailabilityQuery AvailabilityQuery;

	FTimerHandle AvailabilityTimerHandle;

	void RefreshAvailability();

	/** Resolves whether the marker should be visible based on interface state and distance. */
	bool ResolveAvailability(AActor* Owner) const;
};
