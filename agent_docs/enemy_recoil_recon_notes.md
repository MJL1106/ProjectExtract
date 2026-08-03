# Enemy Procedural Recoil — In-Engine Recon Notes (Task 1)

Read-only recon for the plan `agent_docs/enemy_firing_recoil_and_patrol_idle_plan.md`.
Editor: UE 5.7.3, ProjectExtract, VibeUE :8088 (connected, tool calls worked — no API-key block).
NO asset modified or saved. NO PIE. Date: 2026-06-19.

All names below are verbatim from live editor reflection (`unreal.BlueprintService`, asset registry, transient `SkeletalMeshComponent`).

---

## 1. AC_RecoilAnimation — `/Game/KINEMATION/Common/Recoil/AC_RecoilAnimation`

- **Asset class:** Blueprint. **Generated class:** `AC_RecoilAnimation_C`.
- **Parent class:** `/Script/Engine.ActorComponent` (stock `UActorComponent`). ParentClass == NativeParentClass == ActorComponent. **NOT a kit C++ base.** Confirmed via asset-registry `ParentClass`/`NativeParentClass` tags.

### Public function signatures (verbatim)
| Function | Parameters | Notes |
|---|---|---|
| `Init` | `(NewRecoilData: UDA_RecoilData_C*, Firerate: double)` | Stores RecoilData + clamps Firerate to >= 0.001, sets LastShotTime. |
| `Play` | `()` | Per-shot trigger. Sets `IsFiring=true`, advances the timeline/playback, picks Single vs Auto curves, sets up the transition. |
| `Stop` | `()` | Settle. Sets `IsLooping=false`, `IsFiring=false`, recomputes TimelineLength from curve ranges. |
| `SetAiming` | `(IsAiming: bool)` | Just sets the `IsAiming` bool (selects HipPivotOffset vs AimPivotOffset in Finalize). |
| `SetFireMode` | `(NewFireMode: TEnumAsByte<E_FireMode>)` | Sets fire mode (enum `E_FireMode`, a UserDefinedEnum in the same folder). |

Internal-only functions (not needed by us): `UpdateRecoilAnimation`, `ApplySmoothing`, `UpdatePlayback`, `FinalizeRecoilAnimation`, `ApplyProgress/Sway/Pushback/NoiseLayer`, `ComputeTargetData`, `CorrectAlpha`, `SetupTransition`, `GetFireDelta`, `UpateControllerRecoil`, `UpdateRecoilCompensation`, etc.

### Output variable (the one we consume)
- **`RecoilAnimation`** — type **`Transform`** (relative FTransform), category `Default`. This is the computed visual recoil pose. **Written only in `FinalizeRecoilAnimation`** (built via Make Transform / Compose Transforms around HipPivotOffset/AimPivotOffset selected by `IsAiming`, plus `SpaceRotation`).
- Separate camera-recoil output we **IGNORE for enemies:** `ControllerRecoil` (`Vector2D`), plus `TargetControllerRecoil`, `Compensation` (all category `Controller Recoil`).

### STANDALONE VERDICT — usable on the enemy with NO kit character
**YES, fully standalone.** Scanned all 26 graphs:
- **Zero interface nodes** anywhere (no `BPI_TacticalShooterCharacter`/`BPI_TacticalShooterWeapon`, no `K2Node_Message`).
- **Zero kit class references** (no `BP_TacticalShooter*`, no `BP_Weapon*`, no KINEMATION class casts).
- The only owner coupling is BeginPlay: `Get Owner -> Cast To Character -> Set OwnerCharacter` (stock UE `ACharacter` cast, not a kit class).
- `OwnerCharacter` is read in exactly **2 places**: `EventGraph` (cache) and **`UpateControllerRecoil`** — the camera-recoil path (Get Player Controller -> Get Input Mouse Delta -> Add Controller Pitch/Yaw Input on the Pawn). This is the path we DO NOT use. On an AI with no PlayerController it is a safe no-op (`Add Controller *Input` on a controllerless pawn does nothing). **The `RecoilAnimation` visual output is fully independent of `OwnerCharacter`.**

**Init/Play/Stop are driven manually** (no auto-fire loop); EventGraph Tick runs UpdatePlayback -> UpdateRecoilAnimation -> ApplySmoothing -> the Apply* pipeline -> FinalizeRecoilAnimation every frame and writes `RecoilAnimation`. So: add the component, call `Init` once on equip, `Play` per shot, `Stop` on cease-fire, `SetAiming` on aim toggle, read `RecoilAnimation` each anim Update. No kit dependency to satisfy.

---

## 2. DA_RecoilData — `/Game/KINEMATION/Common/Recoil/DA_RecoilData`

- **Asset class:** Blueprint whose generated class `DA_RecoilData_C` derives from **`/Script/Engine.PrimaryDataAsset`** (`UPrimaryDataAsset`). The tuned per-weapon profiles are DataAsset *instances* of this class.
- **The base `DA_RecoilData` CDO is ALL ZEROS / empty** (Playrate=1.0, all pivots/spread/kick = 0, `Curves` struct unassigned). It is a blank template, NOT a tuned profile.

### Fields (28 total)
- **Input Aim:** `PitchAim` (V2D), `YawAim` (V4), `RollAim` (V4), `KickAim` (V2D), `KickAimR` (V2D), `KickUpAim` (V2D)
- **Input Hip:** `PitchHip` (V2D), `YawHip` (V4), `RollHip` (V4), `KickHip` (V2D), `KickHipR` (V2D), `KickHipUp` (V2D)
- **RecoilCurves:** `Curves` (`F_RecoilCurves` — holds 4 `CurveVector*`: SingleRot, SingleLoc, AutoRot, AutoLoc)
- **Smoothing:** `SmoothRot` (V), `SmoothLoc` (V), `MultiRot` (V), `MultiLoc` (V), `bSmoothRoll` (bool)
- **General:** `HipPivotOffset` (V), `AimPivotOffset` (V), `SpaceRotation` (Rotator), `Playrate` (double), `PlaybackOffset` (double)
- **Recoil Layers (structs):** `ControllerRecoil` (`F_ControllerRecoil`), `RecoilNoise` (`F_RecoilNoise`), `Pushback` (`F_RecoilPushback`), `Progress` (`F_RecoilProgress`), `Sway` (`F_RecoilSway`)

### >>> PLAN-AFFECTING: duplicate `RD_AK105`, NOT the blank `DA_RecoilData` <<<
The kit ships **tuned per-weapon profiles named `RD_<weapon>`** (DataAsset instances of `DA_RecoilData_C`), under `/Game/KINEMATION/TacticalShooterPack/Blueprints/Recoil/<weapon>/`:
`RD_AK105`, `RD_TR15`, `RD_SRM-12`, `RD_R08`, `RD_Mk14EBR`, `RD_Herrington-11-87`, `RD_WK-11_Viper`.

`RD_AK105` (the project enemy rifle is the AK105) is **fully populated** and is the correct Task 2 baseline:
- HipPivotOffset = (2, 0, 2); AimPivotOffset = (0, 0, -3)
- SpaceRotation = (pitch 0, **yaw -90**, roll 0)  — note: matches the enemy mesh yaw-90 alignment
- Playrate = 1.0, PlaybackOffset = 0.0
- PitchAim = (0.7, 0.8); YawAim = (-0.3, -0.1, 0.2, 0.3); KickAim = (-3.2, -3.8); KickUpAim = (0,0)
- PitchHip = (1.3, 1.7); KickHip = (-2.0, -2.0); bSmoothRoll = True
- Curves: SingleRot=`AK_Semi_Rot`, SingleLoc=`AK_Semi_Loc`, AutoRot=`AK_Auto_Rot`, AutoLoc=`AK_Auto_Loc` (all in `.../Recoil/AK105/`)

**Task 2 change:** duplicate `/Game/KINEMATION/TacticalShooterPack/Blueprints/Recoil/AK105/RD_AK105` to `/Game/Core/Enemies/Animation/Recoil/DA_EnemyRecoil_Rifle`. Duplicating the blank `DA_RecoilData` (plan's literal instruction) yields zeros + no curves and would need full authoring in Task 7. Use `RD_AK105`.

---

## 3. ABP_Enemy_Grunt — `/Game/Core/Enemies/Animation/ABP_Enemy_Grunt`

AnimBlueprint (the shared enemy ABP for all 8 archetypes). Graphs: `EventGraph` (2 nodes), `AnimGraph` (26 nodes). Anim instance class: see `UEnemyAnimInstance` (C++ parent).

### AnimGraph pose chain (verbatim node titles, traced via get_connections)
```
Locomotion (State Machine).Pose ----------------+
                                                 |--> [LBB-final BB89].BasePose
[grip Blend Poses by bool].Pose --> ... (feeds the grip-blend LBB BlendPoses_0, saved as CachedLocomotion)

CachedLocomotion -> Slot 'DefaultSlot' -> DefaultCache
DefaultCache -> Slot 'UpperBody' (into an LBB BlendPoses_0) ; LBB.Pose -> Slot 'FullBody' -> CachedPostSLots

CachedPostSLots.Pose --> AO_Companion_Rifle02 (AimOffset Player).BasePose
                          AimYaw  -> AO.X
                          AimPitch-> AO.Y
AO_Companion_Rifle02.Pose --> [LBB-mid DB56].BlendPoses_0
CachedPostSLots.Pose      --> [LBB-mid DB56].BasePose
[LBB-mid DB56].Pose       --> [LBB-final BB89].BlendPoses_0
CachedLocomotion          --> [LBB-final BB89].BasePose
[LBB-final BB89].Pose     --> Output Pose.Result
```

Key node IDs:
- **`AO_Companion_Rifle02`** aim offset = `AnimGraphNode_RotationOffsetBlendSpace`, id `E3FBE9FD4F7DE0CC15ABB6B22BEE2B1B`, pos (800, 288). Driven by `AimYaw`/`AimPitch` vars, Alpha=1.0.
- **MID layered-bone-blend** = id `DB56FE08403C5D09391B2A85B659620E`, pos (1152, 272). BasePose = CachedPostSLots, BlendPoses_0 = AO output.
- **FINAL layered-bone-blend** (feeds Output Pose) = id `BB8906734A9A6117D97298A916A0936B`, pos (1632, 144). BasePose = CachedLocomotion, BlendPoses_0 = MID-LBB output. Output -> `Output Pose` (root, id `1B11577C4A872E1B4392F5B29E0B8959`).

### Grip blend (DO NOT disturb)
- `Blend Poses by bool` selects the weapon grip pose: BlendPose_0 = `A_FP_Mk14EBR_Idle` (Sniper), BlendPose_1 = `A_FP_AK105_Idle` (rifle), chosen by `WeaponAnimType` (`Equal (Byte)`). Output feeds the grip-blend LBB's BlendPoses_0 (the `CachedLocomotion` branch). This is the sniper/rifle grip-blend from prior work — upstream of the aim offset, untouched by recoil.

### Slots
- `DefaultSlot`, `UpperBody`, `FullBody` (all Group `DefaultGroup`). The fire-loop / hit-react / montages play on these (the enemy fire-loop montage uses the body slots; the bolt-cycle + reload play on the weapon mesh's own slot, not here). The `FullBody` slot is the montage path the plan removes in Task 6.

### >>> INJECTION POINT for the additive recoil node <<<
Insert the additive (stock **ApplyAdditive** or **Transform (Modify) Bone**) on the single wire:
**`MID-LBB (DB56).Pose  ->  FINAL-LBB (BB89).BlendPoses_0`**  (around x=1632, y=144).
This sits **downstream of `AO_Companion_Rifle02`** and downstream of every grip/upper-body bone-mask blend, and **upstream of the final layered-bone-blend output / Output Pose** — exactly the plan's requested location. It does not touch locomotion (the `CachedLocomotion` BasePose branch), the grip blend, or the reload/montage slots, all of which are upstream.
(Equivalent alternative: on `AO.Pose -> MID-LBB.BlendPoses_0`, but the wire above is cleaner since it's after both bone-mask blends.)

### IDLE state internals (Locomotion state machine)
- Locomotion state-machine states: **`Idle`, `Falling`, `Crouch Locomotion`** (transitions Idle<->Falling, Idle<->Crouch Locomotion).
- The **`Idle` state is NOT a static idle clip** — it is a single **`BS_Enemy_Rifle_Locomotion` Blendspace Player** (`/Game/QuantumCharacter/Retarget/BS_Enemy_Rifle_Locomotion`) driven by **`Get Speed`** + **`Get Direction`** -> `Output Animation Pose`. So "idle" = the zero-speed sample of the locomotion blendspace.
- **Task 9 implication:** patrol-idle variation must layer/blend the new `Rifle_Patrol_Idle*` clips into this Idle state (random pick, gated on `bIsPatrolling`), against/over the blendspace at near-zero speed — there is no single idle Sequence Player to swap. Could add a Blend-by-bool: bIsPatrolling -> random patrol-idle pick, else -> the existing BS_Enemy_Rifle_Locomotion.

### Screenshots (saved)
- `C:/Users/matth/Documents/Github/ProjectExtract/agent_docs/recon_screens/ABP_Enemy_Grunt_AnimGraph.png` — AnimGraph (shows the AO node + layered-blend chain + rifle-holding preview).
- `C:/Users/matth/Documents/Github/ProjectExtract/agent_docs/recon_screens/ABP_Enemy_Grunt_IdleState.png` — second capture (could not programmatically focus the Idle sub-graph; shows the AnimGraph). Idle internals are fully documented in text above.

---

## 4. SK_Military_Character_Skeleton — `/Game/Military_Mega_Bundle/Mesh/SK_Military_Character_Skeleton`

Standard UE5 Mannequin-style rig (Quantum). Bone names verbatim (from a transient `SkeletalMeshComponent` on `SKM_Tshirt_Tucked_Bege`, 351 bones):

- **Right arm chain (additive recoil target):** `clavicle_r` -> `upperarm_r` -> `lowerarm_r` -> **`hand_r`**
- **Left arm chain:** `clavicle_l` -> `upperarm_l` -> `lowerarm_l` -> `hand_l`
- **Spine chain (upper-body kick target):** `spine_01` -> `spine_02` -> `spine_03` -> `spine_04` -> `spine_05` (also `spine_04_latissimus_l/r`)
- **Neck/head:** `neck_01`, `neck_02`, `head`
- **Pelvis/root:** `pelvis`, `root`
- **Weapon attach:** **NO dedicated `weapon_r`/`weapon_l` bone.** The rig uses the standard **`ik_hand_gun`** virtual bone (the weapon-space attach point), plus `ik_hand_root`, `ik_hand_r`, `ik_hand_l`. KINEMATION's FP `VB recoil_hand_r` virtual bone does NOT exist here — additive must target `hand_r` (or `ik_hand_gun` for weapon-space) directly.

### Skeleton SOCKETS (present on the rig)
- **`WeaponSocket`** — the held-weapon attach socket.
- **`WeaponSocket_Fire`** — the fire-align socket (used by the existing `enemy-weapon-fire-align` C++).
- **`WeaponSocket_Mk14`** — sniper-specific weapon socket.
- Plus `interaction`, `center_of_mass`.

**Recoil additive recommendation:** apply `RecoilAnimation` (relative FTransform) to **`hand_r`** (or `ik_hand_gun` if a weapon-space frame reads better), with a small complementary additive on `spine_03`/`spine_04` for the shoulder kick. Bone names confirmed present.

---

## 5. Rifle_Patrol_Idle0X — `/Game/Core/Enemies/Animation/Staging/Patrol/`

- **Skeleton verdict: ALREADY on `SK_Military_Character_Skeleton` (Quantum). NO retarget needed — Task 8 can be SKIPPED.** All present clips are `AnimSequence` on the Military skeleton.
- **>>> PLAN-AFFECTING: only 5 of the 10 clips exist <<<** Present indices: **`03, 06, 07, 08, 09`** (5 clips). Missing: `00, 01, 02, 04, 05`.
  - Matches `git status`: `Rifle_Patrol_Idle00/01/02/04/05` are staged-deleted; `Idle03/06/07/08/09` are untracked-new. So the patrol set was re-recorded/renumbered.
  - **Task 9 implication:** reference the actual present set {Idle03, Idle06, Idle07, Idle08, Idle09} (RandomIntegerInRange over those 5), OR restore/author the missing 5 first. The plan's "Idle00-09 (10 variants)" is stale.

---

## BONUS — enemy actor BP + weapon equip path

- **Enemy actor BPs** (all children of C++ `AEnemyCharacter` = `/Script/Extraction.EnemyCharacter`), under `/Game/Core/Enemies/Blueprints/`:
  `BP_Enemy_Grunt` (rifle), `BP_Enemy_Officer`, `BP_Enemy_Grenadier`, `BP_Enemy_Heavy`, `BP_Enemy_Rusher`, `BP_Enemy_Sniper`, `BP_Enemy_Pistol`, `BP_Enemy_Shotgun`. (Plus `BP_EnemyGrenade` projectile.)
- **`BP_Enemy_Grunt` components:** CollisionCylinder (root, inherited), CharacterMesh0 (SkeletalMesh, inherited), CharMoveComp, HealthComponent, SuppressionComponent, MoraleComponent (EnemyMoraleComponent), AwarenessWidget (WidgetComponent), NavigationInvoker, Arrow. **No weapon component on the BP** — the weapon actor is spawned/attached in C++ (`AEnemyCharacter`). EventGraph is near-empty (3 nodes); no existing equip/fire wiring in the BP.
- **Weapon equip is C++-side.** The `AWeaponBase::OnWeaponFired` BlueprintAssignable delegate the plan binds is bound at runtime, not exposed as an overridable function. The existing `UEnemyAnimInstance` already carries `HandleWeaponFired` / `bIsFiring` / `bIsAiming` signals (per project memory).
  - **>>> PLAN-AFFECTING (Tasks 3-4 trigger path):** the cleanest place to add + Init the `AC_RecoilAnimation` and to drive `Play`/`Stop`/`SetAiming` is likely **`UEnemyAnimInstance`** (which already receives the fire/aim signals), not the actor BP EventGraph. The plan's "add component to the enemy BP + bind OnWeaponFired in the BP" may need to route through the AnimInstance / a small C++ trigger instead. Confirm the equip/fire surface with the C++ team before Task 3; this is the documented C++-fallback boundary in the plan's Task 4 note.

---

## Verdicts summary

| Fact | Verdict |
|---|---|
| AC_RecoilAnimation parent | Stock `UActorComponent` (BP). |
| Init / Play / Stop / SetAiming | `Init(UDA_RecoilData_C*, double)`, `Play()`, `Stop()`, `SetAiming(bool)`, `SetFireMode(TEnumAsByte<E_FireMode>)`. |
| Output var | `RecoilAnimation` (relative `Transform`). Camera path `ControllerRecoil` (V2D) — ignore. |
| Standalone? | **YES** — no interface/kit-class deps; OwnerCharacter only used by the camera-recoil path (safe no-op on AI). |
| DA baseline | Base `DA_RecoilData` is blank; duplicate **`RD_AK105`** (populated) instead. |
| ABP injection point | Wire `MID-LBB(DB56).Pose -> FINAL-LBB(BB89).BlendPoses_0`, downstream of `AO_Companion_Rifle02`, upstream of Output Pose. |
| Idle state | Single `BS_Enemy_Rifle_Locomotion` blendspace (Speed/Direction); no static idle clip to swap. |
| Recoil bones | `hand_r` (+ `ik_hand_gun` weapon-space), spine `spine_03/04`; no `weapon_r` bone. Sockets: `WeaponSocket`, `WeaponSocket_Fire`, `WeaponSocket_Mk14`. |
| Patrol clips | Already on Quantum skeleton (no retarget). Only 5 exist: 03,06,07,08,09 (not 10). |
| Enemy BP / equip | 8 BPs child of C++ `AEnemyCharacter`; weapon spawned in C++; trigger recoil via `UEnemyAnimInstance`. |
