// Thin assault rifle subclass — sets default WeaponDataAsset reference.

#pragma once

#include "CoreMinimal.h"
#include "WeaponBase.h"
#include "AssaultRifle.generated.h"

UCLASS(Blueprintable)
class EXTRACTION_API AAssaultRifle : public AWeaponBase
{
	GENERATED_BODY()

public:

	AAssaultRifle();
};
