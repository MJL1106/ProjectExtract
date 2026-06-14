// Console variable for enemy debug overlay (enemy.DrawDebug).

#include "EnemyDebug.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarEnemyDrawDebug(
	TEXT("enemy.DrawDebug"),
	0,
	TEXT("Enemy AI debug overlay: 0=off, 1=head-tag (archetype/state/suspicion), 2=full in-world overlay (vision cone, target line, markers, BT task)."),
	ECVF_Cheat);

int32 GetEnemyDrawDebugLevel()
{
	return CVarEnemyDrawDebug.GetValueOnGameThread();
}

bool IsEnemyDrawDebugEnabled()
{
	return CVarEnemyDrawDebug.GetValueOnGameThread() != 0;
}
