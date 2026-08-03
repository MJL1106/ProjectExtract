// UAmmoDropTableDataAsset — central ammo-drop economy, keyed by weapon category.
// One asset instance tunes every enemy's death drop (chance, amount range, pickup class).
// Assigned once on the base enemy Blueprint; mirrors UBarkSetData's enum-keyed map pattern.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Enemy/EnemyTypes.h"
#include "AmmoDropTableDataAsset.generated.h"

class AAmmoPickup;
class AWeaponBase;

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

/** Corpse-gun pickup config, keyed by the enemy's weapon CLASS (not anim category — e.g. the
 *  Rusher's SMG reports the Rifle anim type). Every corpse whose weapon has an entry offers its
 *  gun for direct collection. */
USTRUCT(BlueprintType)
struct FWeaponDropEntry
{
	GENERATED_BODY()

	/** Unused since the corpse-pickup rework — corpse guns are always collectable when an entry
	 *  exists. Kept so existing table assets load without data loss. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropChance = 0.25f;

	/** World pickup actor spawned (the BP_Item_Pickup child for the matching player weapon).
	 *  Must implement a custom event InitDropAmmo(int32 Mag, int32 Reserve). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop")
	TSubclassOf<AActor> PickupClass;

	/** Rounds left in the dropped gun's magazine — enemies never truly run dry, so this fakes a
	 *  believable partial mag. Clamped to the weapon's magazine size when rolled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0"))
	int32 MinMag = 10;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0"))
	int32 MaxMag = 30;

	/** Spare reserve that comes with the dropped gun, counted in WHOLE magazines — a looted gun
	 *  never carries a part-mag remainder, so reserve is always RoundsPerMag * this roll. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "1"))
	int32 MinReserveMags = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "1"))
	int32 MaxReserveMags = 4;

	/** Rounds per magazine used to size the reserve. 0 = read it off the dropped weapon's data
	 *  asset. Set explicitly when the enemy gun and the player gun the pickup grants disagree
	 *  (e.g. the 32-round enemy SMG hands over a 30-round player SMG). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0"))
	int32 RoundsPerMag = 0;
};

UCLASS(BlueprintType)
class EXTRACTION_API UAmmoDropTableDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drops")
	TMap<EEnemyWeaponAnimType, FAmmoDropEntry> DropTable;

	/** Gun drops, keyed by enemy weapon class. Weapons with no entry never drop (LMG, revolver). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drops")
	TMap<TSubclassOf<AWeaponBase>, FWeaponDropEntry> WeaponDropTable;

	const FAmmoDropEntry* Find(EEnemyWeaponAnimType Category) const { return DropTable.Find(Category); }

	/** Exact-class match first, then parent-class fallback so BP subclasses still hit. */
	const FWeaponDropEntry* FindWeaponDrop(const UClass* WeaponClass) const
	{
		if (!WeaponClass) return nullptr;
		if (const FWeaponDropEntry* Exact = WeaponDropTable.Find(const_cast<UClass*>(WeaponClass)))
			return Exact;
		for (const TPair<TSubclassOf<AWeaponBase>, FWeaponDropEntry>& Pair : WeaponDropTable)
			if (Pair.Key && WeaponClass->IsChildOf(Pair.Key))
				return &Pair.Value;
		return nullptr;
	}
};
