// UPingMarkerWidget -- small world-anchored marker that tracks the pinged target on screen.
// Projects target world location to screen space each tick and positions the marker via
// render translation. Hides when no target is active or target is off-screen / invalid.
//
// WBP must contain:
//   UImage "MarkerIcon" -- the marker image positioned by this widget

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Companion/CompanionCommandTypes.h"
#include "PingMarkerWidget.generated.h"

class UCompanionCommandComponent;
class UImage;

UCLASS(Abstract, Blueprintable)
class EXTRACTION_API UPingMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// --- Bound widget (designer wires in WBP) ---

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> MarkerIcon;

	// --- Designer-tunable offset ---

	/** Vertical offset (screen pixels) above the target's origin — pushes the marker up so it
	 *  floats above the enemy's head rather than sitting at their feet. */
	UPROPERTY(EditAnywhere, Category = "Ping|Marker")
	float VerticalScreenOffset = -40.f;

private:
	UFUNCTION()
	void HandlePingChanged(ECompanionCommand PendingCommand, AActor* PingedTarget);

	TWeakObjectPtr<UCompanionCommandComponent> CachedCommandComp;
	TWeakObjectPtr<AActor> TrackedTarget;

	/** World location to track when there is no target actor (TakeCover). */
	FVector TrackedLocation = FVector::ZeroVector;
	bool bTrackingLocation = false;
};
