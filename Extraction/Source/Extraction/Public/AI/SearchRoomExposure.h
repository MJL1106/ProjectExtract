// Plain server-side state and math for command-scoped Search/Breach exposure.

#pragma once

#include "CoreMinimal.h"

struct FSearchRoomExposureState
{
	void Begin(const FVector& InRoomAnchor, float Radius, bool bInSilentStartle)
	{
		if (Radius <= 0.f)
		{
			End();
			return;
		}

		++Generation;
		if (Generation == 0) ++Generation;

		bActive = true;
		RoomAnchor = InRoomAnchor;
		RadiusSquared = FMath::Square(Radius);
		bSilentStartle = bInSilentStartle;
	}

	void End()
	{
		bActive = false;
		RoomAnchor = FVector::ZeroVector;
		RadiusSquared = 0.f;
		bSilentStartle = false;
	}

	bool IsActive() const { return bActive; }
	uint32 GetActiveGeneration() const { return bActive ? Generation : 0; }

	bool IsObserverInScope(const FVector& ObserverLocation) const
	{
		return bActive && FVector::DistSquared(ObserverLocation, RoomAnchor) <= RadiusSquared;
	}

	bool HasSilentStartle(uint32 ExposureGeneration) const
	{
		return bActive && bSilentStartle && ExposureGeneration != 0 && ExposureGeneration == Generation;
	}

private:
	bool bActive = false;
	FVector RoomAnchor = FVector::ZeroVector;
	float RadiusSquared = 0.f;
	uint32 Generation = 0;
	bool bSilentStartle = false;
};

namespace SearchRoomExposure
{
	inline float ApplyStartleSuspicion(float CurrentSuspicion, float NormalGain,
		float SuspicionFloor, float HearingOnlyCap)
	{
		return FMath::Min(FMath::Max(CurrentSuspicion + NormalGain, SuspicionFloor), HearingOnlyCap);
	}
}
