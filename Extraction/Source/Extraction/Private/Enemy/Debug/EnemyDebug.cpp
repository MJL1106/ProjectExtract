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
	0,
	TEXT("Detection diagnostic log: 0=off, 1=log [DETECTDBG] sight verdict (CanBeSeenFrom), sight/hearing/shot-at stimuli, and combat entry."),
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

static TAutoConsoleVariable<int32> CVarReloadDebug(
	TEXT("enemy.ReloadDebug"),
	0,
	TEXT("Deep reload diagnostic: 0=off, 1=log + on-screen the full enemy reload path (weapon reload entry, montage resolution, slot, play result)."),
	ECVF_Cheat);

int32 GetReloadDebugLevel()
{
	return CVarReloadDebug.GetValueOnGameThread();
}

bool IsReloadDebugEnabled()
{
	return CVarReloadDebug.GetValueOnGameThread() != 0;
}

static TAutoConsoleVariable<int32> CVarForceCover(
	TEXT("enemy.ForceCover"),
	0,
	TEXT("Cover debug: 0=off, 1=enemies always seek cover in combat (reseek from open every 0.5s regardless of morale) and PLANT there — shuffle, flank-break relocate and compromise invalidation disabled so cover anims can be observed."),
	ECVF_Cheat);

int32 GetForceCoverLevel()
{
	return CVarForceCover.GetValueOnGameThread();
}

static TAutoConsoleVariable<int32> CVarCoverAnimLog(
	TEXT("enemy.CoverAnimLog"),
	0,
	TEXT("Cover animation diagnostic: 0=off, 1=log [COVERANIM] pose mirror edges, velocity-gate open/close, montage selection (including NULL slots) and play results."),
	ECVF_Cheat);

int32 GetCoverAnimLogLevel()
{
	return CVarCoverAnimLog.GetValueOnGameThread();
}

static TAutoConsoleVariable<int32> CVarFlankBreakLog(
	TEXT("enemy.FlankBreakLog"),
	0,
	TEXT("Cover flank-break diagnostic: 0=off, 1=log [FLANKDBG] per compromise eval (arc/body-shield verdict, debounce count, cooldown, phase) and on relocate (protective slot found vs strafe fallback)."),
	ECVF_Cheat);

int32 GetFlankBreakLogLevel()
{
	return CVarFlankBreakLog.GetValueOnGameThread();
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

static int32 GetForceCoverHeight()
{
	return CVarForceCoverHeight.GetValueOnGameThread();
}

static TAutoConsoleVariable<int32> CVarCoverMoveDebug(
	TEXT("enemy.CoverMoveDebug"),
	0,
	TEXT("Cover-move diagnostic: 0=off, 1=per-tick [COVERMOVEDBG] log + directional arrows during cover-move windows (shuffle hold, facing-active transit, SeekingCover, post-arrival soak)."),
	ECVF_Cheat);

int32 GetCoverMoveDebugLevel()
{
	return CVarCoverMoveDebug.GetValueOnGameThread();
}
