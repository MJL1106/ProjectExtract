# Objective Flow, Stims, and Room 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the approved objective sequence, optional supply-room objective, slot-3 health stims, and two authored Room 2 double-takedown encounters.

**Architecture:** A replicated player consumable component owns stim state and consumption; the existing mission inventory remains the loot grant gateway. A level-specific finite-state objective-flow actor owns the singular primary objective and one optional supply objective, advancing through explicit door, enemy-death, loot, extraction-wave, and lift events. Existing level actors are wired in DemoMap after reviewed C++ is built.

**Tech Stack:** Unreal Engine 5.7, C++, Enhanced Input, UMG, replicated ActorComponents, NeoStack/VibeUE editor automation, Unreal Automation Tests.

## Global Constraints

- Stims restore exactly 50 health, restore no shield, and stack to exactly 3.
- Slot 3 uses a stim and replaces the marketplace melee-slot action.
- Full-health use consumes nothing; dead or DBNO players cannot use a stim.
- Loot remains Middle Mouse ping followed by I; companion loot sweep remains unchanged.
- The optional supply objective never gates the primary flow.
- Only the nearest unlooted supply crate receives the optional world marker.
- No hardcoded `/Game/...` asset paths in C++.
- New source subfolders must be registered in the module include arrays.
- Replicated properties require both a replicated UPROPERTY specifier and `DOREPLIFETIME[_CONDITION]`.
- Timers must be cleared in `EndPlay` or `BeginDestroy`.

---

### Task 1: Replicated stim inventory and loot grant

**Files:**
- Create: `Extraction/Source/Extraction/Public/Components/ConsumableInventoryComponent.h`
- Create: `Extraction/Source/Extraction/Private/Components/ConsumableInventoryComponent.cpp`
- Modify: `Extraction/Source/Extraction/Public/World/LootTypes.h`
- Modify: `Extraction/Source/Extraction/Public/Game/MissionInventorySubsystem.h`
- Modify: `Extraction/Source/Extraction/Private/Game/MissionInventorySubsystem.cpp`
- Modify: `Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h`
- Modify: `Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp`
- Test: `Extraction/Source/Extraction/Private/Tests/ConsumableInventory.spec.cpp`

**Interfaces:**
- Produces: `UConsumableInventoryComponent::AddStims(int32)`, `TryUseStim()`, `GetStimCount()`, `OnStimCountChanged`, and server RPC `ServerTryUseStim()`.
- Extends: `ELootType::Stim` and `FLootGrant::StimCount`.
- Consumes: `UHealthComponent::Heal(float)` and `AExtractionPlayer::GetHealthComponent()`.

- [ ] Write automation tests that assert zero initial stims, capacity 3, partial additions, full-inventory rejection, full-health rejection, 50-point clamped healing, no shield change, one-count consumption, and dead/DBNO rejection.
- [ ] Run the focused test and confirm it fails because the stim type and component do not exist.
- [ ] Add `Stim` to the loot enum and an editor-visible `StimCount` field conditioned on that type with a minimum of 1.
- [ ] Implement the replicated component with `StimCount` owner-only replication, authority-only mutation, an owner RPC for use, `MaxStims = 3`, and `HealAmount = 50.f` as editable data values rather than magic numbers in the operation.
- [ ] Add the component to the player with `CreateDefaultSubobject`, expose a getter, and ensure the actor/component replication lifecycle supports the owner RPC.
- [ ] Route stim grants through the mission inventory, broadcasting `+1 Stim` or `Stims full` on the existing loot notification channel.
- [ ] Run the focused automation test and confirm all stim cases pass.

### Task 2: Slot-3 input and HUD count

**Files:**
- Modify: `Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h`
- Modify: `Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp`
- Modify: the existing player HUD C++/UMG binding selected during implementation after inspecting the current third-slot widget
- Create in editor: `IA_UseStim`
- Modify in editor: active gameplay input mapping context and `BP_ExtractionCharacter` defaults
- Test: extend `ConsumableInventory.spec.cpp` and add a PIE input assertion

**Interfaces:**
- Consumes: `UConsumableInventoryComponent::TryUseStim()` and `OnStimCountChanged`.
- Produces: `AExtractionPlayer::UseStimInput(const FInputActionValue&)` and Blueprint event `OnStimUsed()` for the later animation pass.

- [ ] Add a failing test that calls the player-facing use path and expects one authoritative stim consumption plus a 50-health restore.
- [ ] Bind a Boolean input action with `ETriggerEvent::Started`; do not add a second permanent mapping context.
- [ ] Remove the marketplace key-3 melee mapping and map 3 to the new action in the active gameplay context; preserve the priority-10 companion-mode picker so it consumes 3 while open.
- [ ] Update the existing third-slot HUD presentation to show a stim label/icon placeholder and the replicated count, without introducing a new full inventory screen.
- [ ] Fire `OnStimUsed()` only after successful authority-approved consumption; leave its Blueprint implementation empty.
- [ ] Verify in PIE that 3 consumes once per press, does nothing at full health, and selects companion Combat rather than consuming a stim while the X picker is open.

### Task 3: Objective completion event seams

**Files:**
- Modify: base and concrete breach/scripted door headers and sources identified by `ABreachableDoor`, `AScriptedDoor`, and their common base
- Modify: `Extraction/Source/Extraction/Public/World/LootContainer.h`
- Modify: `Extraction/Source/Extraction/Private/World/LootContainer.cpp`
- Modify: `Extraction/Source/Extraction/Public/World/ExtractionTargetActor.h`
- Modify: `Extraction/Source/Extraction/Private/World/ExtractionTargetActor.cpp`
- Test: `Extraction/Source/Extraction/Private/Tests/LevelObjectiveFlow.spec.cpp`

**Interfaces:**
- Produces: dynamic `OnDoorOpened(AActor*)` broadcast only after the door reaches its open state.
- Produces: dynamic `OnLootCompleted(ALootContainer*, AActor*)` broadcast after contents are granted.
- Produces: explicit extraction-target activation and wave-completed events without BeginPlay objective registration.

- [ ] Write failing tests proving a requested breach does not complete early, an opened door broadcasts once, a looted crate broadcasts once even when the grant is rejected by full capacity, and the extraction target stays objective-silent before activation.
- [ ] Add the door-open delegate to the common door abstraction and broadcast from each concrete finished-open path exactly once.
- [ ] Add the loot-completed delegate after `GrantAllContents`; keep `bLooted` authoritative for idempotence.
- [ ] Replace extraction-target BeginPlay objective registration with an explicit activation method used by the level flow.
- [ ] Run the focused tests and confirm all completion seams pass.

### Task 4: Finite-state level objective flow

**Files:**
- Create: `Extraction/Source/Extraction/Public/World/LevelObjectiveFlow.h`
- Create: `Extraction/Source/Extraction/Private/World/LevelObjectiveFlow.cpp`
- Modify: `Extraction/Source/Extraction/Extraction.Build.cs` only if the new public/private subfolder is not already registered
- Test: `Extraction/Source/Extraction/Private/Tests/LevelObjectiveFlow.spec.cpp`

**Interfaces:**
- Produces: `ActivateFlow()`, `GetCurrentStep()`, primary ID `PrimaryObjective`, optional ID `OptionalSupplies`, editable actor arrays for Room 1 enemies, takedown pairs, and supply crates.
- Consumes: door-open, health-death, loot-completed, route-finished, extraction-wave-completed, and lift-completed events.

- [ ] Write failing tests for every primary transition, early-dead enemy catch-up, two-death takedown completion, optional progress independent of the primary step, nearest-unlooted marker selection, and missing-reference failure logging.
- [ ] Implement the finite enum sequence exactly as approved and replace the same primary objective ID at each transition.
- [ ] Bind and unbind every dynamic delegate in BeginPlay/EndPlay; do not poll on Tick.
- [ ] Count only explicitly assigned Room 1 enemies and both explicitly assigned enemies per takedown pair.
- [ ] Register the optional supply objective on Room 2 entry, retarget it to the nearest unlooted designated crate after every loot event, and remove it after all seven crates are looted or destroyed.
- [ ] Activate the extraction target only after the second takedown pair completes, then hand off to its defend objective and the existing lift gate.
- [ ] Run the focused tests and confirm the complete state machine passes.

### Task 5: Stable objective marker motion

**Files:**
- Modify: `Extraction/Source/Extraction/Public/UI/ObjectiveMarkerWidget.h`
- Modify: `Extraction/Source/Extraction/Private/UI/ObjectiveMarkerWidget.cpp`
- Test: add marker projection/interpolation coverage to the nearest existing UI automation test location

**Interfaces:**
- Produces: configurable interpolation speed and persistent smoothed screen position.

- [ ] Add a failing test for frame-rate-independent convergence and viewport-edge clamping.
- [ ] Interpolate render translation toward the projected target using delta-time and an editable speed; snap on first valid projection so markers do not fly in from the origin.
- [ ] Preserve direct world-space distance calculation and existing behind-camera edge behavior.
- [ ] Run the focused UI test and confirm marker smoothing passes.

### Task 6: Review C++ before build

**Files:** all files changed by Tasks 1–5.

- [ ] Dispatch the consolidated UE5 reviewer with the task goal, changed-file list, and this plan path.
- [ ] Dispatch the UI specialist for the objective marker and third-slot HUD changes.
- [ ] Re-dispatch the C++ implementer for every CRITICAL or WARNING finding.
- [ ] Repeat review until no CRITICAL or WARNING findings remain.

### Task 7: Build and in-engine Room 2 authoring

**Files/assets:**
- Modify in editor: DemoMap
- Create in editor: one `IA_UseStim` asset if Task 2 did not create it before the build
- Modify in editor: the active gameplay mapping context and player Blueprint defaults

- [ ] Ask the user fresh permission to close this project’s Unreal Editor.
- [ ] Close only the Unreal Editor process whose command line contains `Extraction.uproject`.
- [ ] Build `ExtractionEditor Win64 Development` and require `Result: Succeeded` in the log.
- [ ] Reboot through the guarded boot-engine workflow and confirm NeoStack and VibeUE connectivity.
- [ ] Place the level-flow actor and wire all roof, Room 1, Room 2, extraction, and lift references.
- [ ] Place stim crates at (-2200,-650,13630), (-2200,-2100,13630), (-3700,-1150,13630), and (-3150,-4050,13630), using doors 20, 22, 24, and 26 respectively.
- [ ] Place rifle-ammo crates at (-750,-1850,13630), (-2450,-2100,13630), and (-3250,-3400,13630), using the 18/19 room, door 23, and door 25 respectively.
- [ ] Keep Grunt and Grunt 2 in place; add a non-overlapping takedown volume centred near (-930,-2792,13720) with half-extents approximately (160,180,120).
- [ ] Move Grunt 4 near (-625,-650,13718), yaw 0, keep Grunt 3 in place, and add the second volume centred near (-930,-749,13720) with half-extents approximately (160,180,120).
- [ ] Assign the four stim grants and three rifle-ammo grants, save DemoMap, and read back every transform/reference/content field.

### Task 8: End-to-end verification

**Files:**
- Modify: `agent_docs/project_roadmap.md` to reflect verified completion status only after the corresponding playtests pass.

- [ ] Run all focused automation tests and the project’s relevant existing loot, takedown, objective, extraction, and input tests.
- [ ] Roof scenario: breach completes → primary changes to follow → route finish changes to second breach.
- [ ] Room 1 scenario: assigned enemies die → keycard objective → crate card acquisition → exit unlock.
- [ ] Room 2 scenario: Stealth gate → pair 1 both die → pair 2 both die → extraction target activates.
- [ ] Optional scenario: primary continues without looting; Ping + I makes the companion sweep nearby crates; nearest optional marker advances; all seven complete the optional objective.
- [ ] Stim scenario: collect to 3 → fourth reports full → damage player → 3 restores 50 health and consumes one → full-health 3 consumes nothing → X then 3 selects Combat without consuming.
- [ ] Marker scenario: run and sprint while looking at a marked door/crate; yellow marker remains visually stable and edge clamps correctly.
- [ ] Completion scenario: interact with target → finite Director wave → all spawned enemies die → use lift → Level Complete UI.
- [ ] Multiplayer scenario: client-owned stim use is validated on the server and only that owner’s count/health changes.
- [ ] Reconcile the roadmap, leave the editor open on DemoMap, and report reviewer results plus test/build evidence.

## File Note

Primary source areas: `Components`, `World`, `Game`, `Character`, `UI`, `Tests`, `Extraction.Build.cs`, DemoMap, input assets, player Blueprint defaults, and `agent_docs/project_roadmap.md`.

