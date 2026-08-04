// AObjectiveMarkerDisplay -- world-space billboard actor spawned by UObjectiveSubsystem for
// each active objective. Replaces the screen-space projection approach with a fixed world
// position that billboards toward the camera and distance-scales for consistent on-screen size.
//
// Spawned/destroyed by the subsystem on objective add/remove. The display actor class is
// provided to the subsystem via SetMarkerDisplayClass (called by the player controller during
// its local setup -- the class is a UPROPERTY on the controller's BP subclass).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/ObjectiveSubsystem.h"
#include "ObjectiveMarkerDisplay.generated.h"

class UWidgetComponent;
class UObjectiveMarker3DWidget;
class APlayerController;

UCLASS(Blueprintable)
class EXTRACTION_API AObjectiveMarkerDisplay : public AActor
{
	GENERATED_BODY()

public:
	AObjectiveMarkerDisplay();

	/** Initialise this display with an objective copy. Must be called immediately after spawn. */
	void InitObjective(const FObjectiveMarker& InObjective);

	/** The objective id this display tracks. */
	FName GetObjectiveId() const { return Objective.Id; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Designer-assigned widget class (set on the BP subclass CDO) ---

	/** Widget class to render on the WidgetComponent. Assign WBP_ObjectiveMarker3D here. */
	UPROPERTY(EditDefaultsOnly, Category = "Objective|Display")
	TSubclassOf<UUserWidget> WidgetClass;

	// --- Tuning ---

	/** Distance at which the marker renders at scale 1.0 (cm). */
	UPROPERTY(EditAnywhere, Category = "Objective|Display", meta = (ClampMin = "100.0"))
	float ReferenceDistance = 700.f;

	/** Exponent applied to the distance/reference ratio before clamping. 1.0 (default) holds
	 *  constant apparent on-screen size, matching legacy behaviour exactly. Values below 1.0 let
	 *  the marker shrink with distance instead of staying a fixed size all the way out. */
	UPROPERTY(EditAnywhere, Category = "Objective|Display", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float DistanceFalloffExponent = 1.f;

	/** Minimum scale factor (prevents marker from becoming invisible at close range). */
	UPROPERTY(EditAnywhere, Category = "Objective|Display", meta = (ClampMin = "0.01"))
	float MinScale = 0.4f;

	/** Maximum scale factor (prevents marker from becoming excessively large at distance). */
	UPROPERTY(EditAnywhere, Category = "Objective|Display", meta = (ClampMin = "0.1"))
	float MaxScale = 3.0f;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Objective|Display", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> MarkerWidgetComponent;

	FObjectiveMarker Objective;
	int32 LastDisplayedDistanceMetres = INDEX_NONE;

	TWeakObjectPtr<UObjectiveMarker3DWidget> CachedWidget;

	/** Both take the already-resolved PlayerController -- Tick resolves it once per frame rather
	 *  than each helper looking it up separately. */
	void UpdateBillboardAndScale(APlayerController* PC);
	void UpdateDistance(APlayerController* PC);
};
