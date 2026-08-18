// Console variables that force enemy cover behaviour for shot-list / manual testing.
// These are behaviour knobs, not diagnostics — they change what the AI does, not what it draws.

#pragma once

#include "CoreMinimal.h"

/** Returns the current enemy.ForceCover cvar value (0=off, 1=always seek + hold cover in combat —
 *  shuffle/relocate/compromise disabled so cover animation can be observed in place). */
int32 GetForceCoverLevel();

/** Returns the current enemy.ForceCoverPeekSide cvar value (0=auto, 1=force left corner peek,
 *  2=force right corner peek, 3=force over-top). Skips the gap/LOS side checks entirely so the
 *  side-peek montages and weapon-align poses can be observed regardless of baked cover flags. */
int32 GetForceCoverPeekSide();

/** Returns the current enemy.ForceCoverReposition cvar value (0=off, 1=same-wall shuffle every
 *  pause cycle, 2=full relocate every pause cycle). For visual testing of cover movement. */
int32 GetForceCoverRepositionLevel();

/** Returns the current enemy.ForceCoverHeight cvar value (0=auto, 1=crouch-only cover points,
 *  2=stand-only). Mismatched points are rejected from EQS queries and hand-rolled cover searches.
 *  Call this rather than resolving the cvar by name into a cached IConsoleVariable* — a Live
 *  Coding patch leaves such a cached pointer stale (see the note in ACompanionRoute::BeginPlay). */
int32 GetForceCoverHeightLevel();
