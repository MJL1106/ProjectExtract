---
name: boot-engine
description: Launch the Unreal Editor for ProjectExtract from the CLI and bring the in-engine MCP servers (VibeUE :8088 via the :8089 proxy, UnrealClaude :3000) live, so you can go straight into VibeUE / UnrealClaude tooling. Use whenever the user says "load up the engine", "boot the engine/editor", "start Unreal", "launch the editor", "open the project", or any time in-editor MCP work is needed and the editor is not already running.
---

# Boot the Unreal Editor (ProjectExtract)

Launch the editor + the VibeUE proxy yourself, wait for boot, confirm the MCP servers respond, then proceed with in-engine work. Do **not** hand the user a "go open the editor" instruction — do it.

## Paths (this machine)
- Editor exe: `C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe`
- Project: `C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Extraction.uproject`
- VibeUE proxy launcher: `C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Plugins\VibeUE\Content\Python\start-vibeue-proxy.bat`

## What provides what
- **The editor process** exposes the in-editor servers: VibeUE on `:8088`, UnrealClaude on `:3000`.
- **`.mcp.json` points VibeUE at `:8089`** (the `vibeue-proxy.py` forwarder → `:8088`), so the proxy **must** be running too. The proxy bat is idempotent (kills any existing :8089 listener, restarts).
- **UnrealClaude** is a stdio node bridge spawned by Claude Code that connects to `:3000` — no separate process; it works once the editor is up.
- Per `agent_docs/UnrealWorkflow.md` §1.7, the VibeUE MCP **auto-reconnects** once the in-editor server is back — you do not relaunch Claude Code.

## Procedure

1. **Check it isn't already up.** If this project's `UnrealEditor` is running and `vibeue_status` already responds, skip to step 5. Filter by the project's `.uproject` so another project's editor isn't mistaken for this one:
   ```powershell
   Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -like '*Extraction.uproject*' } | Select-Object ProcessId, CommandLine
   ```

2. **Pre-flight: no pending full C++ build.** A from-scratch / new-module build needs the editor CLOSED (Live Coding can't add modules; `Build.bat` fails fast, exit 6). If C++ changed and hasn't been built, run the build (editor closed) BEFORE booting. Don't boot on top of unbuilt module changes.

3. **Launch the editor detached** (returns immediately; editor keeps running):
   ```powershell
   Start-Process "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Extraction.uproject"'
   ```
   On a prior crash, add `-ddc=InstalledNoZenLocalFallback` and first kill **only this project's** frozen `UnrealEditor` / `CrashReportClientEditor` (match the `.uproject` in the command line — see the project-scoped kill in Gotchas; never blanket-kill all editors) (§1.7).

4. **Start the VibeUE proxy** (idempotent):
   ```powershell
   & "C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Plugins\VibeUE\Content\Python\start-vibeue-proxy.bat"
   ```

5. **Wait for boot, then confirm.** Cold boot is ~60–120 s (longer after a shader/DDC rebuild). Don't poll tighter than ~20–30 s — booting is slow and each poll wakes you. The editor is up once its process memory climbs past ~1.8 GB. Confirm the servers respond with the MCP status tools (not a port scrape):
   - VibeUE: `vibeue_status`
   - UnrealClaude: `unreal_status`
   Retry the status calls a few times with ~20 s gaps; they start succeeding once the in-editor servers finish init and (for VibeUE) the proxy is forwarding.

6. **You're live.** Proceed with the in-engine task. Before the first edit in a VibeUE domain (blueprints/materials/UMG/etc.), run `manage_skills(action="list")` once, then load the matching skill — see `agent_docs/UnrealWorkflow.md` (the tooling map + gotchas). Note VibeUE needs its API key set in-editor on a fresh install.

## Gotchas
- **Don't `Start-Sleep` in the foreground** waiting for boot — use a background wait / Monitor loop or just schedule a re-check, so you don't block the session for two minutes.
- **VibeUE silent if the proxy is down:** if `unreal_status` works but `vibeue_status` doesn't, the editor is up but the `:8089` proxy isn't — re-run the proxy bat (step 4).
- **First in-engine call may lag** a few seconds after the editor reports ready while the MCP server finishes binding — one retry, not a failure.
- **Closing for a build — PROJECT-SCOPED, never blanket-kill:** when a full C++ rebuild is needed, close **only this project's** editor. ⚠️ **NEVER `Stop-Process -Name UnrealEditor`** — the user routinely has more than one project's editor open at once, and killing by name terminates *all* of them (losing unsaved work in unrelated projects). Identify this project's instance by its command line (the `.uproject` path) and kill only that PID:
  ```powershell
  Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" |
    Where-Object { $_.CommandLine -like '*Extraction.uproject*' } |
    ForEach-Object { Write-Output "Killing PID $($_.ProcessId)"; Stop-Process -Id $_.ProcessId -Force }
  ```
  Same rule for `CrashReportClientEditor` — only kill one tied to this project; don't blanket-kill. (Graceful alternative when PIE is stopped: `unreal.SystemLibrary.quit_editor()` via VibeUE, or close the window.) Then build, then re-boot with this skill.
