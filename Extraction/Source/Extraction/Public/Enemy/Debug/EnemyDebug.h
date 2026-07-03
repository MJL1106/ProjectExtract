// Console variable and helpers for enemy debug overlay.

#pragma once

#include "CoreMinimal.h"

/** Returns the current enemy.DrawDebug cvar value (0=off, 1=head-tag, 2=full in-world overlay). */
int32 GetEnemyDrawDebugLevel();

/** Returns true when the enemy.DrawDebug cvar is non-zero (backwards-compat for existing callers). */
bool IsEnemyDrawDebugEnabled();

/** Returns the current enemy.DrawBarkDebug cvar value (0=off, 1=on). Separate from DrawDebug due to high frequency. */
int32 GetEnemyBarkDebugLevel();

/** Returns true when the enemy.DrawBarkDebug cvar is non-zero. */
bool IsEnemyBarkDebugEnabled();

/** Returns the current enemy.AwarenessMeterLog cvar value (0=off, 1=on). */
int32 GetAwarenessMeterLogLevel();

/** Returns the current enemy.DetectionLog cvar value (0=off, 1=on). */
int32 GetDetectionLogLevel();

/** Returns the current enemy.SightDiag cvar value (0=off, 1=on).
 *  When on, each enemy logs a per-tick sight-gate diagnostic against the local player pawn:
 *  distance, in-range, in-cone, body-LOS, head-clear, engine-sighted, suspicion, and state. */
int32 GetSightDiagLevel();

/** Returns the current enemy.SightDiagFilter string (case-insensitive substring match on pawn label).
 *  Empty = no filter (all enemies logged). */
FString GetSightDiagFilter();

/** Returns the current enemy.ReloadDebug cvar value (0=off, 1=on). */
int32 GetReloadDebugLevel();

/** Returns true when the enemy.ReloadDebug cvar is non-zero. */
bool IsReloadDebugEnabled();

/** Returns the current enemy.FlankBreakLog cvar value (0=off, 1=on). When on, logs [FLANKDBG] per
 *  cover-compromise eval: arc verdict, hunkered body-shield verdict, debounce count, cooldown, phase. */
int32 GetFlankBreakLogLevel();

/** Returns the current enemy.ForceCover cvar value (0=off, 1=always seek + hold cover in combat —
 *  shuffle/relocate/compromise disabled so cover animation can be observed in place). */
int32 GetForceCoverLevel();

/** Returns the current enemy.CoverAnimLog cvar value (0=off, 1=log [COVERANIM] pose mirror edges,
 *  velocity-gate transitions, montage selection and play results). */
int32 GetCoverAnimLogLevel();

/** Returns the current enemy.ForceCoverPeekSide cvar value (0=auto, 1=force left corner peek,
 *  2=force right corner peek, 3=force over-top). Skips the gap/LOS side checks entirely so the
 *  side-peek montages and weapon-align poses can be observed regardless of baked cover flags. */
int32 GetForceCoverPeekSide();
