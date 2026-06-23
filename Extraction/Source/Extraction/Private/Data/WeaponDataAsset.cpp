// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeaponDataAsset.h"

void UWeaponDataAsset::PostLoad()
{
	Super::PostLoad();

	if (PelletCount > 1 && PelletSpreadDeg <= 0.f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UWeaponDataAsset '%s': PelletCount=%d but PelletSpreadDeg=0 — all pellets stack on one point (N× damage). Set PelletSpreadDeg > 0 for a real spread pattern."),
			*GetName(), PelletCount);
	}
}
