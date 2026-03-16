// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtractionDamageType.h"

float UExtractionDamageType::GetMultiplierForRegion(EHitRegion Region) const
{
	switch (Region)
	{
	case EHitRegion::Head:  return HeadMultiplier;
	case EHitRegion::Arms:  // fall through
	case EHitRegion::Legs:  return LimbMultiplier;
	default:                return 1.0f;
	}
}
