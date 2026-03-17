// Data-driven weapon configuration. One asset per weapon archetype.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExtractionTypes.h"
#include "WeaponDataAsset.generated.h"

class UExtractionDamageType;

/**
 * Holds all tuning values for a single weapon type.
 * Create one DataAsset instance per weapon archetype (AR, SMG, etc.)
 * and reference it from the weapon actor's DefaultWeaponData property.
 */
UCLASS(BlueprintType)
class EXTRACTION_API UWeaponDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	// ---- Fire ----

	/** Rounds per second (e.g. 10.0 = 600 RPM) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.1"))
	float FireRate = 10.0f;

	/** Whether holding fire continuously shoots */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	bool bIsAutomatic = true;

	/** Base damage per hit before hitbox multipliers */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "0.0"))
	float BaseDamage = 25.0f;

	/** Maximum hitscan trace range in cm */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire", meta = (ClampMin = "100.0"))
	float MaxRange = 10000.0f;

	/** Damage type class applied on hit */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Fire")
	TSubclassOf<UExtractionDamageType> DamageTypeClass;

	// ---- Ammo ----

	/** Shots per magazine */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "1"))
	int32 MagazineSize = 30;

	/** Starting reserve ammo (total extra rounds carried) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0"))
	int32 DefaultReserveAmmo = 120;

	/** Time in seconds for a full reload */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Ammo", meta = (ClampMin = "0.1"))
	float ReloadTime = 2.2f;

	// ---- ADS ----

	/** Target field of view while aiming down sights */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|ADS", meta = (ClampMin = "20.0", ClampMax = "120.0"))
	float ADSFOV = 65.0f;

	/** Time in seconds for the FOV transition */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|ADS", meta = (ClampMin = "0.01"))
	float ADSTransitionTime = 0.15f;

	/** Max walk speed while ADS in cm/s */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|ADS", meta = (ClampMin = "0.0"))
	float ADSMovementSpeed = 400.0f;

	// ---- Recoil ----

	/** Recoil pattern data for this weapon */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Recoil")
	FRecoilPattern RecoilPattern;
};
