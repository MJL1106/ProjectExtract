---
name: boot-engine
description: Launch the Unreal Editor for ProjectExtract from the CLI and bring the in-engine MCP servers (VibeUE :8088 via the :8089 proxy, NeoStack :9315) live, so you can go straight into VibeUE / NeoStack tooling. Use whenever the user says "load up the engine", "boot the engine/editor", "start Unreal", "launch the editor", "open the project", or any time in-editor MCP work is needed and the editor is not already running.
---

# Boot the Unreal Editor (ProjectExtract)

Launch the editor + the VibeUE proxy yourself, wait for boot, confirm the MCP servers respond, then proceed with in-engine work. Do **not** hand the user a "go open the editor" instruction — do it.

## Paths (this machine)
- Editor exe: `C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe`
- Project: `C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Extraction.uproject`
- VibeUE proxy launcher: `C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Plugins\VibeUE\Content\Python\start-vibeue-proxy.bat`
- Skill helper scripts (this folder): `engine-guard.ps1` (boot/adopt/can-close coordination), `ensure-fresh-build.ps1` (build-if-stale before launch), `watch-restart.ps1` (listen for another chat's rebuild+reboot)

## What provides what
- **The editor process** exposes the in-editor servers: VibeUE on `:8088`, NeoStack on `:9315`.
- **`.codex/config.toml` points VibeUE at `:8089`** (the `vibeue-proxy.py` forwarder → `:8088`), so the proxy **must** be running too. `.mcp.json` may point directly at `:8088` for Claude. The proxy bat is idempotent (kills any existing :8089 listener, restarts).
- **NeoStack** is an HTTP MCP at `:9315` (the `unreal-editor` server, served by the in-editor NeoStackAI plugin) — no separate process; it works once the editor is up.
- Per `agent_docs/UnrealWorkflow.md` §1.7, the VibeUE MCP **auto-reconnects** once the in-editor server is back — you do not relaunch the agent session.

## Coordination — one editor, many chats (read this first)

Multiple Codex/Claude chats share this repo and have been **booting duplicate editors and killing each
other's engine**. To prevent that, every lifecycle decision (boot / adopt / close) goes through
**`engine-guard.ps1`** (in this skill's folder). It tracks ownership in a shared lock file
`.claude/engine-session.lock.json`: which chat booted the editor, when, and a per-chat heartbeat. Keep this lock path shared with Claude so both tools coordinate on one editor.

How it guarantees **exactly one editor**:
- A **named system mutex** serialises the boot decision, so two chats finishing at the same instant can't both `BOOT` — the first claims it, the second gets `WAIT` (then `ADOPT`).
- `register` **self-heals**: if a duplicate ever slips through, the chat that launched the extra one kills **its own** just-launched editor and adopts the survivor — you never *end* with two.
- `boot-check`'s `ADOPT` reports `build=fresh|stale`: since all chats share one working tree, a single build already contains every chat's edits, so a recently-booted editor that's `fresh` should be **adopted, not rebooted**. `stale` means a C++ source file is newer than the editor's DLL → you'd need a rebuild to see your change.

**Generate a session token ONCE and reuse it for every guard call this chat:**
```powershell
$tok = [guid]::NewGuid().ToString()   # keep this value for the whole session
```
If context was reset and you've lost `$tok`, that's fine: you will only ever ADOPT an existing
editor (never duplicate) and you must **never auto-close** one you can't prove you booted.

`& "$guard" ...` below is shorthand for:
`& "C:\Users\matth\Documents\Github\ProjectExtract\.agents\skills\boot-engine\engine-guard.ps1"`

## Procedure

1. **Decide boot vs adopt — never duplicate.** Ask the guard and branch on its output:
   ```powershell
   & "$guard" -Action boot-check -Token $tok
   ```
   - `ADOPT <pid> owner=… build=fresh` → an editor is already up and its build contains current C++. **Do NOT launch, do NOT rebuild.** Skip to step 5, confirm the MCPs respond, and proceed.
   - `ADOPT <pid> owner=… build=stale` → editor is up but a C++ source edit is newer than its DLL — **the running editor's base module (and any naive reboot) is missing that change.** To get it you need a close+rebuild: gate on `can-close` (if another chat is active, **ask the user**; don't yank it), close this project's editor, run `ensure-fresh-build.ps1` (step 2), then boot. `build=unknown` = no DLL to compare; treat as adopt-and-proceed unless you know you changed C++.
   - `BOOT` (or `BOOT (reclaimed stale lock)`) → nothing running and you now hold the boot lock. Continue to steps 2–4, then `register`.
   - `WAIT owner=…` → another chat is mid-boot right now. Wait ~20–30 s and re-run `boot-check`; it flips to `ADOPT` once their editor is up.
   - `DUPLICATE <pids>` → more than one editor is already running (a pre-existing mess). **Stop and tell the user** which PIDs — do not add a third; they decide which to close.

2. **Pre-flight: guarantee the on-disk build is current — ENFORCED (this is the fix for "the reboot lost my change").**
   A bare-`.uproject` launch loads ONLY the base `UnrealEditor-Extraction.dll`. Live Coding patches (`UnrealEditor-Extraction.patch_N.*`) are hot-loaded into a *running* editor and do **not** survive a restart — so anything that was only LC-patched, or saved-to-source-but-never-fully-built, is gone after a reboot. Don't trust that "a build happened"; force one when source is newer than the DLL. A real build is 30–300 s; use `Start-Job` if you need the session free:
   ```powershell
   & "C:\Users\matth\Documents\Github\ProjectExtract\.agents\skills\boot-engine\ensure-fresh-build.ps1"
   ```
   Branch on the single result line:
   - `BUILD-SKIPPED-FRESH` → DLL already contains current C++. Continue to step 3.
   - `BUILD-OK log=…` → rebuilt and `Result: Succeeded` confirmed in the log. Continue to step 3.
   - `BUILD-FAILED log=… exit=…` → **do NOT launch a stale editor.** Read the log; dispatch `ue5-build-specialist` (linker/IWYU/Build.cs) or `ue5-cpp-implementer` (semantic/template/API), fix, then re-run this step.
   - `BUILD-BLOCKED-EDITOR-OPEN pids=…` → an editor for this project is still up, so you're not actually on the BOOT path — re-run `boot-check` (step 1) and ADOPT it, or gate a close first.
   (The script verifies by grepping the log for `Result: Succeeded`, not the exit code — a Live Coding / build-mutex lock fast-fails to a no-op, per `pitfall_live_coding_blocks_build`.)

3. **Launch the editor detached** (returns immediately; editor keeps running), then record the PID so other chats know you own it:
   ```powershell
   $p = Start-Process "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Extraction.uproject"' -PassThru
   & "$guard" -Action register -Token $tok -EditorPid $p.Id
   ```
   `register` prints `REGISTERED <pid>` normally. If it prints `DEDUP-KILLED-SELF adopted=<pid>`, a duplicate had slipped through — the guard killed the editor you just launched and adopted the survivor; **don't relaunch**, just confirm the surviving editor's MCPs (step 5). You're now a co-user, not the owner, so you can't auto-close it.
   On a prior crash, add `-ddc=InstalledNoZenLocalFallback` and first kill **only this project's** frozen `UnrealEditor` / `CrashReportClientEditor` (match the `.uproject` in the command line — see the project-scoped kill in Gotchas; never blanket-kill all editors) (§1.7).

4. **Start the VibeUE proxy** (idempotent):
   ```powershell
   & "C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Plugins\VibeUE\Content\Python\start-vibeue-proxy.bat"
   ```

5. **Wait for boot, then confirm.** Cold boot is ~60–120 s (longer after a shader/DDC rebuild). Don't poll tighter than ~20–30 s — booting is slow and each poll wakes you. The editor is up once its process memory climbs past ~1.8 GB. Confirm the servers respond (not a port scrape):
   - VibeUE: `vibeue_status`
   - NeoStack: a trivial `mcp__unreal-editor__execute_script` (e.g. `return 1`)
   Retry the calls a few times with ~20 s gaps; they start succeeding once the in-editor servers finish init and (for VibeUE) the proxy is forwarding.

6. **You're live.** Proceed with the in-engine task. Before the first edit in a VibeUE domain (blueprints/materials/UMG/etc.), run `manage_skills(action="list")` once, then load the matching skill — see `agent_docs/UnrealWorkflow.md` (the tooling map + gotchas). Note VibeUE needs its API key set in-editor on a fresh install. Optionally refresh your heartbeat (`& "$guard" -Action heartbeat -Token $tok`) when you do in-engine work, so other chats see this editor is actively in use.

## Closing the editor — ALWAYS ask the user first (never auto-close, never stall, never yank another chat's editor)

A full C++ rebuild needs the editor closed. The user multitasks several chats on this **one shared editor**, so the rule is simple: **before any close you ALWAYS ask the user — even when you own it.** You never auto-close, and you never get stuck either (asking IS the action). One shared working tree means *any* chat's rebuild already contains *your* saved changes, so often the answer is "let another chat reboot it and just listen."

1. **Ask the guard who's on it** (this fills in the question, it does not decide for you):
   ```powershell
   & "$guard" -Action can-close -Token $tok
   ```
   - `NO-EDITOR` → nothing to close; skip this whole section.
   - `OK <pids>` → you booted it, no other chat is active.
   - `BLOCKED not-owner owner=…` / `BLOCKED other-users=… pids=…` → another chat is using it.

2. **ALWAYS ask the user before closing — use the available Codex user-question tool when present, otherwise ask a concise plain-text question:**
   - **Q (quote the guard line):** "Close & rebuild this project's editor to bake in C++ changes? Guard: `<can-close output>`."
   - **Option A — "Yes, rebuild now (no other chat working)"** → close + rebuild + reboot (step 3).
   - **Option B — "No — another chat's using it; wait & listen for its restart"** → don't close; start the watcher (step 4).
   - **Other** → the user types an explicit instruction (e.g. "nothing else is live, close it" = A; "leave it, I'll reboot shortly" = B).
   Order the **guard-appropriate option first (it's the recommended one):** `BLOCKED` → put B first; `OK` → put A first. Always surface `owner=`/`pids=` so the user knows which chat. When the user gives no steer and the guard is `BLOCKED`, bias to B — they're usually multitasking.

3. **Option A — close (PROJECT-SCOPED), rebuild, reboot:**
   ```powershell
   Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" |
     Where-Object { $_.CommandLine -like '*Extraction.uproject*' } |
     ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
   & "$guard" -Action release -Token $tok
   ```
   Then run `ensure-fresh-build.ps1` (step 2 of the boot Procedure — wait for `BUILD-OK`), and on green re-boot via this skill. ⚠️ Only ever kill THIS project's editor (the `.uproject` filter above) — **never `Stop-Process -Name UnrealEditor`** (see Gotchas).

4. **Option B — listen for the restart instead of closing:** another chat (or the user) rebuilds+reboots; that build includes your saved changes too. Launch the watcher with a PowerShell job if the session must stay free — it wakes you the moment the editor is back on a build that contains your code:
   ```powershell
   $job = Start-Job -FilePath "C:\Users\matth\Documents\Github\ProjectExtract\.agents\skills\boot-engine\watch-restart.ps1"
   Receive-Job $job -Wait
   ```
   - `RESTARTED pid=… dll=…` → editor rebooted on a fresh build that includes your changes. Confirm the MCPs respond and continue — **nothing to build or close.**
   - `RESTART-TIMEOUT waited=…m` → no restart yet. Re-ask the user (if nothing else is actually live, fall to Option A and close it yourself).
   Tip: tell the user (and any other chat) *what* your change is, so the chat doing the rebuild knows your edit is riding along — it always is, as long as your `.cpp/.h` is saved before their build starts.

## Are my changes in the build? (quick check — don't re-derive this each time)

All chats share one working tree, so any full build compiles every chat's **saved** source. To know whether your edits are in:
- **On disk / what the next boot will load:** `& "$guard" -Action status` → `buildState: fresh` = the base DLL is ≥ the newest source edit (yours included) → your changes ARE in the build. `stale` = a source file is newer than the DLL → not in yet, a rebuild is needed.
- **In the editor running *right now*:** `fresh` alone isn't proof — the live process must have (re)started *after* that build (start time > DLL mtime). `watch-restart.ps1` checks exactly this; use it rather than assuming a `fresh` on-disk DLL means the open editor is running it.
- Save your `.cpp/.h` before checking — `fresh` only accounts for source already on disk (the Edit tool writes immediately, so a completed edit is saved).

## Gotchas
- **Don't `Start-Sleep` in the foreground** waiting for boot — use a background wait / Monitor loop or just schedule a re-check, so you don't block the session for two minutes.
- **VibeUE silent if the proxy is down:** if NeoStack (`mcp__unreal-editor__execute_script`) works but `vibeue_status` doesn't, the editor is up but the `:8089` proxy isn't — re-run the proxy bat (step 4).
- **First in-engine call may lag** a few seconds after the editor reports ready while the MCP server finishes binding — one retry, not a failure.
- **Closing for a build — gate on `can-close`, then PROJECT-SCOPED, never blanket-kill:** when a full C++ rebuild is needed, FIRST run `can-close` (see "Closing the editor" above) — even a project-scoped kill will take down a *second chat's* editor for the SAME project, so the `.uproject` filter alone is not enough. Only on `OK` do you close, and close **only this project's** editor. ⚠️ **NEVER `Stop-Process -Name UnrealEditor`** — the user routinely has more than one project's editor open at once, and killing by name terminates *all* of them (losing unsaved work in unrelated projects). Identify this project's instance by its command line (the `.uproject` path) and kill only that PID:
  ```powershell
  Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" |
    Where-Object { $_.CommandLine -like '*Extraction.uproject*' } |
    ForEach-Object { Write-Output "Killing PID $($_.ProcessId)"; Stop-Process -Id $_.ProcessId -Force }
  ```
  Same rule for `CrashReportClientEditor` — only kill one tied to this project; don't blanket-kill. (Graceful alternative when PIE is stopped: `unreal.SystemLibrary.quit_editor()` via VibeUE, or close the window.) Then build, then re-boot with this skill.
