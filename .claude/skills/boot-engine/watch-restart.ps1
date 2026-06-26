<#
  watch-restart.ps1 — "listen for a restart" so a chat can get its C++ changes into the editor
  WITHOUT closing an editor another chat is using.

  The user routinely multitasks several chats on one shared editor + one shared working tree.
  When THIS chat needs a rebuild but another chat is using the editor, the right move is not to
  kill it — it's to wait for that chat (or the user) to rebuild+reboot. Because all chats share
  one tree, that rebuild compiles THIS chat's saved source too. This script blocks (run it in the
  background) until it can prove the running editor is on a build that contains current source,
  then exits so the harness wakes the agent.

  Detection — ALL must hold:
    * an editor process for this project is running,
    * its process start time is NEWER than the base DLL's write time (the live process actually
      loaded the rebuilt DLL — a fresh DLL on disk under an OLD process does NOT count),
    * build is 'fresh' (DLL >= newest .h/.cpp/.cs under Source -> every saved change is compiled in).

  Prints exactly one terminal line on exit:
    RESTARTED pid=<pid> dll=<iso>     editor rebooted on a build that includes your changes; proceed.
    RESTART-TIMEOUT waited=<min>m     no qualifying restart in the window; re-ask the user.

  Launch with run_in_background:true so the session is not blocked.
#>
[CmdletBinding()]
param(
  [int]$PollSeconds = 20,
  [int]$TimeoutMinutes = 60,
  [string]$UProjectMatch = 'Extraction.uproject',
  [string]$DllPath    = 'C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Binaries\Win64\UnrealEditor-Extraction.dll',
  [string]$SourceRoot = 'C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Source'
)
$ErrorActionPreference = 'Stop'

function Get-EditorProc {
  Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like "*$UProjectMatch*" } |
    Sort-Object CreationDate -Descending | Select-Object -First 1
}
function Get-NewestSourceUtc {
  $n = Get-ChildItem -Path $SourceRoot -Recurse -File -Include *.h,*.cpp,*.cs -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
  if ($n) { return $n.LastWriteTimeUtc } else { return $null }
}

$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
while ($true) {
  $proc = Get-EditorProc
  if ($proc -and (Test-Path $DllPath)) {
    $dllUtc     = (Get-Item $DllPath).LastWriteTimeUtc
    $startedUtc = $proc.CreationDate.ToUniversalTime()
    $newest     = Get-NewestSourceUtc
    $isFresh    = (-not $newest) -or ($dllUtc -ge $newest)
    if (($startedUtc -gt $dllUtc) -and $isFresh) {
      Write-Output ("RESTARTED pid={0} dll={1}" -f $proc.ProcessId, $dllUtc.ToString('o'))
      return
    }
  }
  if ((Get-Date) -ge $deadline) { break }
  Start-Sleep -Seconds $PollSeconds
}
Write-Output ("RESTART-TIMEOUT waited={0}m" -f $TimeoutMinutes)
