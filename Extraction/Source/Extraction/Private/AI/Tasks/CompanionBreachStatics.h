// Shared breach-door mechanics for the companion command tasks — the swing + per-type noise is
// single-sourced here so the ordered breach (BTTask_CompanionBreach) and the door search
// (BTTask_CompanionExplore) can never diverge on the door contract.

#pragma once

#include "CoreMinimal.h"
#include "Companion/CompanionCommandTypes.h"

class ACompanionAIController;
class APawn;

namespace CompanionBreachStatics
{
	/** Swing the breach door and emit the per-type hearing noise (montage contact time reached).
	 *  Re-checks CanBreach so a door opened by something else during the wind-up never
	 *  double-swings. Returns false when the swing was skipped for that reason. */
	bool OpenBreachDoor(ACompanionAIController* AIC, AActor* Door, EBreachType BreachType,
		const FVector& SearchRoomAnchor, float SearchRoomExposureRadius);

	/** Shared room anchor for Search/Breach exposure, engagement, and loot scope. */
	FVector ResolveInteriorAnchor(AActor* Door, APawn* Breacher);
}
