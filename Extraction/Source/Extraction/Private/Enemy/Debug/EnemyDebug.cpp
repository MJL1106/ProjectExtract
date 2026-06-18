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

static TAutoConsoleVariable<int32> CVarDetectionLog(
	TEXT("enemy.DetectionLog"),
	1,
	TEXT("Detection diagnostic log: 0=off, 1=log [DETECTDBG] sight verdict (CanBeSeenFrom), sight/hearing/shot-at stimuli, and combat entry. Default 1 (temporary diagnostic)."),
	ECVF_Cheat);

int32 GetDetectionLogLevel()
{
	return CVarDetectionLog.GetValueOnGameThread();
}

static TAutoConsoleVariable<int32> CVarSightDiag(
	TEXT("enemy.SightDiag"),
	0,
	TEXT("If >0, logs a per-enemy sight-gate diagnostic for the local player (dist/cone/LOS/sighted) while not in Combat."),
	ECVF_Cheat);

int32 GetSightDiagLevel()
{
	return CVarSightDiag.GetValueOnGameThread();
}

static TAutoConsoleVariable<FString> CVarSightDiagFilter(
	TEXT("enemy.SightDiagFilter"),
	TEXT(""),
	TEXT("Substring filter for [SIGHTDIAG] output. Only enemies whose pawn label contains this string (case-insensitive) are logged. Empty = log all."),
	ECVF_Cheat);

FString GetSightDiagFilter()
{
	return CVarSightDiagFilter.GetValueOnGameThread();
}
