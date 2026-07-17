# Grunt Pressure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep grunt awareness intact while removing officerless flanking and turning relentless Press into bounded, medium-range advances.

**Architecture:** Add a small, deterministic posture-cadence policy used by `UEnemyPostureComponent`, leaving perception and target sharing untouched. Gate the existing flank task on a living officer and add a minimum-distance filter to the existing protective-cover picker. All timing and distance values remain DataAsset-driven.

**Tech Stack:** Unreal Engine 5.7, C++, Unreal Automation Tests, Behavior Trees, DataAssets.

## Global Constraints

- Preserve shared squad sightings and focus targeting.
- Preserve existing officer-led behavior.
- Preserve Rusher behavior.
- Add no hardcoded `/Game/...` paths to C++.
- Clear all timer handles in `EndPlay()`.
- Keep tuning values in `UEnemyArchetypeData`.

---

### Task 1: Test and implement bounded Press cadence

**Files:**
- Modify: `Extraction/Source/Extraction/Public/Enemy/EnemyPostureComponent.h`
- Modify: `Extraction/Source/Extraction/Private/Enemy/EnemyPostureComponent.cpp`
- Modify: `Extraction/Source/Extraction/Public/Enemy/EnemyArchetypeData.h`
- Create: `Extraction/Source/Extraction/Private/Tests/EnemyPosturePolicy.spec.cpp`

**Interfaces:**
- Produces: a pure `FEnemyPressCadence` policy with schedule, enter, expiry, and recovery operations.
- Produces: DataAsset fields for initial opportunity delay, maximum episode duration, recovery range, and minimum threat distance.

- [ ] **Step 1: Write failing automation tests**

  Cover these exact cases under `Extraction.Enemy.Posture`: initial delay blocks Press, expiry ends Press, one committed advance ends Press, recovery blocks re-entry, and zero-valued tuning preserves legacy behavior for non-grunt archetypes.

- [ ] **Step 2: Run the focused tests and verify RED**

  Run the Unreal automation filter `Extraction.Enemy.Posture`; expect failures because the cadence policy and tuning fields do not exist.

- [ ] **Step 3: Implement the minimal cadence policy**

  Integrate the policy into `EvaluatePosture()` and `NotifyAdvanceExecuted()`. Schedule randomized delays once per opportunity, end the episode after one committed advance, and reschedule after suppression/morale interruption or timeout. Do not alter aggression scoring, shared awareness, or Rusher logic.

- [ ] **Step 4: Run the focused tests and verify GREEN**

  Run `Extraction.Enemy.Posture`; expect every new posture test to pass.

### Task 2: Test and implement officer-gated flanking and advance standoff

**Files:**
- Modify: `Extraction/Source/Extraction/Private/AI/Tasks/BTTask_EnemyFlank.cpp`
- Modify: `Extraction/Source/Extraction/Private/AI/Tasks/BTTask_EnemyCombatFire.cpp`
- Modify: `Extraction/Source/Extraction/Public/Enemy/EnemyPostureComponent.h`
- Modify: `Extraction/Source/Extraction/Private/Enemy/EnemyPostureComponent.cpp`
- Modify: `Extraction/Source/Extraction/Private/Tests/EnemyPosturePolicy.spec.cpp`

**Interfaces:**
- Consumes: `UEnemySquad::HasLivingOfficer()`.
- Produces: a pure advance-distance predicate used by the cover candidate filter.

- [ ] **Step 1: Write failing automation tests**

  Verify the distance predicate rejects candidates inside the minimum threat distance, rejects candidates that fail minimum gain, and accepts a candidate satisfying both. Add a source-level contract test only if the BT task cannot be exercised without a world: the flank task must call `HasLivingOfficer()` before claiming the Flanker role.

- [ ] **Step 2: Run the focused tests and verify RED**

  Run `Extraction.Enemy.Posture`; expect the new distance and officer-gate cases to fail.

- [ ] **Step 3: Implement the minimal behavior changes**

  In `BTTask_EnemyFlank::ExecuteTask`, fail before cooldown or role claims when the squad lacks a living officer. In the protective-cover candidate loop used by posture advances, exclude candidates closer than the configured minimum threat distance while retaining the existing minimum-gain check.

- [ ] **Step 4: Run the focused tests and verify GREEN**

  Run `Extraction.Enemy.Posture`; expect all cases to pass, then run the existing `Extraction.Enemy` automation filter.

### Task 3: Review, build, wire, and playtest

**Files:**
- Modify in editor: `/Game/Core/Enemies/AI/Data/DA_Enemy_Grunt`
- Update if the task completes its roadmap item: `agent_docs/project_roadmap.md`

- [ ] **Step 1: Run the consolidated C++ review**

  Confirm shared awareness/focus paths are untouched, officer behavior is unchanged, Rusher defaults preserve legacy behavior, timers are cleared, and no per-tick work was added.

- [ ] **Step 2: After the engine guard allows the project-scoped close, build**

  Close only the `Extraction.uproject` editor process, build `ExtractionEditor Win64 Development`, and require `Result: Succeeded` in the build log.

- [ ] **Step 3: Reboot and wire the grunt DataAsset**

  Set initial Press delay to 4–8 seconds, maximum episode to 6 seconds, recovery to 10–16 seconds, and minimum threat distance to 800 cm. Leave shared awareness, focus, and officer tuning unchanged.

- [ ] **Step 4: Playtest**

  Grunt-only squad: shared alerting works; no deliberate flank; advances are independent and stop outside 800 cm.

  Officer squad: existing coordinated flank/bounding behavior still runs.

  Officer killed mid-fight: no new flanks start; shared awareness remains active.
