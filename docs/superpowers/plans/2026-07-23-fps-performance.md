# ProjectExtract FPS Performance Implementation Plan

**Target:** Stable 60 FPS at 1080p, High preset, 100% screen percentage on the current development PC.

**Frame gates:** Frame p95 <= 16.67 ms, frame p99 <= 20 ms, Game/Draw/GPU p95 <= 14 ms, no post-warm-up frame above 50 ms.

## Global constraints

- Preserve the user-owned `DemoMap.umap` and concurrent source edits unless a task explicitly owns them.
- Capture a repeatable baseline before changing runtime behavior or rendering quality.
- Keep C++ asset-agnostic; never add hardcoded `/Game/...` paths.
- First environment pass is static opaque shell content only. Exclude doors, glass, elevators/gates, moving meshes, cover, and gameplay collision.
- User owns PIE and visual/gameplay acceptance.
- Each optimization is an isolated, reversible slice with before/after evidence.
- Stop once the target gate is met; do not trade visual quality for unused headroom.

## Task 1: Performance instrumentation and benchmark contract

- Add Unreal Insights CPU scopes to the identified AI, weapon, UI, audio, and director hotspots without changing behavior.
- Add a reproducible four-scenario benchmark guide and capture contract.
- Add the performance initiative to the live roadmap as in progress.
- Store exact player/camera poses with `BugIt` / `BugItGo` so quiet, office, and sniper captures can be repeated without modifying DemoMap.
- Add a deterministic benchmark-only 20-enemy fixture once the concurrent DemoMap work is clear; current edit-time inspection proves the adaptive Room2 setup cannot guarantee S03.
- Review the diff, build with `Result: Succeeded`, reboot the editor, then hand the baseline run to the user.

## Task 2: Sniper scope capture

- Capture the baseline scope GPU delta.
- Gate Blueprint tick and SceneCapture to equipped-and-ADS.
- Compare 60/45/30 Hz capture and 2048/1536/1024 targets.
- Ship only after zero idle capture cost and user-approved optic quality.

## Task 3: Static-world draw and lighting pressure

- Pilot the largest static opaque ceiling shell with ISM.
- Expand only after transform, collision, navigation, visual, Draw-thread, and GPU validation.
- Add safe cull distances and remove unnecessary render/navigation flags from visual-only content.
- Tune decorative local lights without touching gameplay-critical or transparent assets.

## Task 4: AI game-thread budget

- Cache last-visible enemy body points.
- Cache companion cover hunker geometry.
- Reuse and stagger companion candidate scans.
- Add EQS/cover budgets and stagger expensive evaluations.
- Cache director membership and time-slice squad spawning.

## Task 5: Continuous CPU and combat-burst cleanup

- Event-drive idle awareness and HUD widgets.
- Suspend stationary footstep polling and reuse gear-rattle audio.
- Prewarm decals and remove tracer, shell-audio, and disabled-debug churn.
- Spatially shortlist near-miss targets.

## Task 6: Lifetime, animation, and spawned-object budgets

- Remove or disable empty skeletal meshes.
- Apply safe visibility-based animation ticking and update-rate optimization.
- Bound corpse pickup actors/components.
- Preserve existing Niagara pooling and pool only proven high-frequency spawns.

## Task 7: Scalability and final gate

- Add explicit device/scalability profiles.
- Keep the reference capture native; offer TSR/dynamic resolution as settings.
- Add cook/residency budgets based on measured streaming data.
- Repeat the full benchmark and gameplay regression matrix.

