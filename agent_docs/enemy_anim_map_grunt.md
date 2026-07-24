# Enemy Anim Map — Grunt (Baseline)

**Date:** 2026-06-18  
**Branch:** Enemies  
**Purpose:** Authoritative animation mapping for the Grunt archetype. This is the BASELINE — all other archetypes diverge from this reference. Gaps tagged [BASELINE] are inherited by every archetype.

---

## 1. Weapon & Rig

- **Weapon:** `BP_EnemyAssaultRifle` / `DA_AssaultRifle` — full-auto, two-handed shouldered, 10 rps, 30-round magazine
- **Mesh skeleton:** `SK_Military_Character_Skeleton` (`/Game/QuantumCharacter/Mesh/`) — Quantum military character, mannequin bone hierarchy
- **Anim BP:** `ABP_Enemy_Grunt` (`/Game/Enemy/AI/`) — shared by all 7 archetypes
- **Anim instance class:** `UEnemyAnimInstance`
- **IK rig:** `IK_MilitaryCharacter` + retargeters `RTG_RifleMannequin_to_Military` / `RTG_RifleAutoMannequin_to_Military` — retarget source is Mannequin (SK_Mannequin); these convert the Quantum retarget folder assets
- **Note:** All assets in `/QuantumCharacter/Retarget/` are already on SK_Military_Character_Skeleton (retargeted). The `RifleMega_MocapAnimPack` uses `Rifle_Mannequin_A_Skeleton` — NOT compatible without a retarget pass.

---

## 2. Current State

**What the C++ drives today** (`EnemyAnimInstance.h/.cpp`):

| Signal | Source | Where driven |
|---|---|---|
| `Speed`, `Direction`, `NormalizedSpeed` | `MovementComponent->Velocity` | `NativeUpdateAnimation` every frame |
| `bIsCrouched` | `OwningEnemy->bIsCrouched` | Per frame |
| `bIsAlive` | `HealthComponent->IsAlive()` | Per frame |
| `AimPitch`, `AimYaw`, `bIsAiming` | `GetAIAimLocation` / `GetAIAimTarget` | Per frame via `UpdateAimOffset` |
| `bIsFiring` | `Weapon->IsFiring()` | Per frame |
| `bIsReloading` | `Weapon->IsReloading()` | Per frame |
| `bInCombat` | `AwarenessComponent->GetAwarenessState() == Combat` | Per frame |
| `FireMontage` | Rising edge of `bIsFiring` → `PlayFireMontage()` | Auto-triggered in `NativeUpdateAnimation` (cpp:138) |
| `SingleFireMontage` | `OnWeaponFired` delegate → `HandleWeaponFired()` | Delegate, per-shot (cpp:223) |
| `ReloadMontage` | Rising edge of `bIsReloading` → `PlayReloadMontage()` | Auto-triggered in `NativeUpdateAnimation` (cpp:144) |
| `HitReactMontage` | `OnHitReact` delegate → `HandleHitReact()` | Suppressed during combat/aiming (cpp:204) |
| `DeathMontage` | `PlayDeathMontage()` — must be called externally | No auto-trigger in C++; called by `HandleTakedown` fallback |
| `MeleeMontage` | `OnMeleePerformed` delegate → `HandleMeleePerformed()` | Delegate |
| `TakedownReactionMontage` | `OnTakedownExecuted` delegate → `HandleTakedown()` | Falls back to DeathMontage if unassigned (cpp:247) |

**What is wrong / incomplete today:**

1. **`DeathMontage` has no auto-trigger path from actual death.** `AEnemyCharacter` deaths go straight to ragdoll (`SetSimulatePhysics` on the mesh) — `PlayDeathMontage()` is never called from the death flow. The montage slot exists, the method exists, but nothing routes enemy death through it; enemies ragdoll instantly without a death pose. [BASELINE]

2. **`HitReactMontage` is suppressed whenever `bInCombat = true`.** `PlayHitReactMontage` gates on `bIsFiring || bIsAiming || bInCombat` (cpp:204). Since the grunt is almost always in Combat state when being shot at, hit reactions are functionally invisible in most encounters. The Quantum retarget folder only has one `AM_Companion_HitReact_Aim` — no directional variants. [BASELINE]

3. **Single `ReloadMontage` — no empty-magazine variant.** The DA forces auto-reload; the C++ only has one `ReloadMontage` field. An empty-reload (hand reaches all the way to belt, charging handle pulled) is more believable for a combat soldier. No empty-reload clip exists in the Quantum retarget set. [BASELINE]

4. **Patrol locomotion uses the same aimed blendspace as combat.** There is no relaxed-carry idle or patrol walk. `bIsAiming` and `bInCombat` are available as signals, but the ABP has no separate locomotion path for the Unaware/patrol state — the enemy walks patrol routes with a shouldered-rifle stance. [BASELINE]

---

## 3. Required Anim Set

| Slot | Description | Notes |
|---|---|---|
| **Patrol idle (relaxed)** | Low-ready or slung carry, standing idle with fidget variants | Distinct from combat idle — patrol enemies should not look like they're already aiming |
| **Alert idle (shouldered)** | Weapon raised, standing still — used in Suspicious/Searching states and cover pause | |
| **Aim offset (AO)** | 17-pose 2D grid: pitch ±90°, yaw ±90° | Drives upper-body aim direction layer; existing `AO_Companion_Rifle02` covers this |
| **Locomotion — walk, 8-directional** | Walk blendspace (F/B/L/R + diagonals) for patrol speed 200 cm/s | Existing `BS_Companion_Rifle02_Locomotion` + `St_Walk_*` IPC clips cover this |
| **Locomotion — run, 8-directional** | Run blendspace for combat speed 400 cm/s | Existing run IPC clips cover this |
| **Locomotion — crouch, 8-directional** | Crouch walk for cover repositioning | Existing `BS_Companion_Rifle_Crouch` + `Mil_Rifle_Cr_Walk_*` cover this |
| **Crouch idle** | Stationary crouch hold in cover | `Mil_Rifle_Cr_Idle00` exists |
| **Cover idle L/R** | Behind low wall, peek pose | `Mil_anim_CoverDown_Idle_Left/Right` exist |
| **Fire loop** | Full-auto sustained fire loop, aimed standing | `AM_Companion_Fire_Loop` / `Rifle01_St_Shoot_Auto_Loop` exist |
| **Fire single** | Single-shot recoil kick, per `HandleWeaponFired` | `AM_Companion_Fire_Single` / `Rifle01_St_Shoot_Auto_Single` exist |
| **Reload (tactical)** | Partial mag swap, hand moves but not fully extended | `AM_Companion_Reload` / `Rifle01_St_Reload_Auto` exist |
| **Reload (empty)** | Mag drops, charging handle pulled | Not present in Quantum retarget set [MUST-SOURCE] |
| **Hit-react — front** | Torso flinch forward | `Mil_Rifle02_St_Hit_Light_F` / `Rifle01_St_Hit_Light_F` exist |
| **Hit-react — directional (B/L/R)** | Hit from behind, left, right | `Rifle01/02/03_St_Hit_Light_B/L/R` exist in RifleMega (need retarget) |
| **Death — directional (F/B/L/R)** | Falling animation matched to shot direction | `Rifle01/02/03_St_Death_F/B/L/R` exist in RifleMega (need retarget) |
| **Takedown reaction** | Brief stumble/stagger before ragdoll delay | Not present anywhere; falls back to DeathMontage |

---

## 4. What Exists on Disk

### Quantum retarget set — already on SK_Military_Character_Skeleton [QUANTUM-NATIVE]

**Path prefix:** `Extraction/Content/QuantumCharacter/Retarget/`

| Category | Asset(s) |
|---|---|
| Aim offset AO | `AO_Companion_Rifle02` (references 17 `Mil_Rifle02_St_Aim_*` poses in `AO/`) |
| Stand locomotion walk | `Idle/Mil_Rifle02_St_Walk_{F,B,L,R,45L,45R,135L,135R}_IPC` (8 clips) |
| Stand locomotion run | `Idle/Mil_Rifle02_St_Run_{F,B,L,R,45L,45R,135L,135R}_IPC` (8 clips) |
| Stand idle | `Idle/Mil_Rifle02_St_Idle00` |
| Stand locomotion BS | `BS_Companion_Rifle02_Locomotion` |
| Crouch locomotion walk | `Crouch/Mil_Rifle_Cr_Walk_{F,B,L,R,45L,45R,135L,135R}_IPC` (8 clips) |
| Crouch idle | `Crouch/Mil_Rifle_Cr_Idle00` |
| Crouch locomotion BS | `Crouch/BS_Companion_Rifle_Crouch` |
| Crouch reload | `Crouch/AM_Crouch_Rifle_Reload`, `Crouch/MilRifle_Cr_Reload_Auto` |
| Crouch fire loop | `Crouch/MilRifle_Cr_Shoot_Auto_Loop` |
| Cover idles | `Idle/Mil_anim_CoverDown_Idle_{Left,Right}`, `Idle/AM_Crouch_Cover_{Left,Right}` |
| Fire loop | `Combat/AM_Companion_Fire_Loop` |
| Fire single | `Combat/AM_Companion_Fire_Single` |
| Reload (tactical) | `Combat/AM_Companion_Reload`, `Combat/Mil_Rifle03_St_Reload_Auto`, `Combat/Rifle01_St_Reload_Auto`, `Combat/Rifle01_St_Shoot_Auto_Loop`, `Combat/Rifle01_St_Shoot_Auto_Single`, `Combat/Mil_Rifle03_St_Shoot_Auto_Single` |
| Hit-react aimed | `Combat/AM_Companion_HitReact_Aim` (single frontal) |
| Hit-react light | `Combat/Mil_Rifle01_St_Hit_Light_F`, `Combat/Mil_Rifle02_St_Hit_Light_F` |
| Hit idle | `Combat/AM_Companion_hit_Idle` |

**Caveat:** No patrol (relaxed) idle, no directional hit-reacts (B/L/R), no death anims, no empty reload in this set.

---

### RifleMega_MocapAnimPack — on `Rifle_Mannequin_A_Skeleton` [RETARGET NEEDED]

**Path prefix:** `Extraction/Content/RifleMega_MocapAnimPack/AnimationsFBX/`

| Category | Asset(s) |
|---|---|
| Patrol idle (relaxed low-carry) | `Rifle_Patrol/Rifle_Patrol_Idle{00–05}` (6 variants) |
| Patrol activity idles | `Rifle_Patrol/Rifle_Patrol_Act_Idle{00–05}` (6 activity break variants) |
| Patrol walk (relaxed) | `Rifle_Patrol/Rifle_Patrol_Walk`, `Rifle_Patrol/Rifle_Patrol_Walk_IPC` |
| Patrol run (relaxed) | `Rifle_Patrol/Rifle_Patrol_Run`, `Rifle_Patrol/Rifle_Patrol_Run_IPC` |
| Patrol → alert transitions | `Update1_3/React_BaseSet/Rifle_Patrol_to_St{01–03}` (3 variations), `Rifle_Act_Patrol_to_St{01–03}` |
| Alert idle (shouldered style 1–3) | `Rifle_Styly01_St/Rifle01_IdleSet/Rifle01_St_Idle{00–04}`, `Rifle_Styly02/Rifle02_St_Idle{00–03}`, `Rifle_Styly03/Rifle03_St_Idle{00–03}` |
| Stand locomotion walk (style 1) | `Rifle_Styly01_St/Rifle01_LocomotionSet/Rifle01_St_Walk_{F,B,135L,135R,45L,45R,90L,90R}_IPC` |
| Stand locomotion run (style 1) | `Rifle_Styly01_St/Rifle01_LocomotionSet/Rifle01_St_Run_{F,B,135L,135R,45L,45R}_IPC` |
| Sprint | `Rifle_Styly01_St/Rifle01_LocomotionSet/Rifle01_St_Sprint_IPC`, `Rifle_Styly03_St/Rifle03_LocomotionSet/Rifle03_Sprint_IPC` |
| Turn in-place | `Rifle_Styly01_St/Rifle01_TurnSet/Riflel01_St_Turn{90L,90R,180}`, `Update1_3/Turn180R_Set/*` |
| Aim offsets (standing) | `Rifle_AimOffsets/Rifle01/` (17 poses), `Rifle_AimOffsets/Rifle02/`, `Rifle_AimOffsets/Rifle03/` |
| Aim offsets (crouch) | `Rifle_AimOffsets/Cr/` (18 poses) |
| Fire loop auto (style 1) | `Rifle_ShootingSet/Automatic/Rifle01_St_Shoot_Auto_Loop` |
| Fire single auto (style 1) | `Rifle_ShootingSet/Automatic/Rifle01_St_Shoot_Auto_Single` |
| Fire series auto (style 1) | `Rifle_ShootingSet/Automatic/Rifle01_St_Shoot_Auto_Series` |
| Fire light/hard recoil | `Rifle_ShootingSet/Light_Hard/Rifle01_St_Shoot_{Light,Hard}`, Rifle02/03 variants |
| Reload auto (style 1 + 3) | `Rifle_ReloadingSet/Automatic/Rifle01_St_Reload_Auto`, `Rifle03_St_Reload_Auto`, `Rifle_Cr_Reload_Auto` |
| Directional hit-react light (style 1) | `Rifle_Styly01_St/Rifle01_Hit_DeathSet/Rifle01_St_Hit_Light_{F,B,L,R}` |
| Directional hit-react hard (style 1) | `Rifle_Styly01_St/Rifle01_Hit_DeathSet/Rifle01_St_Hit_Hard_{F,B,L,R}` |
| Directional death (style 1) | `Rifle_Styly01_St/Rifle01_Hit_DeathSet/Rifle01_St_Death_{F,B,L,R}` |
| Directional death (style 2) | `Rifle_Styly02_St/Rifle02_Hit_DeathSet/Rifle02_St_Death_{F,B,L,R}` |
| Directional death (style 3) | `Rifle_Styly03_St/Rifle03_Hit_DeathSet/Rifle03_St_Death_{F,B,L,R}` |
| Crouch locomotion | `Rifle_Cr/Rifle_Cr_LocomotionSet/Rifle_Cr_Run/Walk_{F,B,135L,135R,45L,45R,F_IPC…}` |
| Crouch idle (3 variants) | `Rifle_Cr/Rifle_Cr_IdleSet/Rifle_Cr_Idle{00–02}` |
| Crouch hit/death | `Rifle_Cr/Rifle_Cr_Hit_DeathSet/Rifle_Cr_Hit_{F,B,L,R}`, `Rifle_Cr_Death_{F,B,L,R}` |
| Dodge | `Rifle_Styly01_St/Rifle01_OtherAnims/Rifle01_St_Dodge_{B,L,R}` |
| Grenade throw | `Rifle_Styly01_St/Rifle01_OtherAnims/Rifle01_St_Grenade` |

**Caveat:** All RifleMega clips are on `Rifle_Mannequin_A_Skeleton`. Retarget path = same retargeter toolchain as the existing Quantum set (IK rig `IK_MilitaryCharacter` + `RTG_RifleMannequin_to_Military`). No new retargeter creation is needed — the source skeleton name matches the RTG naming convention exactly.

**Notable absence in RifleMega:** No empty-reload clip in the Automatic set (only one auto reload per style). The DoubleBarrel/ShotGun/Winchester reloads exist but are wrong action geometry for an AR.

---

### Mannequin default anims [NOT COMPATIBLE — wrong skeleton]

`Extraction/Content/Characters/Mannequins/Anims/Death/MM_Death_{Front_01,Front_02,Front_03,Back_01,Left_01,Right_01}` — on SK_Mannequin. Would require retarget to SK_Military_Character_Skeleton. Lower priority since RifleMega covers directional death.

---

### ProceduralFPSKIT — FP arms, SK_Mannequin [NOT USABLE for enemy TP]

All clips in `ProceduralFPSKIT/Character/Animations/WeaponAnims/Rifle/` are FP arm animations on SK_Mannequin. Not applicable to enemy TP. Listed for completeness.

---

## 5. GAPS

| # | Gap | Tag | Baseline? |
|---|---|---|---|
| G1 | **Patrol / relaxed idle** — no low-carry or slung idle in the current Quantum retarget set; enemies patrol in shouldered stance | [MUST-SOURCE: retarget RifleMega `Rifle_Patrol_Idle{00–05}` via RTG_RifleMannequin_to_Military] | [BASELINE] |
| G2 | **Patrol walk (relaxed)** — no weapon-down patrol walk; `BS_Companion_Rifle02_Locomotion` is the aimed run/walk BS only | [MUST-SOURCE: retarget `Rifle_Patrol_Walk_IPC`] | [BASELINE] |
| G3 | **Patrol → alert transition** — no animation bridging relaxed carry to shouldered aim | [MUST-SOURCE: retarget `Rifle_Patrol_to_St{01–03}`] | [BASELINE] |
| G4 | **Death anims** — `DeathMontage` field exists but nothing calls `PlayDeathMontage()` from the death path; enemies ragdoll with no pose transition | [HAVE-RETARGET: RifleMega `Rifle01_St_Death_{F,B,L,R}` — need retarget + C++ death hook] | [BASELINE] |
| G5 | **Directional hit-react (B/L/R)** — only frontal hit-react in the Quantum set; `HandleHitReact(EHitRegion)` receives region data but `PlayHitReactMontage()` ignores it (single montage field, no directional selection) | [HAVE-RETARGET: RifleMega `Rifle01_St_Hit_Light_{F,B,L,R}` — need retarget] | [BASELINE] |
| G6 | **Hit-react suppression in combat** — `bInCombat` gate means the player almost never sees a hit-react when it matters most; C++ fix needed alongside the directional set | [no asset gap — code gate only] | [BASELINE] |
| G7 | **Empty-reload** — `ReloadMontage` is one asset; no empty-mag variant on disk for AR (RifleMega Automatic set only has one reload) | [MUST-SOURCE: no candidate in any existing pack] | [BASELINE] |
| G8 | **Sprint animation** — Grunt combat speed 400 cm/s; no sprint clip in the Quantum retarget set (`Rifle01_St_Sprint_IPC` exists in RifleMega, needs retarget); at high speed the locomotion BS blends strangely with the walk IPC clips | [HAVE-RETARGET: RifleMega `Rifle01_St_Sprint_IPC`] | [BASELINE] |
| G9 | **Turn-in-place** — no turn animations; fast direction changes snap rather than root-motion turn; RifleMega has Turn90L/90R/180 per style | [HAVE-RETARGET: RifleMega `Riflel01_St_Turn{90L,90R,180}`] | [BASELINE] |
| G10 | **Takedown reaction montage** — `TakedownReactionMontage` field falls back to `DeathMontage` if unset; no stagger/capture clip exists anywhere on disk | [MUST-SOURCE: no candidate] | [BASELINE] |

**Priority order for the Grunt baseline:**
1. G1/G2/G3 — patrol believability (most visible during unaware state — first thing players see)
2. G4 — death transition (every kill)
3. G5/G6 — directional hit-react (every time the player lands a shot)
4. G8 — sprint (combat repositioning at 400 cm/s)
5. G9 — turn-in-place
6. G7 / G10 — polish / out-of-scope until after playtest

---

## 6. Wiring Plan

### Current ABP selection (inferred from signals available)

`ABP_Enemy_Grunt` today receives these bool/float variables from `UEnemyAnimInstance`:
- `Speed`, `Direction`, `NormalizedSpeed`, `bHasVelocity`, `bIsAccelerating`
- `bIsCrouched`, `bIsAlive`, `bIsAiming`, `bIsFiring`, `bIsReloading`, `bInCombat`
- `AimPitch`, `AimYaw`

The ABP is expected to:
- Select `BS_Companion_Rifle02_Locomotion` or `BS_Companion_Rifle_Crouch` based on `bIsCrouched`
- Layer `AO_Companion_Rifle02` on the upper body using `AimPitch`/`AimYaw`
- Play montages in a dedicated slot (upper body or full-body depending on slot name)
- Blend to ragdoll when `bIsAlive = false`

### Proposed additions to support the gap list

**New C++ signal needed — `bIsPatrolling`:**
```
bIsPatrolling = !bInCombat && AwarenessState <= Searching && !bIsAiming
```
This one bool is all the ABP needs to branch between relaxed-carry locomotion and shouldered-alert locomotion. Already computable from existing fields; no new data required.

**ABP state machine additions:**

```
Locomotion State Machine
├── Patrol (bIsPatrolling)
│     ├── Idle:  blend Rifle_Patrol_Idle00–05 by random selector
│     ├── Walk:  Rifle_Patrol_Walk_IPC (speed blended)
│     └── → Alert Transition: Rifle_Patrol_to_St01 (on !bIsPatrolling rising edge)
└── Alert (!bIsPatrolling || bInCombat)
      ├── Stand:  BS_Companion_Rifle02_Locomotion (Speed/Direction)
      ├── Crouch: BS_Companion_Rifle_Crouch
      └── Cover:  Mil_anim_CoverDown_Idle_{Left,Right}

Upper Body Aim Layer (layered over all states when bIsAiming)
└── AO_Companion_Rifle02 driven by AimPitch/AimYaw

Full-Body Montage Slot (priority queue)
├── Death:        Rifle01_St_Death_{F|B|L|R} → then set Ragdoll
├── Takedown Rx:  [sourced later] → then Ragdoll
├── Fire loop:    AM_Companion_Fire_Loop (loops while bIsFiring)
├── Fire single:  AM_Companion_Fire_Single (via SingleFireMontage)
├── Reload:       AM_Companion_Reload
└── Hit react:    Rifle01_St_Hit_Light_{F|B|L|R} (when !bInCombat)
```

**C++ `PlayHitReactMontage` change needed** (minor): pass `EHitRegion` through to select the matching directional clip rather than a single `HitReactMontage` field. Suggests expanding to four fields (`HitReactMontage_F/B/L/R`) or a `TMap<EHitRegion, UAnimMontage*>`.

**C++ `PlayDeathMontage` change needed** (minor): death direction must be inferred from the kill shot vector. Either pass the death impulse direction from `AEnemyCharacter::Die()`, or pick the four-clip set randomly if direction is unavailable (still far better than immediate ragdoll).

### Retarget work (in-engine, no C++)

1. Open `RTG_RifleMannequin_to_Military` in the editor.
2. Batch-retarget all required RifleMega clips: `Rifle_Patrol_Idle{00–05}`, `Rifle_Patrol_Walk_IPC`, `Rifle_Patrol_to_St{01–03}`, `Rifle01_St_Death_{F,B,L,R}`, `Rifle01_St_Hit_Light_{F,B,L,R}`, `Rifle01_St_Sprint_IPC`, `Riflel01_St_Turn{90L,90R,180}`.
3. Output to `/Game/Enemy/Animations/Grunt/` (keep patrol / combat / death subfolders).
4. Assign to `ABP_Enemy_Grunt` and `DA_Enemy_Grunt` / ABP default values.
