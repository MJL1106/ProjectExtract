# Enemy Anim Map — Sniper Archetype

_Read-only mapping produced 2026-06-18. No code or assets were modified._

---

## 1. Weapon & Rig

| Property | Value |
|---|---|
| Weapon BP | `Content/Core/Weapons/BP_EnemySniperRifle.uasset` |
| Weapon mesh skeleton | `SK_Sniper_Frame` (InfimaGames/ModernSniper) |
| Character skeleton | `SK_Military_Character_Skeleton` (QuantumCharacter) |
| Anim BP | `ABP_Enemy_Grunt` (shared by all 7 archetypes) |
| Anim instance class | `UEnemyAnimInstance` |
| DA | `Content/Enemy/AI/DA_Enemy_Sniper.uasset` — `bIsSniper=true`, `BurstCount=1`, `bIsAutomatic=false`, 90 dmg, 5 mag, 3.5s reload, `SniperAimTelegraphTime=2.0`, `SniperShotCooldown=4.0`, `SniperRelocateAfterShots=2` |
| Combat BT | `Content/Enemy/AI/BT_EnemyCombat_Sniper.uasset` → `BTTask_SniperNest` |
| Retarget IK rig | `IK_MilitaryCharacter` (QuantumCharacter/Retarget) |
| Active retargeters | `RTG_RifleMannequin_to_Military`, `RTG_RifleAutoMannequin_to_Military` |

---

## 2. Current State

### 2a. Fire path — code

`BTTask_SniperNest` (`Private/AI/Tasks/BTTask_SniperNest.cpp`, lines 221-239) calls `Weapon->StartFiring(); Weapon->StopFiring();` on the same frame (`ReadyToFire` phase). Because the weapon fires once (single-shot, non-auto) and immediately stops, the `bIsFiring` rising-edge that `UEnemyAnimInstance::NativeUpdateAnimation` watches (`EnemyAnimInstance.cpp` lines 136-147) is likely missed — the flag is set and cleared within a single tick, and the auto-trigger logic only sees `bIsFiring && !bPrevIsFiring`.

The correct path for this weapon type is `HandleWeaponFired()`, which is bound to `AWeaponBase::OnWeaponFired` via a lazy `TWeakObjectPtr<AWeaponBase> BoundFireWeapon` delegate (`EnemyAnimInstance.cpp` lines 116-124). When `OnWeaponFired` broadcasts, `HandleWeaponFired()` (`EnemyAnimInstance.cpp` lines 223-232) plays `SingleFireMontage` — **provided `SingleFireMontage` is assigned on the ABP**.

### 2b. The open editor step — SingleFireMontage is null

`SingleFireMontage` is declared `EditDefaultsOnly` on `UEnemyAnimInstance` (`EnemyAnimInstance.h` line 132). It is **not assigned in C++** and has no fallback. Until a designer opens `ABP_Enemy_Grunt` → Class Defaults → Enemy | Animation | Montages → `SingleFireMontage` and assigns a montage, every single-fire weapon (sniper, any future shotgun) produces **no fire animation**. The memory note from 2026-06-18 confirms this step is open.

The suggested assignment from the memory file is `AM_Companion_Fire_Single` (`Content/QuantumCharacter/Retarget/Combat/AM_Companion_Fire_Single.uasset`). This is a retargeted rifle single-fire animation — it works mechanically but shows no bolt cycle (see §5).

### 2c. Current anim set in use (all archetypes, rifle-derived)

All enemies today animate as generic rifle users. The Quantum Retarget folder contains:

| Slot | Asset | Notes |
|---|---|---|
| Standing locomotion | `BS_Companion_Rifle02_Locomotion` + `Mil_Rifle02_St_*` IPCs | 8-directional walk/run |
| Crouch locomotion | `BS_Companion_Rifle_Crouch` + `Mil_Rifle_Cr_Walk_*` IPCs | — |
| Aim offset (standing) | `AO_Companion_Rifle02` (17 poses: `Mil_Rifle02_St_Aim_*`) | Drives `AimPitch`/`AimYaw` |
| Standing idle | `Mil_Rifle02_St_Idle00` | — |
| Crouch idle | `Mil_Rifle_Cr_Idle00` | — |
| Cover (crouch) | `AM_Crouch_Cover_Left/Right`, `Mil_anim_CoverDown_Idle_*` | — |
| Fire loop | `AM_Companion_Fire_Loop` / `Rifle01_St_Shoot_Auto_Loop` | Auto-trigger on `bIsFiring` rising edge |
| Fire single | `AM_Companion_Fire_Single` / `Mil_Rifle03_St_Shoot_Auto_Single` | **Intended `SingleFireMontage` target — not yet assigned** |
| Reload | `AM_Companion_Reload` / `Mil_Rifle03_St_Reload_Auto` / `Rifle01_St_Reload_Auto` | Auto-trigger on `bIsReloading` rising edge |
| Hit-react | `AM_Companion_HitReact_Aim`, `Mil_Rifle01/02_St_Hit_Light_F`, `AM_Companion_hit_Idle` | Suppressed during combat/firing |
| Death | Assigned in ABP (not in Retarget folder — kit-sourced montage) | — |

---

## 3. Required Anim Set — Sniper Archetype

Goal: COD/BF-style bolt-action marksman. Long still hold at sightlines, one deliberate shot, visible bolt-cycle after each round, distinct slow reload.

| Slot | Description | Priority |
|---|---|---|
| **Idle — relaxed patrol** | Sniper rifle held low-ready, two-handed; not shouldered. Used during patrol/Unaware. | Medium (current rifle idle acceptable short-term) |
| **Idle — scoped hold (long-aim)** | Rifle raised and shouldered, eye to scope. Held during the 2s `SniperAimTelegraphTime`. | HIGH — visually identifies the sniper and telegraphs the shot |
| **Aim offset** | Full upper-body yaw+pitch while scoped-hold idle is the base. Can reuse the existing rifle AO if the grip pose is close. | Medium |
| **Locomotion** | Walk/run blendspace. Reusing rifle locomotion is acceptable — sniper spends most time stationary. | Low |
| **Fire single + BOLT-CYCLE** | Single percussive fire recoil **followed immediately** by a bolt-pull-and-push cycle (right hand lifts, cycles bolt, returns to grip). This is the primary missing visual. Duration ~0.8–1.2s total. | **CRITICAL — the defining sniper visual** |
| **Reload — box magazine** | 5-round box mag: extract magazine, insert fresh, chamber first round. 3.5s to match DA reload. Distinct from auto-rifle speed reload. | HIGH |
| **Hit-react** | Upper-body flinch while gripping rifle. Can reuse rifle hit-react if retargeted. | Low |
| **Death** | Can reuse shared death montage. | Low |

---

## 4. What Exists Per Clip

### [QUANTUM-NATIVE] — on SK_Military_Character_Skeleton, ready to use

These are already retargeted to the Quantum skeleton and wired into `ABP_Enemy_Grunt`:

- `AM_Companion_Fire_Single` — rifle single fire. Works as `SingleFireMontage` stopgap. No bolt cycle. Upper-body plays the shot but hands return to rifle grip immediately.
- `AM_Companion_Fire_Loop` — sustained auto-fire loop. Snipers do not use this (loop never starts; single-shot weapon stops firing before the rising edge is latched).
- `AM_Companion_Reload` — rifle speed reload. Works mechanically but speed (~1.5s) and motion (fast mag swap) do not match a bolt-action 3.5s reload.
- `Mil_Rifle03_St_Shoot_Auto_Single` / `Rifle01_St_Shoot_Auto_Single` — raw retargeted single-fire source clips. No bolt.
- `Mil_Rifle02_St_Idle00`, `AO_Companion_Rifle02` — standing idle + full aim offset grid. Usable as scoped-hold base if the pose is close enough; the rifle raised-aim angle will look plausible from a distance.
- Full locomotion + crouch blendspaces — reusable as-is.

### [INFIMA-TP-POSE] — SK_Mannequin, single pose only

`Content/InfimaGames/ModernGunsBundle/_Demo/Animations/A_TPP_Sniper_Idle_Pose_Example.uasset`

This is a **static idle pose** for the Infima sniper on SK_Mannequin (UE5 standard Mannequin). It is a reference for how the sniper grip should look — NOT a playable TP animation on the Quantum skeleton. To use it, retarget via `RTG_Mannequin` (the Infima demo includes `IK_Mannequin` + `RTG_Mannequin`). The result would be a one-frame pose montage for a "scoped hold" idle.

### [KIT-FPS-SNIPER] — SK_Mannequin (FP arms), NOT usable on TP character

`Content/ProceduralFPSKIT/Character/Animations/WeaponAnims/Sniper/`:

| Asset | Description |
|---|---|
| `Anim_Arms_Sniper__Idle` | FP arms idle — sniper hold |
| `Anim_Arms_Sniper_FireLong` | FP arms — fire + bolt cycle (long) |
| `Anim_Arms_Sniper_FireShort` | FP arms — fire + bolt cycle (short) |
| `Anim_Arms_Sniper_Reload` | FP arms — reload |
| `Anim_Arms_Sniper_Reload_Empty` | FP arms — reload from empty |
| `Anim_Arms_Sniper_Pose` | FP arms — idle pose |
| `Anim_Arms_SniperBase` | Base animation sequence |
| Weapon channel: `Anim_Weapon_Sniper_FireLong` | Weapon-only channel fire+bolt |

These are **FP arm animations on `SK_Mannequin` (first-person arms skeleton)**. They cannot be retargeted to `SK_Military_Character_Skeleton` — different bone count, no body, FP-only proportions. The weapon-channel clips (`Anim_Weapon_Sniper_FireLong`) animate only the `SK_Sniper_Skeleton` weapon mesh, not the character.

**Caveat:** `Anim_Arms_Sniper_FireLong` / `FireShort` contain the bolt-cycle motion in the hands. This motion data is the reference for what a TP bolt-cycle should look like, but the clips themselves are not retargetable.

### [INFIMA-BIPOD] — weapon-mesh only

`Content/InfimaGames/ModernGunsBundle/ModernSniper/Animations/A_Sniper_Bipod_*.uasset` — these animate the weapon's bipod component, not the character.

### [RIFLEANIMS-PACK] — SK_Mannequin (TP), retargetable

`Content/ImportedAssets/RifleAnims/` contains a full TP rifle anim set on `SK_Mannequin`:

- `AS_Rifle_Fire`, `AS_Rifle_Fire_Aim` — rifle single-fire poses
- `AM_Rifle_Fire`, `AM_Rifle_Fire_Aim` — montages
- `AS_Rifle_ReloadEmpty`, `AS_Rifle_ReloadLoaded` — two reload variants
- Full blendspaces: standing idle/walk/jog/run + crouch + aim

These are on SK_Mannequin and retargetable via the existing `RTG_RifleMannequin_to_Military` retargeter. They provide a **cleaner rifle fire/reload reference** but still contain no bolt-cycle.

---

## 5. Gaps

### [MUST-SOURCE] — True bolt-cycle TP animation

No asset in the project provides a third-person bolt-pull-and-push cycle for `SK_Military_Character_Skeleton`. The kit's FP fire clips show the correct motion but cannot be retargeted. This is the primary creative gap.

Options, in order of effort:
1. **Marketplace / source a TP bolt-action sniper anim pack** — search for "bolt action sniper" on Fab/Marketplace for SK_Mannequin; retarget via `RTG_RifleMannequin_to_Military`. Several exist (e.g. "Military Animations", "Shooter Animation Pack").
2. **Animate in UE5 (Control Rig / Sequencer)** — extract the rifle single-fire clip, layer a bolt-pull curve on top via Control Rig additive pose. Feasible but authoring work.
3. **Fake it with an additive right-hand pose** — author a short right-hand lift + forward-push as an additive layer montage on top of the fire clip. Lower fidelity but very fast.

### [MUST-SOURCE] — Scoped-hold long-aim idle

No TP "rifle raised and eye to scope" idle exists on the Quantum skeleton. The existing `Mil_Rifle02_St_Idle00` is a low-ready relaxed patrol idle, not a scope-to-eye pose.

The `A_TPP_Sniper_Idle_Pose_Example` (Infima, SK_Mannequin) is the closest existing reference for the pose — retarget via `RTG_Mannequin` → `RTG_RifleMannequin_to_Military` to get a static pose on the Quantum skeleton, then build a montage from it.

### [MUST-SOURCE] — Sniper-specific reload animation

The current `AM_Companion_Reload` is a speed AR mag swap (~1.5s). The DA has `ReloadTime=3.5s` for a 5-round box mag. Even with `PlayRate` adjustment the motion reads wrong. A distinct sniper reload anim (box/stripper clip insertion + chamber) is needed. Can be sourced from the same TP bolt-action pack as the bolt-cycle.

### [HAVE-RETARGET] — Fire recoil base (missing only the bolt)

`AM_Companion_Fire_Single` / `Mil_Rifle03_St_Shoot_Auto_Single` exist on the Quantum skeleton. Assigning one as `SingleFireMontage` immediately gives a fire reaction. The bolt-cycle would be layered/appended on top once sourced.

### [HAVE-RETARGET] — Aim offset (reusable)

`AO_Companion_Rifle02` (17-pose grid) is already on the Quantum skeleton. For a scoped-hold base pose the upper-body yaw/pitch tracking will look reasonable even without a dedicated sniper AO. Only needed if the sniper grip diverges enough from rifle grip to create obvious clipping.

---

## 6. Wiring Plan

### Step 1 — Unblock fire anim now (1 editor step)

Open `ABP_Enemy_Grunt` → Class Defaults → Enemy | Animation | Montages → `SingleFireMontage`. Assign `AM_Companion_Fire_Single`. This immediately plays a rifle single-fire reaction on every sniper shot via the `HandleWeaponFired` delegate path. No C++ required.

**Why this unblocks:** `HandleWeaponFired()` (`EnemyAnimInstance.cpp` line 230) plays `SingleFireMontage` only when the loop `FireMontage` is not already playing. For the sniper (non-auto, `StartFiring+StopFiring` same frame) the loop montage never starts, so `HandleWeaponFired` is the sole fire-anim trigger. Without `SingleFireMontage` assigned, the call at line 231 is a no-op.

### Step 2 — Per-weapon montage selection (C++ + editor)

The current system uses a single `SingleFireMontage` on the shared ABP. For per-archetype overrides two approaches:

**Option A — DA-driven montage refs (preferred, data-driven):**
Add to `UEnemyArchetypeData`:
```cpp
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
TObjectPtr<UAnimMontage> SingleFireMontage;

UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation")
TObjectPtr<UAnimMontage> ReloadMontage;
```
In `HandleWeaponFired()` and `PlayReloadMontage()`, check `OwningEnemy->GetArchetypeData()` for a non-null override before falling back to the ABP-assigned montage. The sniper's DA (`DA_Enemy_Sniper`) then holds sniper-specific montage refs; other archetypes leave them null and fall back to the shared ABP values.

**Option B — separate child ABP per weapon class:**
Create `ABP_Enemy_Sniper` as a child of `ABP_Enemy_Grunt`, override only `SingleFireMontage` and `ReloadMontage` in Class Defaults. Assign `ABP_Enemy_Sniper` on `BP_Enemy_Sniper`'s mesh component. No C++ required.

Option A is preferred (single ABP, data-driven). Option B is faster to ship today and eliminates C++ work.

### Step 3 — Long-aim idle signal (C++ + BT)

The scoped-hold idle should play during `BTTask_SniperNest` `Telegraph` phase. Two approaches:

**Recommended:** Add a `bSniperScoped` bool to `UEnemyAnimInstance` (BlueprintReadOnly, Transient). `BTTask_SniperNest` sets it via a `UEnemyAnimInstance` helper call at `BeginTelegraph` / `CancelTelegraph`. `ABP_Enemy_Grunt` reads `bSniperScoped` to blend from the patrol locomotion state into a "Sniper Scoped" sub-state running the scoped-hold idle montage (upper-body slot). The existing aim-offset continues to drive yaw/pitch on top.

A `PlayRate`-scaled transition (0.3s) when the flag rises gives a natural shouldering motion even before a bespoke scoped-idle clip is sourced.

### Step 4 — Reload duration match

When a sniper-specific reload clip is available, assign it via the mechanism in Step 2. Until then: `PlayReloadMontage` accepts a `PlayRate` param. The shared AR reload is ~1.5s; setting `PlayRate = 0.43f` stretches it to 3.5s. Not ideal visually but keeps the timing correct mechanically. This is a temporary measure only.

### Step 5 — Bolt-cycle integration

Once a TP bolt-cycle clip is sourced (retargeted to `SK_Military_Character_Skeleton`), create a montage that sequences: fire recoil (0–0.3s) → bolt pull (0.3–0.7s) → bolt push (0.7–1.0s). Assign as `SingleFireMontage` (DA or child-ABP). The `OnWeaponFired` delegate fires immediately when the weapon fires, so the montage plays at the exact shot moment. No change to `HandleWeaponFired` required.

---

## Summary of Priorities

1. **Assign `SingleFireMontage = AM_Companion_Fire_Single` on `ABP_Enemy_Grunt`** — 1 click, unblocks fire anim immediately. [UNBLOCKED NOW]
2. **Source a TP bolt-action sniper fire + bolt-cycle clip** (Fab/Marketplace, SK_Mannequin, retarget via `RTG_RifleMannequin_to_Military`). [MUST-SOURCE]
3. **Source or retarget a scoped-hold idle** (retarget `A_TPP_Sniper_Idle_Pose_Example` via `RTG_Mannequin`→Military as a starting pose). [MUST-SOURCE]
4. **Per-archetype montage override** — Option B (child ABP) is fastest; Option A (DA refs) is cleaner. [C++ or editor-only]
5. **Source sniper-specific reload** from the same TP bolt-action pack as the bolt-cycle. [MUST-SOURCE]
