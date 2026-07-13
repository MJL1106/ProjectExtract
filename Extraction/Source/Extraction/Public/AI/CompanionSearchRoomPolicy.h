// Shared policy for Search/Breach engagement, loot chaining, and threat-facing ownership.

#pragma once

#include "Companion/CompanionCommandTypes.h"
#include "Companion/CompanionTypes.h"

namespace CompanionSearchRoomPolicy
{
	inline bool CanEngageUnawareEnemy(ECompanionMode Mode)
	{
		return Mode == ECompanionMode::Combat;
	}

	inline bool ShouldTreatRoomAsHot(ECompanionMode Mode, bool bHasCombatTarget,
		bool bHasVisibleLiveEnemy)
	{
		return bHasCombatTarget
			|| (Mode == ECompanionMode::Combat && bHasVisibleLiveEnemy);
	}

	inline bool CommandYieldsThreatFacing(ECompanionCommand Command, bool bPathMoving,
		bool bSearchRoomExposureActive)
	{
		return Command != ECompanionCommand::None
			&& (Command != ECompanionCommand::Loot || bPathMoving || !bSearchRoomExposureActive);
	}

	inline bool PostureOwnsReadyThreatFacing(bool bStealthActive,
		ECompanionCommand Command, bool bPathMoving, bool bSearchRoomExposureActive)
	{
		return !bStealthActive
			&& !CommandYieldsThreatFacing(Command, bPathMoving, bSearchRoomExposureActive);
	}
}
