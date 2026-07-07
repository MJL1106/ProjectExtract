// UAmmoDropTableDataAsset — central ammo-drop economy, keyed by weapon category.
// One asset instance tunes every enemy's death drop (chance, amount range, pickup class).
// Assigned once on the base enemy Blueprint; mirrors UBarkSetData's enum-keyed map pattern.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Enemy/EnemyTypes.h"
#include "AmmoDropTableDataAsset.generated.h"

class AAmmoPickup;

USTRUCT(BlueprintType)
struct FAmmoDropEntry
{
	GENERATED_BODY()

	/** Chance [0,1] that an enemy holding this weapon category drops ammo on death. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropChance = 0.4f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0"))
	int32 MinAmount = 15;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0"))
	int32 MaxAmount = 45;

	/** Pickup actor spawned for this category (BP child with the mesh assigned). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop")
	TSubclassOf<AAmmoPickup> PickupClass;
};

UCLASS(BlueprintType)
class EXTRACTION_API UAmmoDropTableDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drops")
	TMap<EEnemyWeaponAnimType, FAmmoDropEntry> DropTable;

	const FAmmoDropEntry* Find(EEnemyWeaponAnimType Category) const { return DropTable.Find(Category); }
};
