// ARefreshStationActor — placeable test-range station: one interact fully restores the player's
// health + shield, refills every carried weapon's ammo to spawn defaults, and tops up stims.
// Reusable (not single-use) with a short per-use cooldown. Mesh is designer-assigned in a BP
// child (this project keeps C++ asset-agnostic); the mesh is what catches the interact trace.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/WorldInteractable.h"
#include "RefreshStationActor.generated.h"

class AExtractionPlayer;
class UStaticMeshComponent;

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking))
class EXTRACTION_API ARefreshStationActor : public AActor, public IWorldInteractable
{
	GENERATED_BODY()

public:
	ARefreshStationActor();

	// --- IWorldInteractable ---
	virtual bool CanWorldInteract_Implementation(AActor* Interactor) const override;
	virtual void WorldInteract_Implementation(AActor* Interactor) override;
	virtual FText GetWorldInteractionPrompt_Implementation(AActor* Interactor) const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Assign the mesh in the BP child. Blocks Visibility by default so the interact trace lands. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> StationMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh Station")
	FText InteractionPrompt = NSLOCTEXT("RefreshStation", "PromptDefault", "Refresh loadout");

	/** Seconds after a use before the station answers again — stops a held key re-triggering. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh Station", meta = (ClampMin = "0.0"))
	float ReuseCooldown = 1.f;

	/** BP hook for extras C++ cannot reach — the kit chain's grenade count lives in Blueprint. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Refresh Station")
	void OnRefreshed(AExtractionPlayer* Player);

private:
	float LastUseWorldTime = -1e9f;
};
