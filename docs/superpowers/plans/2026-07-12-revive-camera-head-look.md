# Revive Camera Head Look Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the player-to-companion revive a face-following first-person animation with bounded head look and no transient 180-degree player rotation.

**Architecture:** C++ owns local camera attachment state, revive lifecycle cleanup, pair placement, and actor-yaw invariants. The player animation Blueprint owns the revive-only spine/neck/head look layer and suppresses procedural montage competition. The montage/AnimBP corrects authored visual orientation without rotating the player actor.

**Tech Stack:** Unreal Engine 5.7, UE C++, UAnimInstance/AnimGraph, SpringArm/Camera components, VibeUE editor tooling, NeoStack PIE testing.

## Global Constraints

- Preserve all unrelated user-owned changes in the dirty worktree.
- Never hardcode `/Game/...` asset paths in C++.
- Do not add reflected C++ symbols unless engine inspection proves existing Blueprint-visible state is insufficient.
- The reviving player actor yaw must remain unchanged during revive entry and exit.
- Free look remains bounded to the existing revive yaw/pitch limits.
- Every revive exit path restores the original camera attachment and settings.

---

### Task 1: C++ revive camera lifecycle and stable actor yaw

**Files:**
- Modify: `Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp`
- Modify: `Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h`

**Interfaces:**
- Consumes: Blueprint-owned `USpringArmComponent`, `IsRevivingTarget()`, `ReviverMontage`, `ReviveTarget`.
- Produces: local revive camera attachment/restore helpers and stable player actor yaw for the in-engine animation layer.

- [ ] **Step 1: Record the failing runtime invariant**

Run PIE, enable `revive.CameraDebug 1`, trigger `CompDown`, and start/cancel one revive.

Expected baseline: `actorYaw` changes by approximately 180 degrees when `revLock` becomes 1 and changes back when it becomes 0; camera location remains pinned at the standing-eye value.

- [ ] **Step 2: Add non-reflected camera attachment state**

Add private state sufficient to restore the original spring-arm attachment exactly: weak original parent, original socket name, original relative transform, original pawn-control-rotation flag, and an active-state guard. Add focused helpers named `AttachReviveCameraToHead()` and `RestoreReviveCameraAttachment()`.

The attach helper must:

```cpp
USpringArmComponent* Arm = FindComponentByClass<USpringArmComponent>();
USkeletalMeshComponent* MeshComp = GetMesh();
if (!IsValid(Arm) || !IsValid(MeshComp) || MeshComp->GetBoneIndex(TEXT("head")) == INDEX_NONE)
{
    return false;
}
```

Save state once, attach with `FAttachmentTransformRules::KeepWorldTransform` to `head`, retain the resulting runtime head-relative transform for the hold, and enable pawn control rotation without changing actor rotation. Do not invent or hardcode a face offset.

- [ ] **Step 3: Remove the world-space camera pin and player rotation flip**

Delete `ReviverPinnedCameraLoc` and the `CalcCamera` location override. Remove `GReviverSeatYawFromView` and the player `SetActorRotation` calls in `SetReviverLock` entry/exit. Anchor the revive look cone to the saved start view/actor yaw rather than a flipped seat yaw.

The target companion transform must be recalculated from the unchanged player frame; only the companion target may be teleported/rotated for paired alignment.

- [ ] **Step 4: Make cleanup symmetric**

Call `RestoreReviveCameraAttachment()` from the common unlock path before normal/DBNO camera ownership resumes. Ensure `EndPlay()` also restores/clears non-owning state without touching destroyed components.

- [ ] **Step 5: Remove montage replay churn after arbitration is available**

Replace the per-frame `PlayReviverKneel` reassert loop with a one-shot interruption diagnostic. Do not silently replay a full-body montage every tick.

- [ ] **Step 6: Verify the source diff**

Run:

```powershell
rg -n "GReviverSeatYawFromView|ReviverPinnedCameraLoc|SetActorRotation" Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp
```

Expected: no revive-path player actor rotation or pinned camera location remains; unrelated actor rotation sites may still be present.

---

### Task 2: Player AnimBP revive montage arbitration and head look

**Files:**
- Modify: the player mesh's active Animation Blueprint identified from `BP_ExtractionCharacter`
- Modify if required: `Extraction/Content/Core/Anims/Shared/Downed/AM_Reviver_Player.uasset`
- Modify: `Extraction/Content/ProceduralFPSKIT/Blueprints/BP_ExtractionCharacter.uasset` only if its procedural montage source is outside the AnimBP

**Interfaces:**
- Consumes: `AExtractionPlayer::IsRevivingTarget()`, `UExtractionAnimInstance::AimYaw`, `UExtractionAnimInstance::AimPitch`.
- Produces: uninterrupted revive pose with bounded upper-body/head tracking and corrected local visual orientation.

- [ ] **Step 1: Load the required VibeUE skills and inspect the active graph**

Load `blueprints`, `blueprint-graphs`, `animation-blueprint`, `animation-montage`, and `skeleton` one at a time. Confirm the active AnimClass, montage slot location, existing aim-offset path, and the exact graph/function spawning transient dynamic montages.

- [ ] **Step 2: Gate procedural montage generation**

Add `IsRevivingTarget()` as an early gate around the procedural montage/action path so no transient dynamic montage is created while the revive hold is active. Preserve all existing behavior outside revive.

- [ ] **Step 3: Correct reviver visual orientation locally**

With player actor yaw fixed, apply the smallest animation-local correction that makes the kneeling body face the companion: prefer correcting the montage/source root orientation; otherwise add a revive-only `Rotate Root Bone` before the final skeletal controls. Do not rotate the character actor or capsule.

- [ ] **Step 4: Add the revive head-look layer**

After the revive montage slot, apply component-space skeletal controls gated by `IsRevivingTarget()`. Distribute the already-clamped `AimYaw`/`AimPitch` across upper spine, neck, and head, with the head receiving the largest share. The layer alpha must blend in/out with the revive state and produce no changes outside revive.

- [ ] **Step 5: Compile, save, and reread**

Compile every modified Blueprint with zero errors. Re-read nodes/connections and montage properties through tooling, then save only the modified assets.

---

### Task 3: Consolidated review and fix loop

**Files:**
- Review every file/asset changed by Tasks 1-2.

**Interfaces:**
- Consumes: task goal, design spec, implementation diff, asset inspection report.
- Produces: zero `CRITICAL` or `WARNING` findings for safety, performance, multiplayer behavior, lifecycle cleanup, and edge cases.

- [ ] **Step 1: Dispatch the consolidated UE5 reviewer**

Brief the reviewer with the task goal, changed files, and `docs/superpowers/plans/2026-07-12-revive-camera-head-look.md`.

- [ ] **Step 2: Fix all CRITICAL/WARNING findings through the appropriate implementer**

Re-dispatch C++ findings to the C++ implementer and asset/AnimBP findings to the in-engine agent. Repeat review for non-trivial fixes until clean.

---

### Task 4: Build, reboot, and autonomous PIE verification

**Files:**
- Runtime evidence: `Extraction/Saved/Logs/Extraction.log`

**Interfaces:**
- Consumes: clean reviewed source/assets.
- Produces: successful editor build, live editor, and requirement-by-requirement runtime evidence.

- [ ] **Step 1: Obtain fresh permission and close only ProjectExtract's editor**

Ask `Close the Unreal Editor to build?` immediately before closing. Scope process termination to command lines containing `Extraction.uproject`.

- [ ] **Step 2: Build and verify the authoritative result**

Run the standard `ExtractionEditor Win64 Development` build and confirm the log contains `Result: Succeeded`.

- [ ] **Step 3: Reboot ProjectExtract and verify both MCP bridges**

Use the `boot-engine` skill. Confirm VibeUE status and a trivial NeoStack execute-script call before PIE.

- [ ] **Step 4: Run the revive scenarios**

Scenario: `CompDown`, begin revive, sweep the mouse within limits. Expected: camera follows face; head/shadow follows view; actor yaw stays fixed.

Scenario: release interact early. Expected: camera hierarchy and normal control restore with no snap or 180-degree shadow turn.

Scenario: complete revive. Expected: companion revives, camera restores, and no transient procedural montage replaces `AM_Reviver_Player`.

Scenario: player enters DBNO mid-revive. Expected: revive cancels, attachment restores, then DBNO camera ownership activates.

- [ ] **Step 5: Completion audit**

Compare runtime camera/head/actor rotation logs and visual evidence against every requirement in the design spec. Do not mark complete if any requirement lacks direct evidence.
