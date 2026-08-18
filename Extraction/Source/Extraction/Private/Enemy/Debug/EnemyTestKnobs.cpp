// Console variables that force enemy cover behaviour for shot-list / manual testing.

#include "EnemyTestKnobs.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarForceCover(
	TEXT("enemy.ForceCover"),
	0,
	TEXT("Cover debug: 0=off, 1=enemies always seek cover in combat (reseek from open every 0.5s regardless of morale) and PLANT there — shuffle, flank-break relocate and compromise invalidation disabled so cover anims can be observed."),
	ECVF_Cheat);

int32 GetForceCoverLevel()
{
	return CVarForceCover.GetValueOnGameThread();
}

static TAutoConsoleVariable<int32> CVarForceCoverPeekSide(
	TEXT("enemy.ForceCoverPeekSide"),
	0,
	TEXT("Force the cover peek side for testing: 0=auto (baked flags + LOS verify), 1=left corner peek, 2=right corner peek, 3=over-top. Bypasses the gap/LOS side checks."),
	ECVF_Cheat);

int32 GetForceCoverPeekSide()
{
	return CVarForceCoverPeekSide.GetValueOnGameThread();
}

static TAutoConsoleVariable<int32> CVarForceCoverReposition(
	TEXT("enemy.ForceCoverReposition"),
	0,
	TEXT("Force cover repositioning every pause cycle for visual testing. 0=off, 1=same-wall shuffle, 2=relocate to new cover."),
	ECVF_Cheat);

int32 GetForceCoverRepositionLevel()
{
	return CVarForceCoverReposition.GetValueOnGameThread();
}

static TAutoConsoleVariable<int32> CVarForceCoverHeight(
	TEXT("enemy.ForceCoverHeight"),
	0,
	TEXT("Force cover-height selection for visual testing: 0=auto, 1=crouch-only cover points, 2=stand-only cover points. Mismatched points are rejected from EQS queries and hand-rolled cover searches."),
	ECVF_Cheat);

int32 GetForceCoverHeightLevel()
{
	return CVarForceCoverHeight.GetValueOnGameThread();
}
