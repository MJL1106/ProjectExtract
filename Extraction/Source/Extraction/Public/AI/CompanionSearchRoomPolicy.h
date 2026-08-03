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

	/** Full acquisition gate: an enemy is a valid target if it is pressuring the player, if it has
	 *  engaged the COMPANION itself, or if the mode is weapons-free. Deliberately mode-blind on the
	 *  companion-aware clause — self-defence is not a mode setting. Stealth is excluded at the call
	 *  site instead, which passes false for bEnemyAwareOfCompanion so an unbroken stealth approach
	 *  cannot start a fight off an enemy that shot the companion in some earlier mode. */
	inline bool CanAcquireTarget(ECompanionMode Mode, bool bEnemyDetectedPlayer,
		bool bEnemyAwareOfCompanion)
	{
		return bEnemyDetectedPlayer
			|| bEnemyAwareOfCompanion
			|| CanEngageUnawareEnemy(Mode);
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
