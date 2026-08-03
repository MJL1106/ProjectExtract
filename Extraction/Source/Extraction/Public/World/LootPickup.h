// ALootPickup — physical lootable pickup spawned at an enemy's death location.
// Grants contents via UMissionInventorySubsystem (same path as ALootContainer).
// Mesh and any effects assigned in the Blueprint subclass — C++ stays asset-agnostic.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Lootable.h"
#include "World/LootTypes.h"
#include "LootPickup.generated.h"

class ULootMarkerComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking))
class EXTRACTION_API ALootPickup : public AActor, public ILootable
{
	GENERATED_BODY()

public:
	ALootPickup();

	// --- ILootable ---
	virtual void Loot_Implementation(AActor* Looter) override;
	virtual bool CanLoot_Implementation() const override;

	void SetContents(const TArray<FLootGrant>& InContents);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UStaticMeshComponent> PickupMesh;

	/** Overhead loot marker -- visibility driven by CanLoot(). Designer assigns the widget class on BP. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<ULootMarkerComponent> LootMarker;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FLootGrant> Contents;

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	bool bLooted = false;

	UFUNCTION(BlueprintImplementableEvent, Category = "Loot")
	void OnLooted(AActor* Looter);

private:
	void GrantAllContents();
};
