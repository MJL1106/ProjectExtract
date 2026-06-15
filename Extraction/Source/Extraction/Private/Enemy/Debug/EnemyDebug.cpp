// Console variable for enemy debug overlay (enemy.DrawDebug).

#include "EnemyDebug.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarEnemyDrawDebug(
	TEXT("enemy.DrawDebug"),
	0,
	TEXT("Enemy AI debug overlay: 0=off, 1=head-tag (archetype/state/suspicion), 2=full in-world overlay (vision cone, target line, markers, BT task)."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarEnemyDrawBarkDebug(
	TEXT("enemy.DrawBarkDebug"),
	0,
	TEXT("Enemy bark debug: 0=off, 1=log + on-screen messages for all bark requests (fired/dropped with gate info)."),
	ECVF_Cheat);

int32 GetEnemyDrawDebugLevel()
{
	return CVarEnemyDrawDebug.GetValueOnGameThread();
}

bool IsEnemyDrawDebugEnabled()
{
	return CVarEnemyDrawDebug.GetValueOnGameThread() != 0;
}

int32 GetEnemyBarkDebugLevel()
{
	return CVarEnemyDrawBarkDebug.GetValueOnGameThread();
}

bool IsEnemyBarkDebugEnabled()
{
	return CVarEnemyDrawBarkDebug.GetValueOnGameThread() != 0;
}

static TAutoConsoleVariable<int32> CVarAwarenessMeterLog(
	TEXT("enemy.AwarenessMeterLog"),
	0,
	TEXT("Awareness meter diagnostic log: 0=off, 1=log state/fill/visibility once per second per enemy."),
	ECVF_Cheat);

int32 GetAwarenessMeterLogLevel()
{
	return CVarAwarenessMeterLog.GetValueOnGameThread();
}
