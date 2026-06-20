<#
  engine-guard.ps1 — cross-chat coordination for the ProjectExtract Unreal editor.

  Separate Claude Code chats are separate processes; the only thing they share is the
  filesystem. This script uses a single lock file (.claude/engine-session.lock.json) as a
  registry so that:
    * a chat ADOPTS an already-running editor instead of booting a duplicate,
    * the editor's owner + boot time + per-chat heartbeats are tracked,
    * a chat refuses to kill an editor it did not boot, or that another chat is still using.

  Actions:
    status      Print running editor PIDs + the current lock contents (JSON).
    boot-check  Decide what to do before launching. Prints one of:
                  ADOPT <pid> owner=<tok>   -> editor already up; DO NOT launch. Caller proceeds.
                  BOOT                      -> none running, boot lock acquired. Caller launches,
                                               then calls 'register -EditorPid <pid>'.
                  WAIT owner=<tok>          -> another chat is mid-boot. Caller waits + re-checks.
                  DUPLICATE <pid,pid>       -> >1 editor already running. Caller STOPS + warns user.
    register    Record the launched editor PID as owned by this token (call after BOOT + launch).
    heartbeat   Refresh this token's lastSeen (call when doing in-engine work).
    can-close   Decide whether this chat may kill the editor. Prints one of:
                  OK <pid,...>              -> this chat owns it, no other active users. Safe to kill.
                  BLOCKED not-owner ...     -> another chat booted it. DO NOT kill; ask the user.
                  BLOCKED other-users ...   -> another chat is active. DO NOT kill; ask the user.
                  NO-EDITOR                 -> nothing to close.
    release     Delete the lock (call after the editor is actually closed).

  -Token is this chat's stable per-session GUID. Generate once, reuse for every call this session.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory)][ValidateSet('status','boot-check','register','heartbeat','can-close','release')]
  [string]$Action,
  [string]$Token = '',
  [int]$EditorPid = 0,
  [string]$UProject = 'Extraction.uproject',
  [int]$StaleMinutes = 15
)

$ErrorActionPreference = 'Stop'
$LockPath = 'C:\Users\matth\Documents\Github\ProjectExtract\.claude\engine-session.lock.json'

function Get-EditorPids {
  @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like "*$UProject*" } |
    Select-Object -ExpandProperty ProcessId)
}
function NowIso { (Get-Date).ToUniversalTime().ToString('o') }

function Read-Lock {
  if (Test-Path $LockPath) {
    try { return (Get-Content $LockPath -Raw -ErrorAction Stop | ConvertFrom-Json) } catch { return $null }
  }
  return $null
}
# Normalize a parsed lock (or $null) into a mutable hashtable with a users array of hashtables.
function To-Ht($lock) {
  $users = @()
  if ($lock -and $lock.users) {
    foreach ($u in $lock.users) { $users += @{ token = "$($u.token)"; lastSeen = "$($u.lastSeen)" } }
  }
  $p = 0; if ($lock -and $lock.pid) { $p = [int]$lock.pid }
  $o = ''; if ($lock -and $lock.owner) { $o = "$($lock.owner)" }
  $b = ''; if ($lock -and $lock.bootedAt) { $b = "$($lock.bootedAt)" }
  $bt = $false; if ($lock -and $lock.booting) { $bt = [bool]$lock.booting }
  return @{ pid = $p; owner = $o; bootedAt = $b; booting = $bt; users = $users }
}
function Write-Lock($ht) {
  $dir = Split-Path $LockPath -Parent
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
  ($ht | ConvertTo-Json -Depth 6) | Set-Content -Path $LockPath -Encoding UTF8
}
function Touch-User($ht, $tok) {
  if (-not $tok) { return $ht }
  $found = $false
  foreach ($u in $ht.users) { if ($u.token -eq $tok) { $u.lastSeen = (NowIso); $found = $true } }
  if (-not $found) { $ht.users += @{ token = $tok; lastSeen = (NowIso) } }
  return $ht
}

switch ($Action) {

  'status' {
    $pids = Get-EditorPids
    [PSCustomObject]@{ runningPids = $pids; lock = (Read-Lock) } | ConvertTo-Json -Depth 6
  }

  'boot-check' {
    $pids = Get-EditorPids
    if ($pids.Count -gt 1) { Write-Output "DUPLICATE $($pids -join ',')"; break }
    if ($pids.Count -eq 1) {
      $ht = To-Ht (Read-Lock)
      $ht.pid = $pids[0]
      if (-not $ht.bootedAt) { $ht.bootedAt = (NowIso) }
      if (-not $ht.owner) { $ht.owner = '(pre-existing)' }
      $ht.booting = $false
      $ht = Touch-User $ht $Token
      Write-Lock $ht
      Write-Output "ADOPT $($pids[0]) owner=$($ht.owner)"; break
    }
    # No editor running — claim the boot lock atomically (CreateNew fails if it exists).
    try {
      $fs = [System.IO.File]::Open($LockPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
      $ht = @{ pid = 0; owner = $Token; bootedAt = (NowIso); booting = $true; users = @(@{ token = $Token; lastSeen = (NowIso) }) }
      $bytes = [System.Text.Encoding]::UTF8.GetBytes(($ht | ConvertTo-Json -Depth 6))
      $fs.Write($bytes, 0, $bytes.Length); $fs.Close()
      Write-Output "BOOT"; break
    } catch {
      # Lock exists. Another chat may be mid-boot, or it's an orphan from a dead editor.
      $lock = To-Ht (Read-Lock)
      $fresh = $false
      if ($lock.bootedAt) { try { $fresh = ([datetime]$lock.bootedAt) -gt (Get-Date).ToUniversalTime().AddMinutes(-3) } catch { $fresh = $false } }
      if ($lock.booting -and $fresh) { Write-Output "WAIT owner=$($lock.owner)"; break }
      $ht = @{ pid = 0; owner = $Token; bootedAt = (NowIso); booting = $true; users = @(@{ token = $Token; lastSeen = (NowIso) }) }
      Write-Lock $ht
      Write-Output "BOOT (reclaimed stale lock)"; break
    }
  }

  'register' {
    $ht = To-Ht (Read-Lock)
    $ht.pid = $EditorPid
    if ($Token) { $ht.owner = $Token }
    $ht.bootedAt = (NowIso)
    $ht.booting = $false
    $ht = Touch-User $ht $Token
    Write-Lock $ht
    Write-Output "REGISTERED $EditorPid owner=$($ht.owner)"
  }

  'heartbeat' {
    $ht = Touch-User (To-Ht (Read-Lock)) $Token
    Write-Lock $ht
    Write-Output "OK"
  }

  'can-close' {
    $pids = Get-EditorPids
    if ($pids.Count -eq 0) { Write-Output "NO-EDITOR"; break }
    $lock = To-Ht (Read-Lock)
    if ($lock.owner -and $lock.owner -ne $Token) { Write-Output "BLOCKED not-owner owner=$($lock.owner) pids=$($pids -join ',')"; break }
    $cutoff = (Get-Date).ToUniversalTime().AddMinutes(-$StaleMinutes)
    $others = @()
    foreach ($u in $lock.users) {
      if ($u.token -and $u.token -ne $Token) {
        $ls = $null; try { $ls = [datetime]$u.lastSeen } catch {}
        if ($ls -and $ls -gt $cutoff) { $others += $u.token }
      }
    }
    if ($others.Count -gt 0) { Write-Output "BLOCKED other-users=$($others -join ',') pids=$($pids -join ',')"; break }
    Write-Output "OK $($pids -join ',')"
  }

  'release' {
    if (Test-Path $LockPath) { Remove-Item $LockPath -Force }
    Write-Output "RELEASED"
  }
}
