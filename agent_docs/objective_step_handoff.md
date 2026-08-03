# Handoff — data-driven objectives + attachment camera fix

Written 2026-07-27, end of session, picking up on a different machine.

## READ THIS FIRST — the build is unverified

`ObjectiveStep.cpp` / `.h` were last edited by an implementer agent that **died mid-edit on an API
error**, leaving the header declaring `Activate()` (no args) while the .cpp still defined
`Activate(bool)`. I patched that forward by hand: `Activate()` now forwards to
`ActivateInternal(bool)`, the body moved to `ActivateInternal`, and the one internal call site
(`ApplySideEffect`) calls `ActivateInternal(bResumeReplay)`.

**That patch has never been compiled.** The editor was open (another chat was using it) so
`ensure-fresh-build.ps1` would have refused. The last *verified* green build was before round 4.

**First thing to do on the new machine: build.** If it fails, it is almost certainly in
`AObjectiveStep::Activate` / `ActivateInternal` or their call sites, and it is mine, not a
pre-existing problem.

```
.claude/skills/boot-engine/ensure-fresh-build.ps1
```

## What the objective system is

`AObjectiveStep` replaces `ALevelObjectiveFlow`. The old flow's 13 steps live in two C++ enums plus
a transition table, so adding one beat touches ~7 sites in an 1154-line file. The new one is a
placed actor per beat, chained by a `NextStep` pointer — a designer adds an objective by placing an
actor, picking a condition from a dropdown, wiring one or two level actors, and pointing `NextStep`
at the next beat. No C++ per beat.

- **Conditions:** ReachLocation, AcquireKeycard, EnemiesDead, DoorOpened, ContainerLooted,
  RouteCompleted, Interacted, ExtracteeRescued, WaveCompleted, Manual.
- **Side effects** (`OnActivate` / `OnComplete`): SetMissionPhase, StartDirectorWave, UnlockGate,
  SetCompanionMode, CompleteLevel, ActivateActor, SetExtracteeRescuable, CommandCompanionRoute.
- **Checkpoints:** mark a step, wire a spawn point. Resume walks the chain from the head and
  replays each earlier beat's world-state, derived from that beat's own condition — nothing extra
  to author.
- **Markers:** explicit `MarkerTarget` → derived anchors (enemy-group centroid, nearest unlooted
  crate) → the condition's payload actor → the step's own location. `bFreezeMarkerAtActivation`
  pins a marker for hold-this-ground beats.

`ALevelObjectiveFlow` is deliberately untouched and still runs DemoMap.

### Files
Added: `Public/World/ObjectiveStep.h`, `Private/World/ObjectiveStep.cpp`,
`Public/World/ObjectiveChainWalker.h`, `Public/World/InteractionEventSubsystem.h`,
`Private/World/InteractionEventSubsystem.cpp`, `Private/Tests/ObjectiveStep.spec.cpp`.
Changed: `ObjectiveSubsystem` (AddObjective gained a defaulted `HeightAboveBase`),
`ExtractionGameInstance` (step-id checkpoint alongside the legacy enum one), `ExtractionPlayer`,
`LootContainer` (`MarkLootedForCheckpoint` + `OnRestoredLooted`), `LevelCompletionLiftGate`,
`ExtractionTargetActor`, `ExtracteeCompanion`, `ExtracteeCharacter`, `MissionInventorySubsystem`
(`RecordKeycard` gained `bSilent`).

### Review history
Three passes by `ue5-reviewer`. Round 1: 4 critical (fast-forward destroyed loot; non-idempotent
side effects replayed; sphere bound too late; `Interacted` fired on a *refused* interaction).
Round 2: 1 critical (replay filter leaked through `ActivateActor`). Round 3: 0 critical, 3 warnings,
verdict "ship-ready for the migration, conditionally". Round 4 fixed those 3 — it is the round whose
build is unverified.

## NEXT: the DemoMap migration (not started)

Spec is `agent_docs/objective_flow_migration_source.md` — the exact live wiring of
`LevelObjectiveFlow_DemoMap`, captured from the running editor. Reproduce it exactly.

Hard constraints, all in that doc but repeating the big one: **single swap.** Delete
`LevelObjectiveFlow_DemoMap` in the same change that places the step chain. A half-migrated level
has the two systems driving each other — the new fast-forward's `ForceOpenInstant()` broadcasts
`OnDoorOpened` into the legacy flow's `Advance()`, and it destroys tracked enemies, which the
legacy `IsEnemyGroupDead` can then never satisfy.

Also: author the extraction wave as an `OnActivate` effect on the beat that *watches* it, not
`OnComplete` on the rescue beat; delete the downstream `CompanionRouteTrigger`; the Defend beat
needs `bFreezeMarkerAtActivation` with `MarkerTarget` empty.

## Attachment camera bug — FIXED

Equipping an attachment zoomed the camera without ADS, and the zoom persisted until the next
attachment change.

Cause: in `BP_ExtractionCharacter`'s EventGraph, `SpawnAttachments.then` wired straight into a
`FOV` call with **no `IsAim` gate**, while every sibling FOV call in that graph has one. Its
`Target FOV` pin literal was `NewEnumerator1` = `FOVsight`, so every attachment application forced
a zoomed sight FOV regardless of aiming.

Fix applied and saved in-editor: inserted a `Branch` driven by `Get IsAim` between
`SpawnAttachments.then` (and the reroute node that also fed it) and the `FOV` call; `True` → FOV,
`False` unconnected. Verified by read-back — `FOV.execute` now has exactly one source.

**Not PIE-tested** (user deferred). If it recurs, the remaining unknown is why the kit's write
survived our per-tick `UpdateWeaponFOV` correction at all; the suspect there is
`ExtractionCharacter.cpp:229` sampling `BaseFOV` off the live camera at BeginPlay, which the kit's
equip path may already have moved. Do not chase that unless the symptom comes back.

## Also in this commit

- `agent_docs/objectives_director_loot_guide.html` — designer guide for objectives, the enemy
  director, spawn zones, compositions, waves, loot containers and the weapon case / attachment
  gates. Every number in it was read from live assets.
- Another chat's in-flight weapon-case work (`BP_WeaponCase_Pistol/SMG/Sniper`, the matching
  meshes, three new attachment pickups, `L_EnemyGym` edits) was already staged and is swept into
  this commit so it isn't stranded on the old machine. It is **not mine and not reviewed by me.**

## Two live findings not yet acted on

- **DemoMap has no phase triggers placed.** The director never leaves its default mission phase, so
  the Infiltration and Extraction composition lists never fire there — only `Objective` ones do,
  plus the defence wave which sets its own phase.
- **`DA_DirectorConfig` has two authoring slips.** `ObjectiveConfig` lists `Flush` twice, so its
  real weight is 1.0 not 0.5; and `ExtractionConfig`'s `ShotgunPush` contains three Grunts and no
  Shotgun — the second entry's class was left as Grunt.
- Minor: `DisplayName` is empty on all nine `DA_Attach_*` assets. Invisible now, blank rows once the
  loadout menu lands.
