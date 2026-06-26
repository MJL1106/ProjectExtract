<#
  ensure-fresh-build.ps1 — guarantee the on-disk editor DLL contains current C++ BEFORE boot.

  Why this exists:
    The boot skill launches UnrealEditor.exe on the bare .uproject. That loads ONLY the base
    module `UnrealEditor-Extraction.dll`. Live Coding patches (UnrealEditor-Extraction.patch_N.*)
    are hot-loaded into a RUNNING editor and do NOT survive a restart — so any change that was
    only LC-patched, or saved-to-source-but-never-fully-built, is absent after a reboot. That is
    the "I saved it but the reboot doesn't have my change" bug. The cure is to compile the editor
    target before launching (which is exactly what Rider does under the hood).

  What it does:
    * If the base DLL is already >= the newest .h/.cpp/.cs under Source -> nothing to do.
    * Otherwise run Build.bat for the editor target and CONFIRM "Result: Succeeded" in the log.
      Exit code alone is NOT proof — a Live Coding / build-mutex lock fast-fails (exit 6) without
      compiling, so we grep the log, per the project's hard-won rule.
    * Refuses to build while THIS project's editor is running (it holds the DLL -> the build would
      fast-fail to a no-op). The caller must be on the BOOT path (editor closed) or close first.

  Prints exactly one terminal line (the caller branches on the first token):
    BUILD-SKIPPED-FRESH                 DLL already current; launch immediately.
    BUILD-OK log=<path>                 rebuilt, "Result: Succeeded" confirmed; launch.
    BUILD-FAILED log=<path>             build did not reach "Result: Succeeded"; DO NOT launch.
    BUILD-BLOCKED-EDITOR-OPEN pids=<..> this project's editor is up; not a real BOOT path.

  Run it backgrounded — a real build is 30-300 s.
#>
[CmdletBinding()]
param(
  [switch]$Force,   # build even if the DLL looks fresh
  [string]$UProject    = 'C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Extraction.uproject',
  [string]$DllPath     = 'C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Binaries\Win64\UnrealEditor-Extraction.dll',
  [string]$SourceRoot  = 'C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Source',
  [string]$BuildBat    = 'C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat',
  [string]$Target      = 'ExtractionEditor',
  [string]$Platform    = 'Win64',
  [string]$Config      = 'Development',
  [string]$UProjectMatch = 'Extraction.uproject'   # command-line filter to scope the editor check
)

$ErrorActionPreference = 'Stop'

# --- Refuse to build while this project's editor holds the DLL (Live Coding lock -> no-op build) ---
$running = @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.CommandLine -like "*$UProjectMatch*" } |
  Select-Object -ExpandProperty ProcessId)
if ($running.Count -gt 0) {
  Write-Output "BUILD-BLOCKED-EDITOR-OPEN pids=$($running -join ',')"
  return
}

# --- 'fresh' = base DLL is at least as new as the newest C++/build source edit -------------------
function Get-BuildState {
  if (-not (Test-Path $DllPath)) { return 'unknown' }   # no DLL yet -> must build
  $dll = (Get-Item $DllPath).LastWriteTimeUtc
  $newest = Get-ChildItem -Path $SourceRoot -Recurse -File -Include *.h,*.cpp,*.cs -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
  if (-not $newest) { return 'fresh' }
  if ($dll -ge $newest.LastWriteTimeUtc) { return 'fresh' } else { return 'stale' }
}

if (-not $Force -and (Get-BuildState) -eq 'fresh') {
  Write-Output "BUILD-SKIPPED-FRESH"
  return
}

# --- Build (editor target, editor closed) and verify by LOG, not exit code ----------------------
$stamp  = (Get-Date).ToString('yyyyMMdd-HHmmss')
$outLog = Join-Path $env:TEMP ("extraction-build-$stamp.out.log")
$errLog = Join-Path $env:TEMP ("extraction-build-$stamp.err.log")

# Start-Process (not 2>&1) avoids PS 5.1 wrapping native stderr into terminating ErrorRecords.
$proc = Start-Process -FilePath $BuildBat `
  -ArgumentList @($Target, $Platform, $Config, "-Project=$UProject", '-WaitMutex') `
  -NoNewWindow -Wait -PassThru `
  -RedirectStandardOutput $outLog -RedirectStandardError $errLog

$succeeded = $false
foreach ($f in @($outLog, $errLog)) {
  if ((Test-Path $f) -and (Select-String -Path $f -Pattern 'Result:\s*Succeeded' -Quiet -ErrorAction SilentlyContinue)) {
    $succeeded = $true; break
  }
}

if ($succeeded) { Write-Output "BUILD-OK log=$outLog" }
else            { Write-Output "BUILD-FAILED log=$outLog exit=$($proc.ExitCode)" }
