# Enemy Anim Map — Officer

**Branch:** Enemies  
**Date:** 2026-06-18  
**Scope:** READ-ONLY mapping. No code or asset edits.

---

## 1. Weapon & Rig

| Field | Value |
|---|---|
| Weapon | Infima Assault Rifle — `BP_EnemyAssaultRifle`, `DA_AssaultRifle` (same as Grunt/Grenadier) |
| Weapon class | Two-handed shouldered auto. Grip style: rifle low-ready / shouldered. |
| Skeleton | `SK_Military_Character_Skeleton` (Quantum Military Character pack) |
| Physics asset | `PA_QuantumCharacter.uasset` |
| IK rig (source) | `IK_MilitaryCharacter.uasset` |
| Retargeters on disk | `RTG_RifleMannequin_to_Military.uasset`, `RTG_RifleAutoMannequin_to_Military.uasset` |
| Anim BP | `ABP_Enemy_Grunt` (`/Game/Enemy/AI/ABP_Enemy_Grunt`) — shared by all 7 archetypes |
| Anim instance class | `UEnemyAnimInstance` (`Extraction/Source/Extraction/Public/Animation/EnemyAnimInstance.h`) |

---

## 2. Current State

The Officer has no dedicated anim assets and no anim-instance differentiation from the Grunt today.

- `ABP_Enemy_Grunt` is the anim class for all 7 archetypes with no per-archetype branching; `UEnemyAnimInstance` does not read `EEnemyArchetype` at all.
- The montage fields on `UEnemyAnimInstance` (`FireMontage`, `ReloadMontage`, `HitReactMontage`, `DeathMontage`, `MeleeMontage`, `TakedownReactionMontage`, `SingleFireMontage`) are `EditDefaultsOnly` on the ABP — meaning all archetypes share the same seven montage assignments unless a per-archetype ABP child is created.
- As of the last confirmed wiring state (`e0a9c96e` commit, 2026-06-18), all 6 non-Grunt BP children were realigned to `ABP_Enemy_Grunt` (previously `ABP_CompanionMain`). No per-archetype ABP children exist.
- `UEnemyAnimInstance` has zero hooks for command gestures: no `CommandGestureMontage` field, no `PlayCommandGestureMontage()` method, no delegate binding for rally/focus-fire callouts.
- `BTTask_OfficerCommand` fires barks (`EBarkType::FocusTarget`, `EBarkType::CoveringGo`) and calls `Squad->SetFocusTarget()` / `Squad->Rally()` on its 3-second beat (`BTTask_OfficerCommand.cpp:133–183`) — but there is no call into the anim instance or weapon to trigger any gesture. The officer is visually identical to a grunt holding a rifle at all times.
- Source: `EnemyAnimInstance.h` lines 109–163; `BTTask_OfficerCommand.cpp` lines 129–183.

---

## 3. Required Anim Set

### 3a. Combat baseline (inherited from rifle — same as Grunt)

These are required to exist and be assigned on any Officer-specific ABP child. They are currently wired on `ABP_Enemy_Grunt` and can be reused as-is or given officer-specific variants later.

| Slot | Clip(s) | Notes |
|---|---|---|
| Idle — standing | `Mil_Rifle02_St_Idle00` | [QUANTUM-NATIVE] on Quantum skeleton already |
| Idle — crouch | `Mil_Rifle_Cr_Idle00` | [QUANTUM-NATIVE] |
| Idle — cover hunkered (L/R) | `Mil_anim_CoverDown_Idle_Left/Right` | [QUANTUM-NATIVE] |
| Locomotion standing | `Mil_Rifle02_St_Walk/Run_*_IPC` (8-dir blendspace) | [QUANTUM-NATIVE] |
| Locomotion crouch | `Mil_Rifle_Cr_Walk_*_IPC` (8-dir blendspace) | [QUANTUM-NATIVE] |
| Aim offset | `AO_Companion_Rifle02` (17-pose AO grid, Mil_Rifle02_St_Aim_*) | [QUANTUM-NATIVE] — retargeted from Rifle02 Mannequin set |
| Fire (loop) | `AM_Companion_Fire_Loop` / `Rifle01_St_Shoot_Auto_Loop` | [QUANTUM-NATIVE] retargeted |
| Fire (single) | `AM_Companion_Fire_Single` / `Rifle01_St_Shoot_Auto_Single` | [QUANTUM-NATIVE] retargeted |
| Reload | `AM_Companion_Reload` / `Mil_Rifle03_St_Reload_Auto` | [QUANTUM-NATIVE] retargeted |
| Hit react (standing) | `AM_Companion_hit_Idle` / `Mil_Rifle01_St_Hit_Light_F` | [QUANTUM-NATIVE] retargeted |
| Hit react (aiming) | `AM_Companion_HitReact_Aim` | [QUANTUM-NATIVE] retargeted |
| Death (standing) | From RifleMega pack: `Rifle01/02/03_St_Death_*` variants | [RETARGET] — on SK_Mannequin (RifleMega), need RTG_RifleMannequin_to_Military pass |
| Cover peek (L/R) | `AM_Crouch_Cover_Left/Right` | [QUANTUM-NATIVE] retargeted |

### 3b. SIGNATURE — Command Gesture Set (priority deliverable)

These are what make the Officer read as a commander rather than a grunt. All are MUST-SOURCE from Mixamo or a third-party pack and retargeted to Quantum skeleton.

| Gesture | Trigger in code | Visual intent | Layering |
|---|---|---|---|
| **Point-to-target** (FOCUS) | `BTTask_OfficerCommand.cpp:133–141` — FocusTarget bark fires on `FocusCooldownTimer <= 0`, every 3s | Right-arm jab forward / index finger extended toward the enemy target direction | Upper-body additive slot over rifle idle/locomotion |
| **"Advance"/"Go" signal** (BOUNDING) | `BTTask_OfficerCommand.cpp:178–186` — CoveringGo bark fires when `Squad->StartBounding()` succeeds, every 20s | Arm swept forward or two-finger directional hand wave ("move up") | Upper-body additive slot |
| **Rally** (RALLY) | `BTTask_OfficerCommand.cpp:144–168` — fires when any squadmate is Broken, every 25s | Raised fist or beckoning arm pull ("come on") | Upper-body additive slot |
| **Alert/commanding idle** (PASSIVE) | Looping — plays during `OfficerCommand` task when no gesture is active and no firing | Officers scan/scan-hold with a slightly raised hand, rifle lowered; distinct from generic Rifle02 idle | Full-body or lower-body with upper-body overlay |

---

## 4. What Exists Per Clip

### [QUANTUM-NATIVE] — On Quantum skeleton, usable now

All clips under `/Game/QuantumCharacter/Retarget/` are already on `SK_Military_Character_Skeleton`. These cover the full combat baseline.

- **Aim offset grid:** `AO_Companion_Rifle02` + 17 `Mil_Rifle02_St_Aim_*` poses — full ±90° yaw, ±90° pitch coverage.
- **Locomotion:** 8-direction standing walk/run + 8-direction crouch walk, all IPC (in-place cycle). Blendspace `BS_Companion_Rifle02_Locomotion`.
- **Idles:** `Mil_Rifle02_St_Idle00`, `Mil_Rifle_Cr_Idle00`, `Mil_anim_CoverDown_Idle_Left/Right`.
- **Combat montages:** fire loop, fire single, reload, hit-react (idle + aim), cover peek L/R.

### [RETARGET] — On SK_Mannequin, retargeter present

These clips exist on SK_Mannequin (UE4 mannequin or RifleMega mannequin) and `RTG_RifleMannequin_to_Military` / `RTG_RifleAutoMannequin_to_Military` can convert them to Quantum skeleton.

- **Death:** `RifleMega_MocapAnimPack` has `Rifle01/02/03_St_Death_B/F/L/R` (4-direction standing deaths) and `Rifle_Cr_Death_B/F/L/R` (4-direction crouch deaths). Retarget one standing variant for the officer death montage slot.
- **Patrol idles:** `Rifle_Patrol_Idle00–05` and `Rifle_Patrol_Act_Idle00–05` — six varied patrol idles that read as "soldier standing by." Any of these could anchor the officer's non-gesture patrol idle with more weight than the base standing idle.
- **Hit reacts:** `Mil_Rifle01/02_St_Hit_Light_F` already retargeted (in `/Retarget/Combat/`); `RifleMega` also has `Rifle01/02/03_St_Hit_*` variants for additional directions.
- **Stun set:** `Rifle01/02/03_St_Stun_Start/Loop/End` — could double as a "broken/suppressed" state additive for the officer's fallback branch.

### [MUST-SOURCE] — Not on disk in any form

No clip in any pack on disk (QuantumCharacter, RifleMega_MocapAnimPack, MotionAnimations, ImportedAssets/RifleAnims) contains any arm-signal, point, beckon, hand-wave, or command-gesture animation. The RifleMega OtherAnims folders contain only dodge, grenade-throw, jump, pick-up, and stun. MotionAnimations covers climbing, cover, crouch, interaction (button-presses, valve), and traversal. There is no gesture content anywhere in Content/.

---

## 5. Gaps

### [MUST-SOURCE] — Command gesture set (the entire signature priority)

All four gesture clips must be sourced externally:

| Clip | Mixamo candidate | Notes |
|---|---|---|
| Point-to-target | "Pointing" (Mixamo free) — arm raises, index finger extended. ~1.5s one-shot. | Most recognisable "officer orders" read. Retarget to Quantum via RTG_RifleMannequin_to_Military if sourced in T-pose against UE4 mannequin. |
| Advance/Go signal | "Wave" or "Beckoning" (Mixamo free) — one-arm forward sweep. ~1.5s. | Could also fake with "Hand Raise" clipped to the push. |
| Rally | "Cheer" / "Come Here" (Mixamo free) — arm raise + pull. ~2s. | The raised-fist read works; "Come Here" is less military. Either is better than nothing. |
| Commanding idle | "Idle_Neutral_02" or "Standing Idle" with arms slightly raised (Mixamo). Loop. | Goal: rifle still held in support hand with left arm lowered, right arm slightly forward — suggests readiness without the pure raised-rifle-ready of Mil_Rifle02_St_Idle00. Mixamo "Standing Idle" variants are often the closest free source. |

All four will need:
1. Download from Mixamo as FBX (in T-pose, against UE4 mannequin / SK_Mannequin).
2. Import to a staging folder (e.g. `/Game/Enemy/Officer/Anims/Source/`).
3. Retarget via `RTG_RifleMannequin_to_Military` to produce `/Game/Enemy/Officer/Anims/`.
4. Create animation montages for the three one-shot gestures (point, advance, rally) with an upper-body slot.

### [MUST-SOURCE] — Death montage on Quantum skeleton

The retarget of one `RifleMega Rifle02_St_Death_F` or `_B` variant is required before the officer death montage slot can be assigned. This is technically a [RETARGET] task, not external sourcing, but it hasn't been done yet.

### [HAVE — NOT WIRED] — Patrol idles, stun/stagger variants

`Rifle_Patrol_Idle00–05` and the stun set exist on disk and need only a retarget pass; not currently used by any enemy.

---

## 6. Wiring Plan

### 6a. C++ additions required

Add to `UEnemyAnimInstance` (`EnemyAnimInstance.h` / `.cpp`):

```cpp
// In protected montage assets section (line 109):
UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
TObjectPtr<UAnimMontage> CommandGestureMontage_FocusTarget;

UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
TObjectPtr<UAnimMontage> CommandGestureMontage_Advance;

UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
TObjectPtr<UAnimMontage> CommandGestureMontage_Rally;

// Public method for BT task to call:
UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
void PlayCommandGesture(ECommandGestureType GestureType, float PlayRate = 1.f);
```

Add a new minimal enum (or a simple int/tag param) so `BTTask_OfficerCommand` can specify which gesture to play without knowing montage assets. The task already has the three callout moments.

`BTTask_OfficerCommand.cpp` modifications (line call sites):
- Line 140 (focus-fire call): after `Squad->SetFocusTarget(...)`, cast to `UEnemyAnimInstance` and call `PlayCommandGesture(FocusTarget)`.
- Line 165 (`bNeedsRally` block): after `Squad->Rally(...)`, call `PlayCommandGesture(Rally)`.
- Line 179 (`StartBounding()` success): after the CoveringGo bark, call `PlayCommandGesture(Advance)`.

### 6b. ABP setup

1. Create `ABP_Enemy_Officer` as a child of `ABP_Enemy_Grunt` (or a new ABP deriving from `UEnemyAnimInstance`).
2. Add an **UpperBody montage slot** node in the AnimGraph, layered above the base pose. This is how the gesture plays without interrupting locomotion or the rifle aim-offset.
3. Assign the four gesture montages + the death montage in the `ABP_Enemy_Officer` defaults panel.
4. Update `BP_Enemy_Officer` to point at `ABP_Enemy_Officer` instead of `ABP_Enemy_Grunt`.
5. `DA_Enemy_Officer` does not need changes — no anim asset references live in the DA by design (C++ stays asset-agnostic; `CLAUDE.md` hard rule).

### 6c. Gesture montage setup (once clips are sourced and retargeted)

Each of the three command gesture montages (FocusTarget, Advance, Rally):
- Section: `Default` (no looping — these are one-shots).
- Blend in: 0.15s, blend out: 0.25s.
- Slot: `UpperBody` (so rifle aim-offset and locomotion continue underneath).
- Length: 1.2–2.0s. Point-to-target should be the shortest (~1.2s) since it fires every 3s on the command beat; rally can be longer (~2.0s) since it fires every 25s.

The commanding idle is a looping full-body state, not a montage. The cleanest wiring is a BlendSpace or a dedicated idle animation variable in the ABP AnimGraph, gated on `bHasCommandAura && !bIsFiring && !bIsAiming`. At patrol/non-combat it replaces the standard `Mil_Rifle02_St_Idle00`.

### 6d. Signal convention for the code hook

The recommended signal: `BTTask_OfficerCommand` gets a weak ref to the `UEnemyAnimInstance` at `ExecuteTask` (via `Cast<UEnemyAnimInstance>(Pawn->GetMesh()->GetAnimInstance())`) and calls `PlayCommandGesture(ECommandGestureType)` directly. This keeps the BT task's existing three callout moments as the canonical trigger points, and the anim instance remains the montage authority (consistent with the existing pattern for `PlayHitReactMontage`, `PlayMeleeMontage`, etc.).

No delegate is needed for this — the BT task already owns the callout moment explicitly, unlike hit-react which is delegate-driven because the event source (TakeDamage) is outside the BT.

---

## Summary of gaps by priority

| Gap | Tag | Effort |
|---|---|---|
| Command gesture clips (point, advance, rally, commanding idle) | [MUST-SOURCE] | Mixamo download × 4 + retarget × 4 |
| `UEnemyAnimInstance` command gesture fields + `PlayCommandGesture()` | Code | ~20 lines |
| `BTTask_OfficerCommand` call sites × 3 | Code | ~6 lines |
| `ABP_Enemy_Officer` child ABP with UpperBody slot | In-engine | 15 min |
| Death montage retarget from RifleMega | [RETARGET] | 10 min |
| Commanding idle loop assignment in `ABP_Enemy_Officer` | In-engine | 5 min |
