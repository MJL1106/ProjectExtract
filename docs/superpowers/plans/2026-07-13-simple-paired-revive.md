# Simple Paired Revive Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce both revive directions to one face-to-face alignment, one montage per character, one timer, and one cleanup path while animation owns the player's head and camera.

**Architecture:** Existing player and companion classes keep ownership of their montage properties and DBNO state. The player revive path and companion BT task each align the pair once from the live actor positions, play both montages once, and use the existing revive duration for gameplay completion. Blueprint camera wiring supplies the animation-driven first-person view; C++ does not add camera transforms, look cones, montage replay, or montage-driven completion.

**Tech Stack:** Unreal Engine 5.7, UE C++, Behavior Tree task, AnimMontage, Blueprint-owned SpringArm/camera.

## Global Constraints

- Preserve every unrelated dirty-worktree change.
- Do not commit, stage, push, or revert user-owned work.
- Touch only revive code and the active player camera/animation assets required by this plan.
- Play each revive montage once per hold; never replay it from Tick.
- Use `ReviveAlignDistance`; remove hardcoded authored forward/right/yaw offsets and revive alignment CVars.
- Pair rotations must be reciprocal look-at rotations on the XY plane.
- Revive gameplay completes from the existing hold timer, not montage blend-out.
- During either player revive role, look/move/fire input is locked and the animation owns head/camera position and rotation.
- Preserve server-authoritative DBNO exit, range validation, and symmetric movement-ignore cleanup.

---

### Task 1: Simplify both C++ revive paths

**Files:**
- Modify: `Extraction/Source/Extraction/Private/AI/Tasks/BTTask_RevivePlayer.cpp`
- Modify: `Extraction/Source/Extraction/Public/AI/Tasks/BTTask_RevivePlayer.h`
- Modify: `Extraction/Source/Extraction/Private/Companion/CompanionCharacter.cpp`
- Modify: `Extraction/Source/Extraction/Public/Companion/CompanionCharacter.h`
- Modify: `Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp`
- Modify: `Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h`
- Modify comments only if needed: `Extraction/Source/Extraction/Public/Character/ExtractionPlayerInterface.h`

**Interfaces:**
- Consumes: existing `ReviveMontage`, `BeingRevivedMontage`, `ReviveDuration`, `ReviveAlignDistance`, DBNO interface, and movement-ignore lifecycle.
- Produces: a timer-owned paired revive with one-shot montage playback and reciprocal face-to-face actor rotations.

- [ ] **Step 1: Record the failing source contract**

Run:

```powershell
$patterns = 'AuthoredFwd|AuthoredRight|AuthoredYaw|GReviveCompanionOffset|GRevivePlayerYawOffset|ShouldReassertReviveMontage|OnBeingRevivedMontageBlendOut|ClampLookCone|GReviveCameraDebug'
$hits = rg -n $patterns Extraction/Source/Extraction/Private/AI/Tasks/BTTask_RevivePlayer.cpp Extraction/Source/Extraction/Private/Companion/CompanionCharacter.cpp Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp Extraction/Source/Extraction/Public/Companion/CompanionCharacter.h Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h
if ($LASTEXITCODE -eq 0) { $hits; throw 'Legacy revive complexity is still present' }
exit 0
```

Expected: FAIL with matches for the current authored offsets, montage replay/completion, look cone, and camera diagnostics.

- [ ] **Step 2: Replace companion-revives-player alignment**

At hold entry in `UBTTask_RevivePlayer`, stop movement, calculate the XY direction from the downed player to the companion's live pre-snap location, fall back to the player's forward vector only when the positions coincide, place the companion at `PlayerLocation + Direction * ReviveAlignDistance`, rotate the companion toward the player, then call `AlignForRevive` so the downed player rotates toward the companion. Start both montages once after alignment.

The calculation must satisfy:

```cpp
const FVector PatientToReviver = (ReviverLocation - PatientLocation).GetSafeNormal2D();
const FVector AlignedReviverLocation = PatientLocation + PatientToReviver * ReviveAlignDistance;
const float PatientYaw = PatientToReviver.Rotation().Yaw;
const float ReviverYaw = (-PatientToReviver).Rotation().Yaw;
```

Remove authored-offset CVars, hardcoded `60/-64.5/-46` values, hold diagnostics, montage reassertion, and montage grace completion. Increment the existing timer and call `ExitDBNO()` at `ReviveDuration`.

- [ ] **Step 3: Replace player-revives-companion alignment**

Keep the reviving player as the anchor. On the server, place the downed companion directly in front of the player's actor forward at the companion's `ReviveAlignDistance`, rotate the companion toward the player, and leave the player's actor rotation unchanged. Call the target's `AlignForRevive` so the companion uses the same look-at rule. Preserve authority validation and movement-ignore cleanup.

- [ ] **Step 4: Reduce montage lifecycle to play/stop**

In both player and companion being-revived implementations, play the assigned montage once at hold start, rate-scaled to the hold duration, and stop it on hold end. Remove blend-out delegates and montage-driven `ExitDBNO`. In the companion reviver implementation, remove `ShouldReassertReviveMontage`, active-montage diagnostics, and live override getters/CVars; use the Blueprint data properties directly.

- [ ] **Step 5: Let animation own the player view**

Keep `LookInput`, `DoAim`, `DoMove`, fire, reload, and ADS gated while the player is either reviver or patient. Remove revive look cones, camera traces, deferred camera handback, and reviver yaw-follow deferral. Save/restore `bUseControllerRotationYaw` once per role. While the player is actively being revived, disable DBNO pawn-control free look so the existing head-linked camera inherits the montage; restore DBNO free look only if the hold ends while the player remains DBNO.

- [ ] **Step 6: Verify the green source contract**

Run the Step 1 command again.

Expected: PASS with no matches and exit code 0.

- [ ] **Step 7: Focused source verification**

Run:

```powershell
rg -n "Montage_Play|SetActorLocation|SetActorRotation|AlignForRevive|ReviveElapsed|ExitDBNO|MoveIgnoreActor" Extraction/Source/Extraction/Private/AI/Tasks/BTTask_RevivePlayer.cpp Extraction/Source/Extraction/Private/Companion/CompanionCharacter.cpp Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp
```

Expected: one play path per montage role, one alignment per hold, timer-owned completion, and symmetric ignore cleanup.

---

### Task 2: Make the player camera purely animation-driven during revive

**Files:**
- Inspect/modify: `Extraction/Content/ProceduralFPSKIT/Blueprints/BP_ExtractionCharacter.uasset`
- Inspect/modify only if revive-specific controls exist: `Extraction/Content/ProceduralFPSKIT/Demo/Character/Mannequins/Animations/ABP_Manny.uasset`
- Inspect only: player and companion revive montage assets and `BP_Companion.uasset`

**Interfaces:**
- Consumes: C++ revive lock flags and the active full-body revive montages.
- Produces: a camera attached to the animated head with no revive-only procedural head or camera override.

- [ ] **Step 1: Inspect live asset state**

Load the VibeUE `blueprints`, `animation-blueprint`, `animation-montage`, and `skeleton` skills one at a time. Confirm the exact montage assignments, active montage slot, SpringArm parent/socket, `bUsePawnControlRotation` default, and any revive-only AnimBP skeletal-control layer.

- [ ] **Step 2: Apply the minimal camera wiring**

Attach the existing player SpringArm/camera chain to the mesh's `head` socket if it is not already attached. Keep the normal relative transform unless inspection proves it is invalid. Remove only revive-specific head-look/camera override logic. Do not add aim offsets, control-rig nodes, camera interpolation, or runtime reattachment.

- [ ] **Step 3: Compile and reread**

Compile modified Blueprints with zero errors, save only modified assets, and reread the component hierarchy plus AnimBP connections to confirm the camera inherits the head and no revive-only override remains.

---

### Task 3: Review, build, reboot, and playtest

**Files:**
- Review all files/assets changed by Tasks 1-2.

**Interfaces:**
- Consumes: focused implementation diff and asset inspection report.
- Produces: clean review, successful build, running editor, and direct revive evidence.

- [ ] **Step 1: Consolidated C++ review**

Dispatch `ue5-reviewer` with the task goal, changed files, and this plan. Fix every `CRITICAL` or `WARNING` through `ue5-cpp-implementer`, then re-review until clean.

- [ ] **Step 2: Confirm before closing the editor**

Ask exactly: `Close the Unreal Editor to build?` and proceed only after `Yes, close now`.

- [ ] **Step 3: Build and reboot**

Build `ExtractionEditor Win64 Development`, require `Result: Succeeded`, then reboot through `boot-engine` and confirm both editor bridges.

- [ ] **Step 4: Runtime scenarios**

Scenario: player revives companion. Expected: companion snaps directly in front, faces the player, both montages play once, and the animated head owns the camera.

Scenario: companion revives player. Expected: companion uses its approach side, pair stands at `ReviveAlignDistance`, both face each other, and both montages play once.

Scenario: release player revive early. Expected: both montages stop and movement, weapon visibility, camera ownership, and collision ignores restore.

Scenario: reviver becomes DBNO. Expected: revive cancels once and the downed camera mode resumes without retained revive state.
