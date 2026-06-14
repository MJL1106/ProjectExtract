// Console variable and helpers for enemy debug overlay.

#pragma once

#include "CoreMinimal.h"

/** Returns the current enemy.DrawDebug cvar value (0=off, 1=head-tag, 2=full in-world overlay). */
int32 GetEnemyDrawDebugLevel();

/** Returns true when the enemy.DrawDebug cvar is non-zero (backwards-compat for existing callers). */
bool IsEnemyDrawDebugEnabled();
