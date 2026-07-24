# Performance Benchmark Contract

## Reference target and gates

The provisional target is 1920x1080 at 60 FPS, High preset, 100% screen percentage, DX12, VSync off, uncapped, and dynamic resolution off on the reference development PC.

| Metric | Gate |
| --- | --- |
| Frame p95 | <= 16.67 ms |
| Frame p99 | <= 20.00 ms |
| Game / Draw / GPU p95 | <= 14.00 ms |
| Post-warm-up hitch | No frame above 50 ms |

Only a Development Standalone or packaged Development build is authoritative. Editor viewport, PIE, unfocused-window, stat-overlay, `ProfileGPU`, memory-trace, memreport, and legacy stat-file runs are diagnostic only and cannot pass or fail these timing gates. The user owns all gameplay and visual acceptance.

## Required run metadata

Record this once per benchmark session and repeat it whenever the executable, commit, configuration, machine, driver, or RHI changes.

| Field | Required value |
| --- | --- |
| Build | `Development Standalone` or packaged `Development`; executable path; executable timestamp; build identifier |
| Source | Full commit from `git rev-parse HEAD`; clean/dirty state from `git status --short` |
| CPU / RAM | CPU model and installed RAM |
| GPU / driver | GPU model and driver version |
| RHI | `D3D12`, confirmed in the startup log |
| Display | `1920x1080`, fullscreen, High, 100% screen percentage |
| Controls | `-TraceAutoStart=0`, `r.VSync=0`, `rhi.SyncInterval=0`, `t.MaxFPS=0`, `r.DynamicRes.OperationMode=0` |

Use these PowerShell commands from the repository root to collect the machine fields:

```powershell
Get-CimInstance Win32_Processor | Select-Object Name
Get-CimInstance Win32_ComputerSystem | Select-Object TotalPhysicalMemory
Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, AdapterRAM
git rev-parse HEAD
git status --short
```

## Fixed display and quality setup

1. Launch Standalone or the packaged Development executable with `-dx12 -ResX=1920 -ResY=1080 -Fullscreen -TraceAutoStart=0` and without any `-trace`, `-tracefile`, `-LLM`, or stat auto-start argument.
2. Open the game console before scenario setup and run `r.SetRes 1920x1080f`.
3. Run `sg.ViewDistanceQuality 2`.
4. Run `sg.AntiAliasingQuality 2`.
5. Run `sg.ShadowQuality 2`.
6. Run `sg.GlobalIlluminationQuality 2`.
7. Run `sg.ReflectionQuality 2`.
8. Run `sg.PostProcessQuality 2`.
9. Run `sg.TextureQuality 2`.
10. Run `sg.EffectsQuality 2`.
11. Run `sg.FoliageQuality 2`.
12. Run `sg.ShadingQuality 2`.
13. Run `sg.ResolutionQuality 100`.
14. Run `r.ScreenPercentage 100`.
15. Run `r.VSync 0`.
16. Run `rhi.SyncInterval 0`.
17. Run `t.MaxFPS 0`.
18. Run `r.DynamicRes.OperationMode 0`.
19. Run `stat none`.
20. Confirm the startup log reports D3D12 and record the final settings in the run metadata.

Any capture with a different resolution, quality group, screen percentage, RHI, VSync, cap, or dynamic-resolution state is invalid rather than comparable.

## Verified DemoMap anchors

The active benchmark map is `/Game/UWC_Modular_Skyscraper/Maps/DemoMap.DemoMap`. These anchors were verified from the live editor without changing the map:

| Scenario | Stable anchor |
| --- | --- |
| S01 | `PersistentLevel.PlayerStart_1` at `(-2970.45,-1321.19,14662)`, rotation `(0,0,0)`; companion `PersistentLevel.BP_Companion_C_1` at `(-2781.72,-1372.95,14651.96)`. |
| S02 | `TP_Checkpoint_FIND_OFFICE_KEYCARD` at `(-2634,-4000,14124)`, then `IP3_AO_Meeting_Table` → `IP3_AO_Utility_Copier` → `IP3_AO_WS1_Table` → `IP3_AO_WS2_Table` → `IP3_AO_WS3_Table`. |
| S03 | `TP_Checkpoint_REACH_EXTRACTION_TARGET` at `(-700,-2792,13718)` within director scope tag `Room2Defence`. |
| S04 | Default `PlayerStart_1`; the exact camera pose is stored as a `BugItGo` command before `R01` and reused for all three states. |

For S02 and S04, run `EnableCheats` in the Development build, set the exact player/camera pose, then run `BugIt <AnchorName>`. Copy the generated `BugItGo X Y Z Pitch Yaw Roll` command and reference screenshot into the results record. Replay that exact `BugItGo` command before every repeat. A run without the stored command and matching screenshot is invalid.

The verified S02 landmark locations are `IP3_AO_Meeting_Table` `(-3550,-2700,14030)`, `IP3_AO_Utility_Copier` `(-3520,-1600,14030)`, `IP3_AO_WS1_Table` `(-3300,-693.5,14030)`, `IP3_AO_WS2_Table` `(-2100,-643.5,14030)`, and `IP3_AO_WS3_Table` `(-900,-1243.5,14030)`.

S03 is blocked until a deterministic benchmark fixture exists. DemoMap currently has 14 placed enemies total and four inside the Room2 scope. Its active Objective configuration caps living enemies at 12, the punishment configuration caps them at 16, and the base Extraction value of 20 is only a cap rather than a guaranteed population. Do not record or compare S03 until one fixture disables adaptive variance, resets the roster, and produces exactly 20 living enemies plus the existing living companion at fixed Room2 transforms.

## Authoritative 60-second Insights capture

Use `YYYYMMDD_HHMM_<Build>_<Scenario>_R01` through `R03`; include the S04 state, for example `20260723_1430_Dev_S04_SniperADS_R02`.

1. Reset the scenario exactly as specified below and move to its recorded anchor.
2. Confirm every required setup condition and abort condition before recording.
3. Warm up for 15 seconds without any capture, overlay, console, menu, or focus change.
4. Open the console outside the gate, run `stat none`, then run `Trace.File <CaptureName>.utrace cpu,gpu,frame,bookmark`.
5. Run `SetBind F9 "Trace.Bookmark <CaptureName>_GATE_START"`.
6. Run `SetBind F10 "Trace.Bookmark <CaptureName>_GATE_END"`.
7. Close the console and hold the required state for 5 seconds so the console transition is outside the gate.
8. Start the fixed 60-second action and press `F9` on its first frame; do not open the console.
9. At exactly 60 seconds press `F10`, end the fixed action, and hold the final state for 5 seconds without opening the console.
10. Open the console after that settling pad, run `Trace.Stop`, then end or reset the scenario.
11. Repeat from a full scenario reset for `R02` and `R03`.

Analyze only frames between the `_GATE_START` and `_GATE_END` bookmarks. The bound F9/F10 input frames are deliberately inside the gate and must be identical across repeats; console transitions, trace control, warm-up, setup, reset, and teardown are outside it. The timing trace is the sole pass/fail source and contains only `cpu,gpu,frame,bookmark`; it excludes CSV, network, memory, file, load-time, stat overlays, and other diagnostic channels.

## Separate CSV replay

CSV is a corroborating artifact, not a source for the percentile or hitch gates. Capture it in a separate replay so CSV overhead cannot affect the authoritative Insights result.

1. Fully reset the scenario, restore its exact fixture, and repeat the 15-second warm-up.
2. Open the console outside capture and run `SetBind F9 "csvprofile start"`.
3. Run `SetBind F10 "csvprofile stop"`, close the console, and hold the required state for 5 seconds.
4. Press `F9` and begin the same fixed action on that frame without opening the console.
5. At exactly 60 seconds press `F10`, end the fixed action, and wait for the CSV file to finish writing.
6. Immediately discover and rename the autogenerated CSV before another capture.
7. Discard the first and last CSV rows containing the F9/F10 command boundary. Use the remaining rows only as diagnostic corroboration; never use them to pass or fail a gate.

Use the exact capture name with `_CSV` appended and these PowerShell commands from the repository root:

```powershell
$CaptureName = '<CaptureName>_CSV'
$Csv = Get-ChildItem -LiteralPath '.\Extraction\Saved\Profiling' -Recurse -File -Filter '*.csv' |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
$Csv | Select-Object FullName, LastWriteTimeUtc
Rename-Item -LiteralPath $Csv.FullName -NewName "$CaptureName.csv"
```

Abort the rename if the displayed timestamp is not from the run just completed. Record the `.utrace` and renamed CSV paths immediately.

## Scenario fixtures

Before the first authoritative run, define each anchor without inventing or relying on an undocumented actor name. Record the map, player location and rotation, camera aim point, a reference screenshot, loadout, relevant world state, and the exact action timeline. Do not move an anchor or alter the fixture between repeats.

### S01_QuietStart

- Anchor: fresh DemoMap default spawn at `PlayerStart_1`, location `(-2970.45,-1321.19,14662)`, rotation `(0,0,0)`.
- Setup: fresh Standalone load with no checkpoint restore; companion starts at `BP_Companion_C_1`, location `(-2781.72,-1372.95,14651.96)`; confirm no combat or alert state before the gate.
- Action: remain stationary and provide no movement, aim, fire, interaction, or menu input for 60 seconds.
- Reset: fully exit/reload DemoMap Standalone and use the default spawn before every repeat.
- Abort: any combat/alert, player or camera movement, actor-count/setup mismatch, focus loss, menu, overlay, streaming/setup transition, or unplanned input during the gate.

### S02_DenseOffice

- Anchor: saved `BugItGo` pose captured at `TP_Checkpoint_FIND_OFFICE_KEYCARD`, location `(-2634,-4000,14124)`.
- Setup: fresh DemoMap load; replay the saved `BugItGo`; reproduce the recorded loadout, companion mode, doors, enemies, and world state; no unplanned combat.
- Action: traverse `Meeting_Table` → `Utility_Copier` → `WS1_Table` → `WS2_Table` → `WS3_Table` in that order. Before `R01`, record each landmark arrival time, movement speed, turn, and pause; repeat the same 60-second timeline.
- Reset: reload DemoMap and replay the exact saved `BugItGo`; do not rely on checkpoint restore until its runtime behavior has been validated.
- Abort: route deviation, collision stall, wrong movement mode, early/late endpoint, combat/setup mismatch, focus loss, menu, overlay, or unplanned input during the gate.

### S03_Combat20Companion

- Status: `BLOCKED` until the deterministic 20-enemy fixture described above exists; no current DemoMap state is valid for S03.
- Anchor: `TP_Checkpoint_REACH_EXTRACTION_TARGET`, location `(-700,-2792,13718)`, inside `EnemyDirectorScope_Room2`.
- Spawn area: fixed Room2 zones West `(-2250,-2500,13700)`, North `(-3114.45,-949.32,13700)`, and East `(-600,-2600,13700)`.
- Setup: after the fixture exists, start from a full reset with the fixed loadout, archetype mix, spawn transforms, awareness state, companion state, and world state. Save authoritative evidence of exactly `20` living enemies and one living companion immediately before every gate.
- Action: initiate combat at the recorded cue, hold the recorded engagement area, and repeat the same 60-second fire/reload/movement timeline.
- Reset: invoke the future fixture's full reset and verify its roster evidence; do not reuse the post-combat world from the previous repeat.
- Abort: S03 is invalid unless exactly `20` living enemies plus the living companion is confirmed immediately before the gate. Also abort on roster/archetype/setup mismatch, companion downed before the gate, premature combat, action-timeline deviation, focus loss, menu, overlay, or unplanned input.

### S04_SniperAB

- Anchor: one saved `BugItGo` pose and reference screenshot captured at default `PlayerStart_1`; replay it for all states and repeats.
- Setup: fresh DemoMap load; no combat; primary slot `BP_Sniper` / `DA_Sniper`, secondary slot `BP_Pistol` / `DA_Pistol`, and one fixed attachment state shared by every run. `Unequipped` means the secondary pistol is held while the primary sniper actor remains instantiated in its slot but hidden/inactive; `Hip` means the primary sniper is held with ADS released; `ADS` means the primary sniper is held with ADS continuously pressed. Settle the selected state for 5 seconds before capture.
- Action: remain stationary, hold the selected state, and provide no fire, reload, movement, camera, interaction, or menu input for 60 seconds.
- Reset: reload DemoMap and replay the exact saved `BugItGo` before every state/repeat; never transition between S04 states inside a gated window.
- Abort: any transform, aim, scene-content, loadout, animation, combat, focus, menu, overlay, or selected-state mismatch.

Capture `R01` through `R03` for each scenario. S04 therefore requires three runs each for `Unequipped`, `Hip`, and `ADS`.

## Non-gated diagnostics

Run diagnostics only after the clean timing captures, with a separately named `_Diag` artifact. None of these artifacts may be used to pass or fail the 60-second gates.

1. Keep the separate CSV replay above non-gated. Run `stat unit`, `stat game`, `stat AI`, `stat EQS`, `stat rhi`, `stat scenerendering`, and `stat streaming` in another diagnostic replay outside every gated timing window.
2. Run `ProfileGPU` only in a separate diagnostic replay after warm-up; save the result outside every gated timing window.
3. If a legacy stat file is needed, run `stat startfile`, reproduce the diagnostic segment, then run `stat stopfile`; never overlap it with an authoritative trace/CSV capture.
4. For dedicated memory diagnostics, launch a separate process with the normal DX12/display arguments plus `-trace=default,memory -LLM -tracefile="<ProjectRoot>/Extraction/Saved/Profiling/<CaptureName>_DiagMemory.utrace"`. This launch is explicitly non-gated and its timing numbers are invalid for the performance gates.
5. In the dedicated memory run, reproduce the scenario outside a gated window, run `memreport -full`, then stop the memory trace/process.
6. Immediately discover and rename the autogenerated memreport before another memory run.

Use these PowerShell commands from the repository root immediately after `memreport -full`:

```powershell
$CaptureName = '<CaptureName>_DiagMemory'
$MemReport = Get-ChildItem -LiteralPath '.\Extraction\Saved\Profiling' -Recurse -File -Filter '*.memreport' |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
$MemReport | Select-Object FullName, LastWriteTimeUtc
Rename-Item -LiteralPath $MemReport.FullName -NewName "$CaptureName.memreport"
```

Abort the rename if the displayed timestamp is not from the memory run just completed. Record memory-trace, memreport, stat, and `ProfileGPU` artifact paths as diagnostics, not authoritative timing evidence.

## Results

| Capture | Frame p50/p95/p99 ms | Game p50/p95/p99 ms | Draw p50/p95/p99 ms | GPU p50/p95/p99 ms | Hitches >50 ms | Gate |
| --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |

| Diagnostic artifact (including CSV) | Draw calls | Primitives | AI ms | Trace count | VRAM/pool pressure | Source path |
| --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |

Record source artifact paths beside both tables. Investigate each p95/p99 outlier before changing runtime behaviour; all optimizations remain measurement-gated.
