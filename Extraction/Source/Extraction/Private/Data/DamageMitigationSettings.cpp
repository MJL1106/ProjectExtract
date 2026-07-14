// Copyright Epic Games, Inc. All Rights Reserved.

#include "DamageMitigationSettings.h"
#include "Curves/CurveFloat.h"

float UDamageMitigationSettings::RampChance01(const UDamageMitigationSettings& S, float TimeOnTarget)
{
	float Alpha = FMath::Clamp(TimeOnTarget / FMath::Max(S.RampTime, 0.01f), 0.f, 1.f);

	if (IsValid(S.RampCurve))
		Alpha = FMath::Clamp(S.RampCurve->GetFloatValue(Alpha), 0.f, 1.f);

	return FMath::Lerp(S.AcquireHitChance, S.SettledHitChance, Alpha);
}

float UDamageMitigationSettings::EffectiveResetGap(const UDamageMitigationSettings& S, float FireRateRPS)
{
	const float SecondsPerShot = 1.f / FMath::Max(FireRateRPS, 0.01f);
	return FMath::Max(S.RampResetGapSeconds, S.ShotGapResetMultiplier * SecondsPerShot);
}
