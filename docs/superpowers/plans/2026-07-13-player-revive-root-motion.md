# Player Revive Root Motion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the player-revives-companion montage pair drive both characters through montage root motion while preserving the existing initial pair alignment.

**Architecture:** Keep the current one-time companion alignment and player seat unchanged. Remove the movement-mode and per-tick position constraints that currently discard the montage root motion, matching the working companion-revives-player path. Existing timer ownership, range validation, montage playback, and cleanup remain unchanged.

**Tech Stack:** Unreal Engine 5.7, UE C++, CharacterMovementComponent, AnimMontage.

## Global Constraints

- Preserve every unrelated dirty-worktree change.
- Do not commit, stage, push, or revert user-owned work.
- Keep the current initial pair position and rotation logic unchanged.
- Root motion must come only from the active revive montages.
- Preserve timer-owned completion and symmetric cancellation cleanup.

---

### Task 1: Remove player-revive root-motion suppression

**Files:**
- Modify: `Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp`
- Modify: `Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h`
- Modify: `Extraction/Source/Extraction/Private/Companion/CompanionCharacter.cpp`

**Interfaces:**
- Consumes: existing `BeginReviveHold`, `SetBeingRevived`, `UpdateRevive`, montage playback, and cleanup paths.
- Produces: the same aligned revive pair with capsule movement driven by the paired montages.

- [ ] **Step 1: Record the failing source contract**

Run:

```powershell
$hits = rg -n "MOVE_None|ReassertXY|ReviveHoldSeatLocation|ReviveHoldPatientLocation" Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h Extraction/Source/Extraction/Private/Companion/CompanionCharacter.cpp
if ($LASTEXITCODE -eq 0) { $hits; throw 'Player revive still suppresses montage root motion' }
```

Expected: FAIL with the current hold freeze, XY pin, and pin-state matches.

- [ ] **Step 2: Allow montage root motion during the hold**

Keep `StopMovementImmediately()` at player hold start and companion patient activation, but keep both characters in `MOVE_Walking` so `UCharacterMovementComponent` consumes montage root motion. Do not change the existing alignment, seat, rotation, montage rate, timer, or movement-ignore logic.

- [ ] **Step 3: Remove the hold-time XY override**

Delete the captured seat/patient pin state and the `ReassertXY` block in `UpdateRevive`. The normal proximity guard remains active and cancels only if the authored pair exceeds `ReviveProximityRadius`.

- [ ] **Step 4: Verify the source contract**

Run the Step 1 command again.

Expected: PASS with no matches and exit code 0.

---

### Task 2: Review and runtime verification

**Files:**
- Review all files changed by Task 1 plus the assigned revive montage assets.

**Interfaces:**
- Consumes: the focused source diff and the existing paired montage assignments.
- Produces: a clean build and direct evidence that player-revive movement is montage-driven.

- [ ] **Step 1: Review the focused diff**

Check root-motion ownership, movement-mode restoration, cancellation, target invalidation, DBNO transitions, and preservation of the current alignment logic. Fix every `CRITICAL` or `WARNING`, then repeat the review.

- [ ] **Step 2: Build**

Build `ExtractionEditor Win64 Development` and require `Result: Succeeded` in the build log.

- [ ] **Step 3: Reboot and verify**

Boot ProjectExtract, confirm the editor bridges, then run these scenarios:

Scenario: player revives companion. Expected: the pair starts in the existing aligned pose and both capsules follow the montage root motion without being snapped back each tick.

Scenario: companion revives player. Expected: existing movement and alignment remain unchanged.

Scenario: release revive early. Expected: both montages stop and normal movement, weapon visibility, camera ownership, and collision ignores restore.

Scenario: player becomes DBNO during the hold. Expected: revive cancels once with no retained root-motion or revive state.
