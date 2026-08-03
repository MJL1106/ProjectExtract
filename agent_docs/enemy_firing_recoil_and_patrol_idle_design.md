# Enemy Firing Recoil + Random Patrol Idle — Design Spec

**Date:** 2026-06-19
**Branch:** Enemies
**Status:** Approved direction — pending implementation plan

## Goal

Replace the enemy firing animation. Today enemies play a baked **fire-loop montage**
(`AM_Companion_Fire_Loop`) on a pose-replacing slot in the shared enemy AnimBlueprint, plus a C++
"fire-align" socket-ease band-aid. The montage overrides the upper body, so it fights the
grip + locomotion + reload blend the team is happy with.

Transfer KINEMATION's **procedural recoil** (the additive "body shake" + weapon kick that composes
cleanly in the pack's test level) onto the third-person enemies — **visuals only**. The enemy keeps
its existing weapon logic (`AWeaponBase`: fire/ammo/hitscan/fire-rate). KINEMATION's weapon logic,
input, and fire-mode state machine are **not** adopted.

Secondary feature in the same effort: **random patrol-idle variation** — when an enemy is idle on
patrol, pick one of the `Rifle_Patrol_Idle00-09` clips at random instead of the single generic idle.

## What this delivers

### Feature 1 — Firing recoil (visual)

- KINEMATION's `AC_RecoilAnimation` (`/Game/KINEMATION/Common/Recoil/AC_RecoilAnimation`) added to the
  enemy as a **visual-only** driver. Confirmed pure Blueprint (parented to stock `UActorComponent`),
  self-contained tick solver that outputs a single relative `FTransform` and never reads a skeleton —
  no C++ plugin, no engine rebuild.
- Driven entirely from signals the project already computes:
  - `AWeaponBase::OnWeaponFired` (`BlueprintAssignable`, once per shot) → recoil `Play()`
  - `bIsFiring` falling edge → recoil `Stop()`
  - `bIsAiming` → recoil `SetAiming()`
- Tuning comes from a `DA_RecoilData` (visual data, not kit logic). Start with **one shared rifle
  profile**; add per-weapon profiles later as needed.
- In `ABP_Enemy_Grunt`: the anim BP's Update event reads the component's recoil transform and caches it
  to a BP variable (game-thread, thread-safe). A stock **ApplyAdditive / ModifyBone** node, placed
  **downstream of the aim offset** (`AO_Companion_Rifle02`) and before the final output pose, applies
  the cached transform additively to the Military rig's `hand_r` / spine bones.
- The fire-loop montage field is left **null** → `PlayFireMontage` early-returns, the pose-replacing
  montage is gone, and the existing C++ fire-align alpha decays to 0 and goes inert on its own.

The result: recoil is additive and injected after grip + locomotion + aim, so the "perfect" hold pose
and reload mag-drop are untouched; the upper body / weapon kick layers on top, arms follow via the
existing left-hand IK.

### Feature 2 — Random patrol idle

- A small random-pick inside the locomotion **idle state** of `ABP_Enemy_Grunt`. When the enemy is
  patrolling and stationary (`bIsPatrolling` already exists on `UEnemyAnimInstance`), it selects one of
  `Rifle_Patrol_Idle00-09` at random, plays it, and re-rolls when it loops.
- No montages required, no new C++ — a random int variable selects the variant; the existing patrol
  flag gates entry.

## How it meets the goal

- **Visuals only, our logic kept:** the recoil component is triggered by our weapon's existing fire
  events and only contributes an additive pose. `AWeaponBase` is unchanged.
- **Composes instead of fights:** additive injection after the aim offset means the grip/locomotion/
  reload blend is never overwritten — the exact problem with today's loop montage.
- **Easier for future weapons:** a new weapon needs one `DA_RecoilData`, no authored fire montage and
  no new ABP node.
- **Can be 100% in-engine:** the recoil-transform cache lives in the anim BP's Update event, so no C++
  change is required. (Optional alternative: a ~5-line cache in `UEnemyAnimInstance::NativeUpdateAnimation`
  driven from the existing fire signals — not required.)

## Out of scope

- KINEMATION weapon **logic**, input, fire-mode state machine — enemies keep `AWeaponBase`.
- KINEMATION `AC_IKMotionPlayer` breathing/idle sway — recoil only.
- Multiplayer replication of enemy recoil — single-player only. Enemies fire server-side on a
  listen-server-of-one, so the un-replicated recoil transform is correct. (If MP is ever revived,
  driving recoil off replicated weapon state is the re-entry cost — noted, not built.)
- Removing the fire-align C++ — it self-neutralizes; full removal is an optional later cleanup.

## The one real cost

KINEMATION's recoil-application graph (`UE5_ABP_IK_Main`) targets Manny IK bones (`ik_hand_gun`,
`ik_hand_root`) and KINEMATION virtual bones (`VB recoil_hand_r`, etc.) that **do not exist** on
`SK_Military_Character_Skeleton`. The pack ships no third-person enemy application graph, so the small
additive node chain must be **re-authored** against the Military rig's `hand_r` / weapon-attach + spine
bones. Bounded, in-engine, stock nodes only.

## Edge cases worth pushback

- `DA_RecoilData` is tuned for the FP Operator rig; the third-person body has a different mesh offset
  (0,0,-86 / yaw-90), so recoil magnitude/pivot will need a data-tuning pass — expect iteration.
- `Rifle_Patrol_Idle00-09` may be Manny-sourced; if so they retarget to Quantum first (existing recipe)
  before they can play on the enemy.
- Random idle must blend cleanly between variants at loop seams and must not interrupt when the enemy
  transitions out of idle into walk/combat.
- Recoil `Stop()` must reset cleanly on death and weapon swap (no frozen offset).
- Confirm `AC_RecoilAnimation` has no hidden dependency on KINEMATION's character/weapon BP interfaces
  before reusing it standalone.

## In-engine confirmations (first plan steps)

1. Current idle-state wiring in `ABP_Enemy_Grunt`, and that `AC_RecoilAnimation` is standalone (no kit-BP
   callback dependency).
2. Whether `Rifle_Patrol_Idle00-09` are already on the Military/Quantum skeleton, or need retargeting.

## Key asset/code references

- Recoil: `/Game/KINEMATION/Common/Recoil/AC_RecoilAnimation`, `/Game/KINEMATION/Common/Recoil/DA_RecoilData`
- Enemy ABP: `ABP_Enemy_Grunt` (shared, under `/Game/Core/Enemies/...`), aim node `AO_Companion_Rifle02`
- Skeleton: `SK_Military_Character_Skeleton`
- Patrol idles: `/Game/Core/Enemies/Animation/Staging/Patrol/Rifle_Patrol_Idle00-09`
- Trigger surface: `AWeaponBase::OnWeaponFired` (`Public/Weapon/WeaponBase.h`); `bIsFiring` / `bIsAiming`
  / `bIsPatrolling` on `UEnemyAnimInstance` (`Public/Animation/EnemyAnimInstance.h`)
