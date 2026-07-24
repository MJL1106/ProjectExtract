# Enemy Anim Map — RUSHER
**Branch:** Enemies · **Date:** 2026-06-18 · **Status:** READ-ONLY reference; no code was modified

---

## 1. Weapon & Rig

| Field | Value |
|---|---|
| Weapon | BP_EnemySMG → SK_SMG_Default_Combined |
| DA | DA_SMG (15 RPS auto, 32 mag) |
| Skeleton | SK_Military_Character_Skeleton (Quantum "Military" rig — standard mannequin bones, same hierarchy as UE5 Mannequin) |
| Anim BP | ABP_Enemy_Grunt (shared by all 7 archetypes; no per-archetype ABP today) |
| Anim instance class | UEnemyAnimInstance |
| Perspective | Third-person only |

---

## 2. Current State

### What runs today (EnemyAnimInstance.h / .cpp)

`UEnemyAnimInstance` drives the ABP with these live variables:

- `Speed`, `Direction`, `NormalizedSpeed`, `bHasVelocity`, `bIsAccelerating`, `bIsCrouched`, `bIsAlive` — locomotion
- `AimPitch`, `AimYaw`, `bIsAiming` — aim offset
- `bIsFiring`, `bIsReloading`, `bInCombat` — combat state

Montage slots wired and working (delegate-driven):
- `FireMontage` — looping fire; auto-triggered on `bIsFiring` rising edge (`EnemyAnimInstance.cpp:138`)
- `SingleFireMontage` — per-shot fallback via `OnWeaponFired` delegate (`cpp:231`) — relevant for single-fire modes
- `ReloadMontage` — auto-triggered on `bIsReloading` rising edge (`cpp:143`)
- `HitReactMontage` — triggered via `OnHitReact` delegate; **suppressed during firing/aiming/combat** (`cpp:203`)
- `DeathMontage` — triggered on `OnTakedownExecuted` fallback (`cpp:249`)
- `MeleeMontage` — **slot exists, delegate is wired, function is implemented** (`cpp:215–219`); triggered via `EnemyCharacter.OnMeleePerformed` → `HandleMeleePerformed()` → `PlayMeleeMontage()` (`cpp:239–242`)
- `TakedownReactionMontage` — enemy receives takedown from player

### What is wrong today

1. **No melee/CQC anim is assigned.** `MeleeMontage` is `nullptr` — `PlayMeleeMontage()` returns immediately (`cpp:216: if (!IsValid(MeleeMontage)) return;`). `PerformMelee()` fires the damage and broadcasts `OnMeleePerformed`, but nothing plays. The archetype's signature moment is completely invisible.

2. **No sprint animation.** `BS_Companion_Rifle02_Locomotion` drives the shared blendspace using Mil_Rifle02_St_Run_* clips retargeted to Quantum. None of these push past a "run" gate. At the Rusher's 600 cm/s combat speed the locomotion BS will play run clips full-throttle, but there is no dedicated sprint cycle — it reads as skating/sliding rather than an aggressive charge.

3. **Fire-while-moving uses the standard rifle aim-offset + fire montage.** This is acceptable for ranged suppression, but `AO_Companion_Rifle02` was authored for the companion's SMG/rifle posture and matches the Rusher's SMG close enough. No bug — just note that no separate fire-while-sprinting blend exists; the ABP fires the loop montage over whatever locomotion state is active.

---

## 3. Required Anim Set

Priority order: melee/CQC is the deliverable.

### 3a. Base locomotion (already retargeted — acceptable for Rusher)

| State | Clip | Notes |
|---|---|---|
| Idle (standing) | `Mil_Rifle02_St_Idle00` | [QUANTUM-NATIVE] |
| Walk 8-directional | `Mil_Rifle02_St_Walk_*_IPC` (8 dirs) | [QUANTUM-NATIVE] |
| Run 8-directional | `Mil_Rifle02_St_Run_*_IPC` (8 dirs) | [QUANTUM-NATIVE] |
| Crouch idle | `Mil_Rifle_Cr_Idle00` | [QUANTUM-NATIVE] |
| Crouch walk | `Mil_Rifle_Cr_Walk_*_IPC` (8 dirs) | [QUANTUM-NATIVE] |
| Cover (crouch) | `AM_Crouch_Cover_Left/Right`, `Mil_anim_CoverDown_Idle_*` | [QUANTUM-NATIVE] |

### 3b. Sprint (needed — Rusher's defining locomotion)

| State | Required clip | Notes |
|---|---|---|
| Sprint forward | One full-body sprint cycle, weapon tucked | Currently missing in the Retarget folder |

Source candidate: `RifleMega_MocapAnimPack/AnimationsFBX/Rifle_Styly01_St/Rifle01_LocomotionSet/Rifle01_St_Sprint_IPC.uasset` — on `Rifle_Mannequin_A_Skeleton`, must be retargeted to Quantum via `RTG_RifleMannequin_to_Military`.

### 3c. ADS / aim offset

`AO_Companion_Rifle02` + `Mil_Rifle02_St_Aim_*` (17 poses, full 8-direction ±90°) already retargeted and in use. Acceptable for a shouldered-SMG posture — no change needed.

### 3d. Fire

| State | Clip |
|---|---|
| Fire loop | `AM_Companion_Fire_Loop` (→ `Rifle01_St_Shoot_Auto_Loop`) [QUANTUM-NATIVE] |
| Fire single | `AM_Companion_Fire_Single` (→ `Rifle01_St_Shoot_Auto_Single`) [QUANTUM-NATIVE] |

These were authored for a rifle but are indistinct enough at SMG speeds. Acceptable.

### 3e. Reload

`AM_Companion_Reload` / `Mil_Rifle03_St_Reload_Auto` [QUANTUM-NATIVE]. Acceptable.

### 3f. Hit-react

`AM_Companion_HitReact_Aim` / `Mil_Rifle01_St_Hit_Light_F` / `Mil_Rifle02_St_Hit_Light_F` [QUANTUM-NATIVE].

### 3g. Death

Four directional death clips exist in `RifleMega_MocapAnimPack/AnimationsFBX/Rifle_Styly01_St/Rifle01_Hit_DeathSet/` — on `Rifle_Mannequin_A_Skeleton`; require retarget. `MM_Death_*` clips (`Characters/Mannequins/Anims/Death/`) are on the standard UE5 Mannequin skeleton; also require retarget. **Neither is currently assigned to `DeathMontage` on ABP_Enemy_Grunt.** Today the enemy dies straight to ragdoll with no montage (the takedown path falls back to `PlayDeathMontage` only when `TakedownReactionMontage` is also null — `cpp:249`).

### 3h. Melee / CQC — PRIORITY DELIVERABLE

| Beat | Required clip | Rationale |
|---|---|---|
| Strike (primary) | Gun-butt / rifle strike | Rusher carries an SMG; a rifle-stock whip is consistent with any long weapon. Best source: `RifleMega_MocapAnimPack/AnimationsFBX/Rifle_Melee/Rifle01_Melee_Hard_01` or `Rifle01_Melee_Hard_02` (hard = committed lunge contact, not just a swipe) |
| Strike (variant) | Light follow-up | `Rifle01_Melee_Light_01` / `Rifle01_Melee_Light_02` for the 1.5s cooldown repeat |
| Optional: knife strike | Not recommended — adds a prop attachment requirement with no gameplay gain over gun-butt |
| Optional: fist | `Anim_Character_AttackL` / `Anim_Character_AttackR` (FistAnims) — no prop; reads as unarmed brawl, which breaks the "soldier with SMG" read |

**Why RifleMega gun-butt over kit melee:** the kit clips (`Anim_Arms_AmericanRifle_Melee`, `Anim_Arms_Pistol_Melee`) are **FP arms-only** — authored on `SK_Mannequin` with no visible lower body, designed to run on a first-person arms mesh. They are upper-body montages that will work on the TP skeleton but will not show leg commitment. The RifleMega clips (`Rifle_Mannequin_A_Skeleton`) are **full-body TP mocap** — the character steps into the strike. That commitment is exactly what the Rusher needs at 180cm range.

**Approach animation (optional but strong):** `RifleMega` has no dedicated melee-approach clip, but the sprint-charge into melee is conveyed by the sprint locomotion + the strike landing mid-sprint — no separate "lunge" clip strictly required. If a lunge is wanted, the knife `Anim_Character_Knife_AttackLeft/Right` are the closest (full-body, arms extended), but they read as a knife thrust, not a rifle butt.

---

## 4. What Exists Per Clip

### [QUANTUM-NATIVE] — already on SK_Military_Character_Skeleton, no retarget needed

All clips in `Extraction/Content/QuantumCharacter/Retarget/`:
- Idle: `Idle/Mil_Rifle02_St_Idle00.uasset`
- Walk 8-dir: `Idle/Mil_Rifle02_St_Walk_*_IPC.uasset` (8 clips)
- Run 8-dir: `Idle/Mil_Rifle02_St_Run_*_IPC.uasset` (8 clips)
- Crouch idle: `Crouch/Mil_Rifle_Cr_Idle00.uasset`
- Crouch walk 8-dir: `Crouch/Mil_Rifle_Cr_Walk_*_IPC.uasset` (8 clips)
- Cover idles: `Idle/Mil_anim_CoverDown_Idle_Left/Right.uasset`
- Cover montages: `Idle/AM_Crouch_Cover_Left/Right.uasset`
- Aim offset poses: `AO/Mil_Rifle02_St_Aim_*.uasset` (17 poses)
- AO blendspace: `AO_Companion_Rifle02.uasset`
- Locomotion BS: `BS_Companion_Rifle02_Locomotion.uasset`
- Fire loop: `Combat/AM_Companion_Fire_Loop.uasset` → source `Combat/Rifle01_St_Shoot_Auto_Loop.uasset`
- Fire single: `Combat/AM_Companion_Fire_Single.uasset` → source `Combat/Rifle01_St_Shoot_Auto_Single.uasset`
- Reload: `Combat/AM_Companion_Reload.uasset` → sources `Combat/Mil_Rifle03_St_Reload_Auto.uasset`, `Combat/Rifle01_St_Reload_Auto.uasset`
- Hit-react: `Combat/AM_Companion_HitReact_Aim.uasset`, `Combat/Mil_Rifle01_St_Hit_Light_F.uasset`, `Combat/Mil_Rifle02_St_Hit_Light_F.uasset`, `Combat/AM_Companion_hit_Idle.uasset`

### [RETARGET-AVAILABLE] — on Rifle_Mannequin_A_Skeleton; retarget via RTG_RifleMannequin_to_Military

All clips in `Extraction/Content/RifleMega_MocapAnimPack/AnimationsFBX/`:

**Melee strikes (the Rusher's gap):**
- `Rifle_Melee/Rifle01_Melee_Hard_01.uasset` — hard strike variant 1
- `Rifle_Melee/Rifle01_Melee_Hard_02.uasset` — hard strike variant 2
- `Rifle_Melee/Rifle01_Melee_Light_01.uasset` — light strike variant 1
- `Rifle_Melee/Rifle01_Melee_Light_02.uasset` — light strike variant 2
- `Rifle_Melee/Rifle02_Melee_Hard_03/04.uasset`, `Rifle03_Melee_Hard_05/06.uasset` — alt style sets
- `Rifle_Melee/Rifle_Melee_Hit_01–06.uasset` — hit-land poses (use as additive blends or montage tail)

**Sprint:**
- `Rifle_Styly01_St/Rifle01_LocomotionSet/Rifle01_St_Sprint_IPC.uasset`
- `Rifle_Styly01_St/Rifle01_LocomotionSet/Rifle01_St_Sprint.uasset`

**Death:**
- `Rifle_Styly01_St/Rifle01_Hit_DeathSet/Rifle01_St_Death_B/F/L/R.uasset` (4 dir)
- Other style sets have equivalent death clips (Rifle02, Rifle03, Cr variants)

**Caveat — skeleton mismatch:** RifleMega clips are on `Rifle_Mannequin_A_Skeleton` (found at `RifleMega_MocapAnimPack/Demo/Models/Character/Mesh/Rifle_Mannequin_A_Skeleton.uasset`). The retargeter `RTG_RifleMannequin_to_Military` already exists in the project (`QuantumCharacter/Retarget/RTG_RifleMannequin_to_Military.uasset`), so retargeting is a batch operation in-editor with no new IK rig authoring required.

### [FP-ONLY — weak for TP use]

Kit clips in `ProceduralFPSKIT/Character/Animations/WeaponAnims/`:
- `Rifle/Anim_Arms_AmericanRifle_Melee.uasset` — FP arms-only; no legs; will look floaty on TP skeleton
- `Pistol/Anim_Arms_Pistol_Melee.uasset` — same issue
- `Knife/Anim_Character_Knife_AttackLeft/Right.uasset` — full-body TP (character, not arms); on SK_Mannequin → retarget via RTG_RifleMannequin_to_Military is the wrong retargeter (Mannequin→Military); would need a direct Mannequin→Quantum retarget; also reads as a knife attack, not SMG butt
- `FistAnims/Anim_Character_AttackL/R.uasset` — full-body TP, no prop dependency; usable fallback but no weapon commitment

---

## 5. Gaps

| Gap | Tag | Priority |
|---|---|---|
| **MeleeMontage is null** — `PlayMeleeMontage()` bails immediately; 0 visual on the Rusher's 35dmg strike | [MUST-ASSIGN] | P0 — archetype's signature is invisible |
| **No retargeted gun-butt clip exists** — RifleMega melee clips are [RETARGET-AVAILABLE] but have not been run through `RTG_RifleMannequin_to_Military` | [MUST-RETARGET] | P0 — blocking the above |
| **No sprint animation retargeted** — `Rifle01_St_Sprint_IPC` exists in RifleMega but not in `QuantumCharacter/Retarget/`; 600 cm/s charge reads as skating | [MUST-RETARGET] | P1 — core Rusher read |
| **DeathMontage is null** — `PlayDeathMontage()` bails; enemy dies to ragdoll with no transition montage | [HAVE-RETARGET] — RifleMega death clips exist; same retargeter | P2 — shared gap with all archetypes |
| **No dedicated fire-while-sprinting blend** — firing during `RusherAdvance` plays `FireMontage` over whatever locomotion is blending; acceptable (BF/COD soldiers do this) but no explicit sprint+fire blend exists | [ACCEPTABLE-AS-IS] unless sprint upper body pops | P3 |
| **No enemy-side hit-react during combat** — `PlayHitReactMontage` is suppressed when `bInCombat` (correct for cover-peekers); Rusher never uses cover so hit-react never plays at all once he's in Combat. Functional but the "getting shot while charging" read is zero | [DESIGN-DECISION] — may want to relax the combat gate for Rusher specifically | P3 |

---

## 6. Wiring Plan

### Signal chain (already correct — C++ and delegate are live)

```
BTTask_EnemyMelee::ExecuteTask()
  → AEnemyCharacter::PerformMelee(Target)          // range check, cooldown, ApplyDamage
  → OnMeleePerformed.Broadcast()                    // EnemyCharacter.h:105
  → UEnemyAnimInstance::HandleMeleePerformed()      // EnemyAnimInstance.cpp:239
  → PlayMeleeMontage()                              // EnemyAnimInstance.cpp:215
  → Montage_Play(MeleeMontage)                      // plays if !nullptr
```

Nothing in this chain needs code changes. **The only work is in the editor:**

### Step 1 — Retarget RifleMega melee clips to Quantum

In-editor via the Retarget editor or a Python batch:
- Source skeleton: `RifleMega_MocapAnimPack/Demo/Models/Character/Mesh/Rifle_Mannequin_A_Skeleton`
- Retargeter: `QuantumCharacter/Retarget/RTG_RifleMannequin_to_Military`
- Target clips: `Rifle01_Melee_Hard_01`, `Rifle01_Melee_Hard_02`, `Rifle01_Melee_Light_01`, `Rifle01_Melee_Light_02`
- Output folder: `QuantumCharacter/Retarget/Combat/` (mirror the pattern used for fire/reload/hit-react)

Retarget the sprint clip at the same time: `Rifle01_St_Sprint_IPC` → output to `QuantumCharacter/Retarget/Idle/`

### Step 2 — Create montage AM_Enemy_Melee_Gunbutt

- Skeleton: `SK_Military_Character_Skeleton`
- Slot: `DefaultSlot` (same slot used by fire/reload montages — they cannot collide because melee fires only when weapon is downed at ≤180cm, i.e. `bIsFiring` is false by the time melee triggers)
- Content: sequence `Rifle01_Melee_Hard_01` (retargeted) as the default section; optionally add `Rifle01_Melee_Light_01` as a "Light" section for variety
- BlendIn: 0.10s, BlendOut: 0.15s
- No loop — single play, return to locomotion

### Step 3 — Assign to ABP_Enemy_Grunt (or per-archetype override)

Open `ABP_Enemy_Grunt` → Class Defaults → `MeleeMontage` → assign `AM_Enemy_Melee_Gunbutt`.

Note: all 7 archetypes share ABP_Enemy_Grunt. If this montage looks wrong on the other 6 (who have `bCanMelee=false` and will never call `PerformMelee`), it still never plays on them — safe to assign globally. If per-archetype ABPs are ever split out, the Rusher's ABP child overrides this.

### Step 4 — Sprint blendspace slot (optional, recommended)

Add `Rifle01_St_Sprint_IPC` (retargeted) to `BS_Companion_Rifle02_Locomotion` at the high-speed end of the Speed axis (above the existing run gate, e.g. Speed ≥ 550 cm/s). The Rusher's 600 cm/s combat speed will naturally sample it; other archetypes at ≤400 cm/s will never reach that gate.

Alternatively: add a `bIsSprinting` bool to `UEnemyAnimInstance` driven by `Speed >= 500.f`, and branch in the ABP to a single sprint BS feeding only `Rifle01_St_Sprint_IPC`. Simpler than a BS-axis extension for one archetype.

### Slot collision note

`Montage_Play(MeleeMontage)` runs on `DefaultSlot`. The fire montage check in `PlayFireMontage()` is `Montage_IsPlaying(FireMontage)` — it will not block melee. Melee is called only at ≤180cm after the weapon is flagged down, so `bIsFiring` will be false at the point of trigger. No slot conflict.

---

## Summary of State Today

**Archetype:** Rusher — SMG, 60HP, bFearless, bCanMelee, 600cm/s charge, melee 35dmg/180cm/1.5s cooldown, `BTTask_EnemyMelee` in the combat subtree post-advance.

**Three things wrong, ordered by visibility:**

1. **No melee/CQC anim** — `MeleeMontage = nullptr`; the archetype's defining close-combat beat produces zero visual feedback. Delegate chain and C++ are fully wired and correct; the gap is entirely in-editor.

2. **No sprint** — Rusher charges at 600cm/s but the locomotion blendspace tops out at run; he skates. `Rifle01_St_Sprint_IPC` is [RETARGET-AVAILABLE] via the existing `RTG_RifleMannequin_to_Military`.

3. **No death montage** — shared gap with all archetypes; RifleMega death clips exist and use the same retargeter.

**Gaps tagged:**
- `MeleeMontage` null → [MUST-ASSIGN] (P0)
- Gun-butt clips not retargeted → [MUST-RETARGET] (P0, blocks above)
- Sprint clip not retargeted → [MUST-RETARGET] (P1)
- Death montage clips available but not retargeted or assigned → [HAVE-RETARGET] (P2)
