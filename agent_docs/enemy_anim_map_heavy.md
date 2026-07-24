# Heavy Enemy — Animation Map

**Date:** 2026-06-18  
**Branch:** Enemies  
**Status:** Read-only audit — no code or assets changed.

---

## 1. Weapon & Rig

| Property | Value |
|---|---|
| Weapon BP | `BP_EnemyLMG` |
| Weapon mesh | `SK_LightMachineGun_Frame` (Infima Modern Guns bundle) |
| DA | `DA_LMG` — 12 RPS, 20 dmg, 100-round belt, 4.5s reload |
| Character skeleton | `SK_Military_Character_Skeleton` (Quantum) |
| Anim instance | `UEnemyAnimInstance` / `ABP_Enemy_Grunt` (shared — all 7 archetypes) |
| ABP asset | `Content/Enemy/AI/ABP_Enemy_Grunt.uasset` |
| Retarget rigs (on disk) | `IK_MilitaryCharacter`, `RTG_RifleMannequin_to_Military`, `RTG_RifleAutoMannequin_to_Military` |

The Infima LMG folder (`Content/InfimaGames/ModernGunsBundle/ModernLightMachineGun/`) contains **no animation assets** — only mesh, physics asset, materials, and textures. Every current Heavy animation is drawn from the Quantum Rifle retarget set.

---

## 2. Current State

### What the Heavy actually plays today

Every animation slot is sourced from `Content/QuantumCharacter/Retarget/` and treats the weapon as a standard two-handed rifle grip. Nothing is LMG-specific.

| Slot | Current asset | Source |
|---|---|---|
| Standing idle | `Retarget/Idle/Mil_Rifle02_St_Idle00.uasset` | [QUANTUM-NATIVE retarget] |
| Locomotion walk (8-dir) | `Retarget/Idle/Mil_Rifle02_St_Walk_*_IPC.uasset` (×8) | [QUANTUM-NATIVE retarget] |
| Locomotion run (8-dir) | `Retarget/Idle/Mil_Rifle02_St_Run_*_IPC.uasset` (×8) | [QUANTUM-NATIVE retarget] |
| Crouch idle | `Retarget/Crouch/Mil_Rifle_Cr_Idle00.uasset` | [QUANTUM-NATIVE retarget] |
| Crouch walk (8-dir) | `Retarget/Crouch/Mil_Rifle_Cr_Walk_*_IPC.uasset` (×8) | [QUANTUM-NATIVE retarget] |
| Aim offset | `Retarget/AO/Mil_Rifle02_St_Aim_*.uasset` (17-pose set) | [QUANTUM-NATIVE retarget] |
| Fire loop (sustained/auto) | `ABP_Enemy_Grunt.FireMontage` → `Retarget/Combat/AM_Companion_Fire_Loop.uasset` | [QUANTUM-NATIVE retarget] |
| Fire single | `ABP_Enemy_Grunt.SingleFireMontage` → `Retarget/Combat/AM_Companion_Fire_Single.uasset` | [QUANTUM-NATIVE retarget] |
| Reload | `ABP_Enemy_Grunt.ReloadMontage` → `Retarget/Combat/AM_Companion_Reload.uasset` (or `Mil_Rifle03_St_Reload_Auto.uasset`) | [QUANTUM-NATIVE retarget] |
| Hit react | `ABP_Enemy_Grunt.HitReactMontage` → `Retarget/Combat/AM_Companion_HitReact_Aim.uasset` | [QUANTUM-NATIVE retarget] |
| Cover idle (crouch-left/right) | `Retarget/Idle/Mil_anim_CoverDown_Idle_*.uasset` | [QUANTUM-NATIVE retarget] |
| Cover stand peek | `Retarget/Idle/AM_Crouch_Cover_*.uasset` | [QUANTUM-NATIVE retarget] |
| Death | `ABP_Enemy_Grunt.DeathMontage` — designer-assigned, no per-archetype override | [QUANTUM-NATIVE retarget] |

All montages are `EditDefaultsOnly` on `ABP_Enemy_Grunt` — there is one ABP for all seven archetypes; there is no Heavy-specific ABP child.

### Mismatches

**Grip (real bug — reads wrong):** The entire rifle retarget set has the left hand at a standard rifle foregrip position, fingers wrapped close to the receiver. An LMG with a wide, extended handguard (`SM_LightMachineGun_Handguard_Default/Extended`) positions the forend much further forward. The current left hand floats off the handguard geometry, or rides the wrong point of the gun entirely. This is the mismatch flagged in prior sessions as "worth retargeting."

**Reload (real bug — wrong mechanism and wrong duration):** The Quantum Rifle reload (`AM_Companion_Reload`) is a box-magazine swap: pull the mag, slot a new one, charge handle. A 100-round belt-fed LMG reloads by opening the feed cover, lifting the expended belt or inserting a new drum/box, closing the cover, and charging. Visually and mechanically different. The rifle reload is also timed for a ~2.5–3s magazine swap; the Heavy's `DA_LMG.ReloadTime = 4.5s`. If `PlayRate = 1.0` (the default in `UEnemyAnimInstance::PlayReloadMontage`), the montage ends before the weapon finishes reloading, leaving a gap with no animation. If `PlayRate` is reduced to compensate it looks too slow and the motion reads as a rifle reload in slow motion.

**Fire loop (polish — acceptable at distance):** The rifle fire-loop additive drives a standard shoulder/right-hand recoil cycle. An LMG should have heavier cyclic bounce and less precise re-acquisition between rounds. At engagement range (600–2200cm) this reads passably; at closer distances or during the sustained HeavySuppress anchor behavior the single looping motif becomes obvious.

**No suppression flinch animation:** `OnSuppressedStateChanged` (from `USuppressionComponent`) is exposed as a `BlueprintAssignable` delegate. The Heavy's ABP does not bind it. When suppression transitions in (`bNowSuppressed = true`), nothing happens visually — the Heavy continues to animate in its normal sustained-fire or idle pose. The design intent (§5 of enemy_gameplay_as_built.md) is that a suppressed enemy does not enter the Expose phase of the peek-fire loop, but there is no corresponding visual "hunkering" animation.

---

## 3. Required Animation Set

What the Heavy needs to read as a COD/BF machine gunner / juggernaut:

| Slot | Description | Priority |
|---|---|---|
| **Idle — braced LMG grip** | Standing combat idle with left hand forward on handguard, butt in shoulder, slightly lowered barrel (resting carry posture, not full ADS) | HIGH |
| **Aim offset — wide grip** | 17-pose aim offset set (matching the existing Mil_Rifle02_St_Aim grid: ±90°/±45° yaw, ±90°/±45° pitch, cross-center) but with left hand forward | HIGH |
| **Locomotion — heavy walk** | 8-directional walk blendspace at combat speed (250–300 cm/s); heavier footfall, slight hunch, weapon stays low — not rifle-carry upright | MEDIUM |
| **Locomotion — run** | 8-directional run; current rifle run acceptable as a placeholder (body mechanics aren't far off) | LOW |
| **Fire loop — sustained** | Cyclic fire additive or full-body: shoulder absorption with heavier muzzle climb per round and lower inter-round re-acquisition than rifle. Must loop cleanly at 12 RPS (83ms/round) | HIGH |
| **Reload — long belt / drum** | 4.5s. Key beats: lower weapon, open feed-cover or remove drum, seat new belt/drum, close cover, charge, raise. Must not finish before 4.5s elapses at PlayRate 1.0 | HIGH |
| **Suppression hunkered** | Crouched low, weapon depressed, head down — played while `bIsSuppressed = true`. Can be a pose blend or simple montage. Exits when suppression clears | MEDIUM |
| **Hit react** | Current rifle hit-react works; low priority to replace | LOW |
| **Death** | Current shared death works; ragdoll takes over quickly | LOW |
| **Cover idle (crouch)** | Current crouch cover idles work at distance — grip mismatch less visible crouched | LOW |

---

## 4. What Exists Per Clip

### On disk now

| Asset path | Format | Skeleton | Status |
|---|---|---|---|
| `Content/InfimaGames/ModernGunsBundle/_Demo/Animations/A_TPP_LightMachineGun_Idle_Pose_Example.uasset` | Pose (single frame) | SK_Mannequin (Infima/UE4) | [RETARGET] — needs RTG to SK_Military |
| All `Content/QuantumCharacter/Retarget/` assets listed in §2 | Animation sequences / blendspaces / montages | SK_Military_Character_Skeleton | [QUANTUM-NATIVE] — in use today |

### Infima TPP Idle Pose note

`A_TPP_LightMachineGun_Idle_Pose_Example` is a **single-frame reference pose** on `SK_Mannequin` (the Infima / UE4 Epic skeleton). It shows the left hand forward on the LMG handguard. This pose can be used as the source for:

1. An IK-corrected retarget to `SK_Military_Character_Skeleton` via the existing `RTG_RifleMannequin_to_Military` retargeter (or a new LMG retargeter if hand IK positions differ significantly).
2. The base for a custom aim-offset set and locomotion base pose — all 17 AO frames need to be authored or retargeted from this hand position.

The Infima bundle contains **no LMG fire, reload, or locomotion animations** — only this idle pose and the weapon meshes.

### Other source packs (disk inventory)

The brief notes additional packs. Glob results:

- `Content/ProceduralFPSKIT/Character/Animations/WeaponAnims/` — FP arms animations on `SK_Mannequin`, no TP body. Not useful directly.
- `Content/ImportedAssets/LAMPVol2/`, `MotionAnimations/`, `RifleAnims/` — not verified in this session (outside scope). May contain TP locomotion or reload animations on SK_Mannequin that could be retargeted.
- No dedicated LMG fire-loop or belt-reload animation was found in any Content subdirectory.

---

## 5. Gaps

### [MUST-SOURCE] — no asset exists on disk

| Gap | Why it matters | Notes |
|---|---|---|
| **Belt / drum reload animation** (4.5s, TP body) | The rifle reload plays the wrong mechanism and has a timing mismatch vs `DA_LMG.ReloadTime = 4.5s` | Must be sourced (Mixamo "machine gun reload", asset store, or custom mocap equivalent). Skeleton: SK_Mannequin (for retarget) or SK_Military_Character_Skeleton directly |
| **Sustained LMG fire-loop** (TP body) | The rifle fire-loop at 12 RPS reads as a rifle; acceptable at distance but fails close-up during `HeavySuppress` | Same sourcing path. Cyclic-additive preferred for blend flexibility |
| **Suppression hunkered pose / montage** | No visual on `OnSuppressedStateChanged` | A low crouch, depressed weapon, head-down — can be a blended pose or 1–2s montage that holds until unsuppressed |

### [HAVE-RETARGET] — asset exists, needs retarget to SK_Military

| Gap | Source asset | Retarget path |
|---|---|---|
| **LMG braced idle** (wide grip, standing combat) | `A_TPP_LightMachineGun_Idle_Pose_Example` (SK_Mannequin) | `RTG_RifleMannequin_to_Military` (or new LMG-specific RTG if hand IK diverges) |
| **LMG aim offset set** (17 poses, wide grip) | None exists — must be authored from the retargeted idle pose as the base reference | After idle is retargeted, create 17 AO poses in UE5 by adjusting upper body from the base |

### Polish vs Real

| Gap | Classification |
|---|---|
| Grip / aim offset (wrong hand position) | **Real** — visible at any range when the left hand floats off the handguard |
| Belt reload (wrong mechanism + timing mismatch) | **Real** — 4.5s reload with a ~2.5s rifle animation leaves dead time; wrong mechanism breaks identity |
| Sustained fire loop | **Polish** — rifle loop works at distance; only fails at close range or under scrutiny |
| Suppression hunkered pose | **Real** — the design beat (covering fire visibly works) has no visual expression |
| Heavy locomotion feel | **Polish** — current rifle locomotion is acceptable; heavier movement would add character |

---

## 6. Wiring Plan

### Proposed signal: per-weapon montage overrides on ABP

The current `ABP_Enemy_Grunt` has `EditDefaultsOnly` montage properties (`FireMontage`, `ReloadMontage`, `SingleFireMontage`, `HitReactMontage`, `DeathMontage`, `MeleeMontage`). These are designer-assigned in the ABP directly. Because all seven archetypes share one ABP, there is no per-archetype slot without either:

**(A) ABP child class per archetype** — create `ABP_Enemy_Heavy` as a child of `ABP_Enemy_Grunt`, override the montage properties with LMG assets. `DA_Enemy_Heavy` already points at `BP_Enemy_Heavy`; `BP_Enemy_Heavy` assigns `AnimClass`. This is the clean path.

**(B) Per-weapon montage map on `UEnemyArchetypeData`** — add `TMap<TSubclassOf<AWeaponBase>, FEnemyMontageSet>` to the DA; `UEnemyAnimInstance::NativeUpdateAnimation` reads the equipped weapon class and selects the montage set at runtime. More data-driven but adds C++ and DA complexity; only worth it if multiple archetypes need different montages for the same weapon.

**Recommendation: Option A (ABP child).** The Heavy is the only archetype whose weapon is genuinely different enough to need its own grip/reload. Option B is premature generality at this stage.

### Proposed signal: suppression hunkered blend

`USuppressionComponent::OnSuppressedStateChanged` (line 10 of `SuppressionComponent.h`) is a `DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSuppressedStateChanged, bool, bNowSuppressed)`. It is already broadcast at state transitions.

Wire path:

1. Add `bIsSuppressed` bool to `UEnemyAnimInstance` (mirror the existing `bIsReloading` pattern).
2. In `NativeUpdateAnimation`, read `OwningEnemy->GetComponentByClass<USuppressionComponent>()->IsSuppressed()` each tick (cheap).
3. In `ABP_Enemy_Heavy`'s AnimGraph, blend the hunkered pose into the upper body when `bIsSuppressed = true` — either a layered blend per bone (upper body only) over the locomotion, or a full-body state in the state machine that overrides Idle/Walk when suppressed + not firing.

No new C++ delegate binding needed — the tick poll is sufficient and matches the existing pattern for `bIsFiring` / `bIsReloading`.

### Per-weapon reload timing

`PlayReloadMontage` in `UEnemyAnimInstance.cpp` (line 193–198) calls `Montage_Play(ReloadMontage, PlayRate)` with `PlayRate = 1.0`. When `ABP_Enemy_Heavy` uses the LMG belt-reload montage (sourced at ~4.5s length), it plays at 1.0 and aligns naturally with `DA_LMG.ReloadTime = 4.5s`. No C++ change is needed — the timing contract is met by authoring the montage to the correct duration.

If the sourced reload montage is not exactly 4.5s, adjust via `PlayRate = MontageLength / DA->ReloadTime` at the call site — this would require a small C++ change to pass the DA reload time through, or an `EditDefaultsOnly float ReloadPlayRate` on the ABP.

---

## Summary

**Archetype:** Heavy — 250 HP, armour (front ×0.3, 3 plates), `TurnRate = 90°/s`, `MaxAimYaw = 60°`, suppression resistance ×3, morale resistance ×2. Combat subtree: `Fallback → HeavySuppress → CombatFire`. No cover. Anchors, hoses, suppresses from open ground.

**3 things wrong (priority order):**

1. **Belt-reload (real):** rifle reload plays a mag-swap in ~2.5s; the LMG needs a belt/drum mechanism at 4.5s. Dead animation time + wrong identity.
2. **Grip (real):** left hand rides a rifle foregrip position — floats off the wide LMG handguard in all standing poses and the aim offset.
3. **No suppression hunkered visual (real):** `OnSuppressedStateChanged` fires but nothing plays; the design beat "covering fire visibly suppresses the heavy" has no animation expression.

**Prioritized gaps:**

- [HAVE-RETARGET] LMG braced idle: `A_TPP_LightMachineGun_Idle_Pose_Example` (SK_Mannequin) → retarget via `RTG_RifleMannequin_to_Military`
- [HAVE-RETARGET] Aim offset set (17 poses): author from retargeted idle base — no source clip needed, UE5 pose authoring
- [MUST-SOURCE] Belt/drum reload (~4.5s TP body): no clip on disk — source or record
- [MUST-SOURCE] Sustained LMG fire loop (TP body): no clip on disk — source or record (cyclic additive preferred)
- [MUST-SOURCE] Suppression hunkered pose/montage: no clip on disk — low complexity, can be a ~1s transition to a held crouch pose
