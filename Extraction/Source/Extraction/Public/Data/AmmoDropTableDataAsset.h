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

/** Chance for an enemy to drop its actual gun as a world weapon pickup. Keyed by the enemy's
 *  weapon CLASS (not anim category — e.g. the Rusher's SMG reports the Rifle anim type). */
USTRUCT(BlueprintType)
struct FWeaponDropEntry
{
	GENERATED_BODY()

	/** Chance [0,1] that this enemy weapon drops as a pickup on death. A successful gun drop
	 *  replaces that kill's ammo drop — the gun is the ammo that time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropChance = 0.25f;

	/** World pickup actor spawned (the BP_Item_Pickup child for the matching player weapon).
	 *  Must implement a custom event InitDropAmmo(int32 Mag, int32 Reserve). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop")
	TSubclassOf<AActor> PickupClass;

	/** Rounds left in the dropped gun's magazine — enemies never truly run dry, so this fakes a
	 *  believable partial mag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0"))
	int32 MinMag = 5;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0"))
	int32 MaxMag = 25;

	/** Spare reserve ammo that comes with the dropped gun. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0"))
	int32 MinReserve = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drop", meta = (ClampMin = "0"))
	int32 MaxReserve = 30;
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
