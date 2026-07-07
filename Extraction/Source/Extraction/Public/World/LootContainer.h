// ALootContainer — self-animating lootable container (cabinet / drawer / crate).
// One container = one Loot() call: contents are granted to the PLAYER through
// UMissionInventorySubsystem regardless of who looted (companion loot still feeds the player).
// The open animation is BP-driven via OnOpened — the skeletal mesh + its anim are assigned in
// the Blueprint subclass; C++ stays asset-agnostic.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Lootable.h"
#include "World/LootTypes.h"
#include "LootContainer.generated.h"

class USkeletalMeshComponent;

UCLASS(Blueprintable, HideCategories = (Replication, Input, LOD, Cooking))
class EXTRACTION_API ALootContainer : public AActor, public ILootable
{
	GENERATED_BODY()

public:
	ALootContainer();

	// --- ILootable ---
	virtual void Loot_Implementation(AActor* Looter) override;
	virtual bool CanLoot_Implementation() const override;

protected:
	/** Container body. Mesh (and its open animation, played from OnOpened) assigned in the BP subclass. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<USkeletalMeshComponent> ContainerMesh;

	/** What this container holds. Authored per placed instance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FLootGrant> Contents;

	/** Set true on a placed instance to start pre-looted (set dressing). Flipped on loot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	bool bLooted = false;

	/** BP hook: play the drawer/lid open animation (and any SFX). Fired once, on loot. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loot")
	void OnOpened(AActor* Looter);

private:
	void GrantAllContents();
};
