<#
  engine-guard.ps1 — cross-chat coordination for the ProjectExtract Unreal editor.

  Separate Claude Code chats are separate processes; the only thing they share is the
  filesystem. This script uses a single lock file (.claude/engine-session.lock.json) as a
  registry, plus a named system mutex, so that:
    * only ONE chat can ever be mid-boot at a time (the mutex serialises the decision),
    * a chat ADOPTS an already-running editor instead of booting a duplicate,
    * even if a duplicate ever slips through, the chat that launched the extra editor kills
      its OWN just-launched process and adopts the survivor (never end with two editors),
    * ADOPT reports whether the running editor's build already contains current C++ changes,
      so a chat doesn't reboot needlessly (all chats share one working tree → one build
      contains every chat's edits),
    * a chat refuses to kill an editor it did not boot, or that another chat is still using.

  Actions:
    status      Print running editor PIDs + the current lock contents (JSON).
    boot-check  Decide what to do before launching. Prints one of:
                  ADOPT <pid> owner=<tok> build=fresh   -> editor up AND its build is newer than
                                                           the newest C++ source edit. DO NOT launch,
                                                           DO NOT rebuild. Just confirm MCPs + proceed.
                  ADOPT <pid> owner=<tok> build=stale   -> editor up but its DLL is OLDER than a C++
                                                           source edit. To get your changes you need a
                                                           close+rebuild — gate it on 'can-close' first
                                                           (if another chat is active, ASK the user).
                  ADOPT <pid> owner=<tok> build=unknown -> editor up, no editor DLL found to compare.
                  BOOT                                  -> none running, boot lock acquired under the
                                                           mutex. Caller launches, then calls
                                                           'register -EditorPid <pid>'.
                  WAIT owner=<tok>                      -> another chat is mid-boot. Caller waits ~20-30s
                                                           and re-runs boot-check (flips to ADOPT).
                  DUPLICATE <pid,pid>                   -> >1 editor already running (pre-existing mess).
                                                           Caller STOPS + warns the user.
    register    Record the launched editor PID. SELF-HEALS: if >1 editor now exists and one is the PID
                you just launched, it kills your own and adopts the survivor. Prints:
                  REGISTERED <pid> owner=<tok>          -> normal, single editor.
                  DEDUP-KILLED-SELF adopted=<pid>       -> a duplicate existed; killed your own launch,
                                                           adopted the survivor (you are now a co-user,
                                                           not the owner).
    heartbeat   Refresh this token's lastSeen (call when doing in-engine work).
    can-close   Decide whether this chat may kill the editor. Prints one of:
                  OK <pid,...>             -> this chat owns it, no other active users. Safe to kill.
                  BLOCKED not-owner ...    -> another chat booted it. DO NOT kill; ask the user.
                  BLOCKED other-users ...  -> another chat is active. DO NOT kill; ask the user.
                  NO-EDITOR                -> nothing to close.
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
  [int]$StaleMinutes = 15,
  [string]$DllPath = 'C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Binaries\Win64\UnrealEditor-Extraction.dll',
  [string]$SourceRoot = 'C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Source'
)

$ErrorActionPreference = 'Stop'
$LockPath = 'C:\Users\matth\Documents\Github\ProjectExtract\.claude\engine-session.lock.json'
$MutexName = 'Global\ProjectExtractEngineGuard'
$BootFreshMinutes = 3   # a 'booting' lock older than this is treated as a dead boot and reclaimed

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
function Test-Fresh($iso, $minutes) {
  if (-not $iso) { return $false }
  try { return ([datetime]$iso) -gt (Get-Date).ToUniversalTime().AddMinutes(-$minutes) } catch { return $false }
}
# 'fresh'  = editor DLL is at least as new as the newest C++ source edit (contains current changes)
# 'stale'  = a .h/.cpp/.cs is newer than the DLL (editor missing latest C++ → needs rebuild)
# 'unknown'= no DLL to compare against
function Get-BuildState {
  if (-not (Test-Path $DllPath)) { return 'unknown' }
  $dll = (Get-Item $DllPath).LastWriteTimeUtc
  $newest = Get-ChildItem -Path $SourceRoot -Recurse -File -Include *.h,*.cpp,*.cs -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
  if (-not $newest) { return 'fresh' }
  if ($dll -ge $newest.LastWriteTimeUtc) { return 'fresh' } else { return 'stale' }
}

# status is read-only — no mutex.
if ($Action -eq 'status') {
  $pids = Get-EditorPids
  [PSCustomObject]@{ runningPids = $pids; buildState = (Get-BuildState); lock = (Read-Lock) } | ConvertTo-Json -Depth 6
  return
}

# All mutating actions run inside a named system mutex so two chats finishing at the same
# instant are strictly ordered — this is what makes the singleton guarantee hold.
$mutex = New-Object System.Threading.Mutex($false, $MutexName)
$haveMutex = $false
try {
  try { $haveMutex = $mutex.WaitOne([TimeSpan]::FromSeconds(30)) }
  catch [System.Threading.AbandonedMutexException] { $haveMutex = $true }  # prior holder died; we own it now
  if (-not $haveMutex) { Write-Output "ERROR mutex-timeout"; return }

  switch ($Action) {

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
        Write-Output "ADOPT $($pids[0]) owner=$($ht.owner) build=$(Get-BuildState)"; break
      }
      # No editor running. Is another chat genuinely mid-boot?
      $lock = To-Ht (Read-Lock)
      if ($lock.booting -and (Test-Fresh $lock.bootedAt $BootFreshMinutes)) {
        Write-Output "WAIT owner=$($lock.owner)"; break
      }
      # Nothing running, no fresh boot in progress → claim the boot lock (mutex makes this safe).
      $ht = @{ pid = 0; owner = $Token; bootedAt = (NowIso); booting = $true; users = @(@{ token = $Token; lastSeen = (NowIso) }) }
      Write-Lock $ht
      if ($lock.bootedAt) { Write-Output "BOOT (reclaimed stale lock)" } else { Write-Output "BOOT" }
      break
    }

    'register' {
      $pids = Get-EditorPids
      if ($pids.Count -gt 1 -and $EditorPid -gt 0 -and ($pids -contains $EditorPid)) {
        # A duplicate slipped through. Kill MY own just-launched editor (safe — seconds old, no chat
        # is attached to it yet) and adopt the survivor.
        $survivors = @($pids | Where-Object { $_ -ne $EditorPid })
        try { Stop-Process -Id $EditorPid -Force -ErrorAction SilentlyContinue } catch {}
        $lock = To-Ht (Read-Lock)
        # Prefer the survivor already recorded in the lock; else the lowest PID.
        $chosen = $survivors[0]
        if ($lock.pid -and ($survivors -contains $lock.pid)) { $chosen = $lock.pid }
        $ht = $lock
        $ht.pid = $chosen
        $ht.booting = $false
        if (-not $ht.owner -or $ht.owner -eq $Token) { $ht.owner = '(pre-existing)' }  # I'm no longer the owner
        $ht = Touch-User $ht $Token
        Write-Lock $ht
        Write-Output "DEDUP-KILLED-SELF adopted=$chosen owner=$($ht.owner)"; break
      }
      $ht = To-Ht (Read-Lock)
      $ht.pid = $EditorPid
      if ($Token) { $ht.owner = $Token }
      $ht.bootedAt = (NowIso)
      $ht.booting = $false
      $ht = Touch-User $ht $Token
      Write-Lock $ht
      Write-Output "REGISTERED $EditorPid owner=$($ht.owner)"; break
    }

    'heartbeat' {
      $ht = Touch-User (To-Ht (Read-Lock)) $Token
      Write-Lock $ht
      Write-Output "OK"; break
    }

    'can-close' {
      $pids = Get-EditorPids
      if ($pids.Count -eq 0) { Write-Output "NO-EDITOR"; break }
      $lock = To-Ht (Read-Lock)
      if ($lock.owner -and $lock.owner -ne $Token -and $lock.owner -ne '(pre-existing)') {
        Write-Output "BLOCKED not-owner owner=$($lock.owner) pids=$($pids -join ',')"; break
      }
      $others = @()
      foreach ($u in $lock.users) {
        if ($u.token -and $u.token -ne $Token) {
          if (Test-Fresh $u.lastSeen $StaleMinutes) { $others += $u.token }
        }
      }
      if ($others.Count -gt 0) { Write-Output "BLOCKED other-users=$($others -join ',') pids=$($pids -join ',')"; break }
      Write-Output "OK $($pids -join ',')"; break
    }

    'release' {
      if (Test-Path $LockPath) { Remove-Item $LockPath -Force }
      Write-Output "RELEASED"; break
    }
  }
}
finally {
  if ($haveMutex) { $mutex.ReleaseMutex() }
  $mutex.Dispose()
}
