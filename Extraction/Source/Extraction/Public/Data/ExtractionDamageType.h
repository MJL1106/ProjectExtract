// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "ExtractionTypes.h"
#include "ExtractionDamageType.generated.h"

/**
 * Project-wide damage type with hitbox multipliers and material flags.
 * Create Blueprint subclasses per weapon / hazard to tune per-region scaling.
 */
UCLASS()
class EXTRACTION_API UExtractionDamageType : public UDamageType
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	bool bIsExplosive = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	bool bCanPenetrate = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float HeadMultiplier = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float LimbMultiplier = 0.75f;

	/** Returns the damage multiplier for the given hit region. Torso = 1.0x. */
	float GetMultiplierForRegion(EHitRegion Region) const;
};
