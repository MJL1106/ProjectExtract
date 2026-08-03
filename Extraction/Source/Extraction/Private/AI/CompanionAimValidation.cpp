// Shared aim-bearing validation — see header for the contract (decline, never re-bear).

#include "AI/CompanionAimValidation.h"

#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace CompanionAim
{
	namespace
	{
		// How many pawns the overwatch traces step past before giving up. Bounds the re-trace loop — a
		// firing line with five bodies standing in it is not a standoff measurement worth salvaging.
		constexpr int32 OverwatchTracePawnSkipLimit = 4;
	}

	bool TraceGeometryPastPawns(const UWorld& World, const FVector& Start, const FVector& End,
		FCollisionQueryParams Params, FHitResult& OutHit)
	{
		for (int32 Attempt = 0; Attempt <= OverwatchTracePawnSkipLimit; ++Attempt)
		{
			if (!World.LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params)) return false;

			const AActor* HitActor = OutHit.GetActor();
			if (!IsValid(HitActor)) return true;

			// A weapon or other attached actor rides its owner — same "not a wall" read.
			const AActor* HitOwner = HitActor->GetOwner();
			if (!HitActor->IsA<APawn>() && !(IsValid(HitOwner) && HitOwner->IsA<APawn>())) return true;

			Params.AddIgnoredActor(HitActor);
		}
		// Out of skips: treat the last hit as geometry rather than claim a clear line.
		return true;
	}
}
