# Enemy Animation Map — Shield Archetype

**Branch:** Enemies | **Date:** 2026-06-18 | **Status:** pre-anim (visually broken)

---

## 1. Weapon & Rig

| Property | Value |
|---|---|
| Weapon class | `BP_EnemyRevolver` |
| Weapon mesh | `SK_Revolver_Default_Combined` (InfimaGames ModernRevolver; SKEL_Revolver skeleton, separate from character) |
| Weapon DA | `DA_Revolver` — single-action, 45 dmg, 6-round cylinder, 2.6s reload |
| Character skeleton | `SK_Military_Character_Skeleton` (Quantum pack) |
| Shield component | `UEnemyShieldComponent` : `UStaticMeshComponent` — created at possess when `bHasShield=true` |
| Shield attach socket | `lowerarm_l` (DA_Enemy_Shield: `ShieldAttachSocket="lowerarm_l"`) |
| Shield relative transform | `ShieldRelativeTransform` — designer-assigned in DA |
| Attach code | `EnemyCharacter.cpp:585-589` — `AttachToComponent` with socket name, then `SetRelativeTransform` |
| Shield mesh (current) | `/Engine/BasicShapes/Cube` (placeholder — see §2) |
| Anim BP | `ABP_Enemy_Grunt` (shared, all 7 archetypes) |
| Anim instance class | `UEnemyAnimInstance` |

The shield is a **static mesh socket-attached to the left forearm bone**. It is not driven by bones or IK — it rides the left forearm wherever the animation drives it.

---

## 2. Current State — What Is Actually Playing

### 2.1 The rifle pose is active

`ABP_Enemy_Grunt` currently plays the retargeted Quantum TP rifle set from
`Extraction/Content/QuantumCharacter/Retarget/`:

- **Idle:** `Mil_Rifle02_St_Idle00` — two-handed rifle carry. Both hands wrap a long gun.
- **Locomotion blendspace:** `BS_Companion_Rifle02_Locomotion` — eight-direction walk/run, rifle-up carry. Both arms in two-handed grip.
- **Aim offset:** `AO_Companion_Rifle02` — 17-clip offset, both hands on stock + foregrip.
- **Fire loop:** `AM_Companion_Fire_Loop` / `AM_Companion_Fire_Single` — two-handed rifle fire.
- **Reload:** `AM_Companion_Reload` — two-handed mag swap.
- **Hit/death:** `Mil_Rifle02_St_Hit_Light_F`, etc. — rifle-carry body.

None of these clips are contextual per archetype. `UEnemyAnimInstance::NativeUpdateAnimation`
(`EnemyAnimInstance.cpp:57-153`) drives `bIsFiring`, `bIsReloading`, `Speed`, `Direction`,
`AimPitch/Yaw` generically — it has **no archetype or weapon-type awareness**. `PlayFireMontage`
(`EnemyAnimInstance.cpp:175`) plays whichever `FireMontage` is assigned on the ABP; today that
is the shared rifle montage.

### 2.2 Why the rifle pose breaks the Shield

The Shield archetype carries:
- A **revolver in the RIGHT hand** (one-handed weapon).
- A **riot shield socket-attached to the LEFT forearm** (left arm raised, holding shield forward).

The rifle pose expects **both hands gripping a long gun simultaneously**:

1. **Right hand** — the rifle grip position places the hand roughly where a pistol grip sits, so the
   revolver mesh is approximately in the right hand region. However the wrist angle and arm extension
   are wrong for a single-action revolver held at arm's length.

2. **Left hand (the hard problem)** — the rifle pose drives the left arm into a foregrip reach: the
   left forearm extends forward and slightly down to grip the handguard of an imaginary rifle. This
   means `lowerarm_l` (where the shield is attached) swings to a **rifle-foregrip position** —
   roughly in front of the torso at waist height, not raised shield-forward. The shield mesh rides
   this bone, so it floats at waist level pointing forward rather than being held upright in a
   protective carry.

3. **The shield mesh is currently the engine cube** (`/Engine/BasicShapes/Cube`, unit scale) attached
   at the `lowerarm_l` socket with default identity relative transform. The cube physically wraps the
   carrier and blocks 360° of incoming fire — the archetype's counter (flank, grenade) is inaccessible
   until this is fixed. This is a known P3 blocker recorded in `agent_docs/enemy_code_plan.md:349` and
   `enemy_code_plan.md:438`.

4. **Locomotion** — the rifle walk blendspace (`BS_Companion_Rifle02_Locomotion`) reads as a
   two-handed gun carrier. The Shield archetype's design intent is a slow 220 cm/s shield-advance
   walk (`ShieldAdvanceSpeed=220`), but the clips show normal combat-run body lean and arm swing,
   not a wall-of-shield advance posture.

5. **Reload** — the two-handed rifle reload drives the left hand off the imaginary foregrip to a
   magazine. There is no spare hand for a revolver cylinder swing; the six-round cylinder reload
   requires opening the crane, ejecting casings, and loading speedloader or individual rounds — a
   fully distinct two-hand interaction. With the shield on the left forearm, a real reload would
   require the shield to hang or be set down; the current montage plays the rifle mag-swap regardless.

**Net visual result:** the revolver floats off the right hand at a wrong angle, the left arm
drives a cube to waist height rather than raising a shield, and the walk clips show a two-handed
rifle carrier rather than a slow advancing wall.

---

## 3. Required Animation Set

### 3.1 Slot mapping

`UEnemyAnimInstance` controls four montage slots and one blendspace, all designer-assigned on the
ABP (`EnemyAnimInstance.h:111-136`). Per-archetype selection does not exist yet — the ABP would
need a weapon-type enum input or a separate ABP child class to select different montage sets.

### 3.2 Required clips (all on `SK_Military_Character_Skeleton` post-retarget)

| Slot | Name | Description |
|---|---|---|
| Locomotion BS | `BS_Shield_Advance` | 8-direction walk at 220 cm/s; left arm raised, shield held forward vertical; right arm at side or pistol low-ready; full blendspace (F/B/L/R/diagonals). **No equivalent on disk.** |
| Idle | `Idle_Shield_Forward` | Standing still, shield raised on left forearm, revolver low-ready in right hand. **No equivalent on disk.** |
| Crouch idle | `Idle_Shield_Crouch` | Crouched behind shield, right arm extended past shield edge. **No equivalent on disk.** |
| Aim offset base | `AO_Pistol_Onehanded` | Right-arm-only aim offset (one-handed gun); LEFT ARM stays shield-forward throughout the full yaw/pitch range. **Critical gap — see §4.** |
| Fire loop | `FireMontage` (shield) | One-handed revolver fire — right arm extends forward/right past shield edge; left arm holds shield without breaking posture. **No equivalent on disk.** |
| Single fire | `SingleFireMontage` (shield) | Same as above for the 0.6s burst single-fire path used by `HandleWeaponFired`. |
| Reload | `ReloadMontage` (shield) | Cylinder swing, eject, load speedloader — right arm only OR shield lowers to side briefly. **No equivalent on disk.** This is the most mechanically complex clip in the project. |
| Hit react | `HitReactMontage` (shield) | Upper-body flinch that does NOT break the left arm's shield position; right-side and front hits primarily. |
| Death | `DeathMontage` (shield) | Falls backward or forward; shield arm releases naturally. Could reuse an existing death clip from `RifleMega` with acceptable quality. |
| Shield break VFX / pose | Post-break idle | After `OnShieldBroken` fires, the enemy fights as a "brittle grunt"; transitions to standard rifle poses. Handled structurally (BT `ShieldAdvance` fails → normal combat). No extra clip needed — the rifle set takes over. |

### 3.3 Special: shield-carry/advance (most unique requirement)

The shield-advance walk is the archetype's identity animation. It requires the left arm rigid and
raised throughout the blendspace — a constraint no on-disk locomotion clip satisfies. Options:

- **Full mocap set** (ideal): purpose-made shield-advance locomotion + idle.
- **Additive layering** (practical): additive left-arm-shield pose applied over any locomotion base
  via a per-bone layer in ABP. The left arm is locked to a "forearm raised, shield forward" pose
  additively on top of the walk blendspace, while the right arm plays one-handed gun states.

---

## 4. What Exists Per Pack

### 4.1 Kit Pistol set (FP, SK_Mannequin)
**Path:** `Extraction/Content/ProceduralFPSKIT/Character/Animations/WeaponAnims/Pistol/`

Clips: `Anim_Arms_Pistol_Fire`, `_Idle`, `_Inspect`, `_Melee`, `_Reload`, `_ReloadEmpty`, `_Run`.

- Skeleton: `SK_Mannequin` (FP arms only — no legs, no full body).
- These are **first-person** animations on a different skeleton. They require retargeting to
  `SK_Military_Character_Skeleton` to be usable for the TP enemy.
- They animate the **right hand only** in a one-handed pistol hold — this is the closest on-disk
  reference for the revolver right-hand pose and fire timing.
- **Critical mismatch:** the FP pistol idle has a relaxed or two-hand-cup left hand. The Shield
  needs the left arm locked forward holding a shield — completely incompatible left-arm pose. The
  right-hand reference is useful; the left-hand data is actively wrong.
- Tag: [RETARGET-FP-TO-TP] — usable as right-arm reference after retarget + left-arm override.

### 4.2 Mannequin Pistol set (TP, SK_Mannequin)
**Path:** `Extraction/Content/Characters/Mannequins/Anims/Pistol/`

Clips: `MM_Pistol_Fire`, `MM_Pistol_Reload`, `MM_Pistol_Equip`, `MM_Pistol_DryFire`, `MM_Pistol_Fire_Montage`, `MF_Pistol_Idle_ADS`, full walk/jog blendspace (8-dir), aim offset `AO_Pistol`, jump set.

- Skeleton: `SK_Mannequin` (full TP body).
- This is a **full TP one-handed pistol set** with locomotion, fire, reload, and aim offset.
- Requires retargeting to `SK_Military_Character_Skeleton` via a new IK Rig / RTG asset.
  `RTG_RifleMannequin_to_Military.uasset` exists; a Pistol-targeted retargeter would work the same
  way or could reuse the same IK rig with a second RTG asset.
- After retarget this covers: right-arm fire, right-arm reload, one-handed locomotion, one-handed
  aim offset base.
- **Same left-arm problem:** the Mannequin pistol idle/walk have a two-hand cup grip or relaxed
  left arm — not a shield-raised forward arm. The left arm data cannot be used as-is.
- Tag: [HAVE-RETARGET] — this is the best starting point for right-arm reference on Quantum skeleton.

### 4.3 InfimaGames TPP idle poses (SK_Mannequin)
**Path:** `Extraction/Content/InfimaGames/ModernGunsBundle/_Demo/Animations/`

Clips: `A_TPP_Revolver_Idle_Pose_Example`, `A_TPP_Pistol_Idle_Pose_Example` (+ AR/LMG/Shotgun/SMG/Sniper).

- Single static idle pose per weapon type, skeleton unknown but likely `SK_Mannequin` or proprietary.
- Provides the held-weapon T-pose reference for socket alignment and IK target setup.
- Not a playable locomotion/combat set — no fire, no reload, no walk.
- Tag: [HAVE-RETARGET] for socket/IK reference only.

### 4.4 QuantumCharacter Retarget set (SK_Military_Character_Skeleton)
**Path:** `Extraction/Content/QuantumCharacter/Retarget/`

All clips are already on the target skeleton. Contents: two-handed rifle idle/walk/run (8-dir, stand + crouch), rifle aim offset `AO_Companion_Rifle02`, cover idles, fire/reload/hit montages.

- Every clip is two-handed rifle. None is usable for one-handed revolver or shield posture.
- Two RTG assets exist: `RTG_RifleAutoMannequin_to_Military` and `RTG_RifleMannequin_to_Military`.
- Tag: [HAVE-ON-SKELETON] but wrong weapon type — only death/hit-react clips are reusable.

### 4.5 RifleMega MocapAnimPack (SK_Mannequin)
**Path:** `Extraction/Content/RifleMega_MocapAnimPack/`

Full two-handed rifle mocap set: aim offsets (Rifle01/02/03, crouch), locomotion, death/hit by direction (4-dir crouch death, multi-dir stand death/hit), turn sets.

- All two-handed rifle. No pistol, no one-handed, no shield content.
- Death clips (`Rifle_Cr_Death_B/F/L/R`, stand equivalents) are skeletal-retarget candidates for the
  Shield death montage after the shield-arm releases.
- Tag: [HAVE-RETARGET] for death/hit only.

### 4.6 What does NOT exist anywhere on disk

- Any TP clip with a left arm raised holding a shield forward.
- Any "shield + pistol/revolver" combined pose.
- Any shield-advance locomotion (slow deliberate walk, shield raised).
- Any one-handed revolver fire or reload on `SK_Military_Character_Skeleton`.
- Any additive "shield arm locked" pose asset.

---

## 5. Gaps

### [MUST-SOURCE] — Blocking gaps (archetype non-functional without these)

| Gap | Why blocking |
|---|---|
| **Shield mesh** | Engine cube blocks 360°. Real riot shield SM + correct relative transform on `lowerarm_l`. No animation work functions correctly until the mesh is right — the arm pose drives the shield wherever `lowerarm_l` goes. |
| **One-handed revolver TP combat set on SK_Military_Character_Skeleton** | Fire loop, single fire, aim offset (right arm only), locomotion idle — the right-arm half of every combat clip. Source: Mannequin Pistol set retargeted via a new `RTG_PistolMannequin_to_Military` asset. |
| **Shield-carry idle and locomotion** | Left arm raised, shield forward throughout. No on-disk locomotion clip has this. Options: (a) source a riot-shield locomotion pack (Marketplace: "Riot Shield Animations", "Police Combat Pack"), (b) author an additive left-arm override pose and layer it over the retargeted Mannequin pistol walk in ABP. Option (b) is lower risk given the existing retarget pipeline. |
| **One-handed revolver reload** | Cylinder swing, eject, load — the most mechanically specific clip in the whole roster. The kit Pistol reload (`Anim_Arms_Pistol_Reload` → `MM_Pistol_Reload`) is a magazine-fed semi-auto; the motion is completely different from a revolver. Must source or author. |
| **Per-weapon ABP selection signal** | `ABP_Enemy_Grunt` has no archetype awareness. A `WeaponType` enum input or second ABP child is needed to route Shield to pistol-set montages and locomotion BS rather than rifle-set. This is a C++/ABP wiring task, not just an asset import. |

### [HAVE-RETARGET] — Usable after retarget work

| Asset | Use | Work needed |
|---|---|---|
| `MM_Pistol_Fire`, `MM_Pistol_Reload`, `MF_Pistol_Idle_ADS`, `MF_Pistol_Walk_*`, `AO_Pistol` | Right-arm reference for revolver combat; base locomotion before additive shield-arm layer | New `RTG_PistolMannequin_to_Military` RTG asset; IK rig already exists (`IK_MilitaryCharacter`) |
| `Rifle_Cr_Death_B/F/L/R`, `Rifle01/02/03_St_Death_*` (RifleMega) | Death montage (acceptable at equal-or-worse quality after shield releases) | Retarget via existing RTG; left-arm shield pose irrelevant on death |
| `Mil_Rifle02_St_Hit_Light_F`, `AM_Companion_hit_Idle` | Hit-react base (upper-body flinch layered over shield hold) | Already on Quantum skeleton; trim left-arm data or accept rifle-pose left arm on hit |
| `A_TPP_Revolver_Idle_Pose_Example` (Infima) | Socket/IK alignment reference for revolver grip | Retarget to Quantum or use as IK target only |

---

## 6. Wiring Plan

### 6.1 ABP architecture change (required)

`ABP_Enemy_Grunt` must become weapon-type-aware. Minimal approach:

```
// Proposed input signal on ABP_Enemy_Grunt (Blueprint variable set by UEnemyAnimInstance)
EEnemyWeaponAnimType { Rifle, Pistol, Shield }
```

`UEnemyAnimInstance` already caches `OwningEnemy->GetCurrentWeapon()` each tick
(`EnemyAnimInstance.cpp:104`). Add: read `ArchetypeData->bHasShield` (or weapon type enum) and
expose it as `WeaponAnimType` to the ABP. The ABP then selects blendspace and montage set by
switch.

### 6.2 Layered composition (shield-arm lock)

The left arm cannot be driven by any single locomotion clip because no clip has the right pose.
Recommended approach for ABP:

1. **Base layer:** retargeted Mannequin Pistol walk blendspace (right arm correct, left arm wrong).
2. **Per-bone additive layer:** apply a single authored "shield-arm forward" pose additively to
   `upperarm_l`, `lowerarm_l`, `hand_l` bones only, blended in at full weight when `bHasShield &&
   !IsShieldBroken`. This overrides the left arm from the locomotion clip.
3. **Shield mesh follows automatically** — it is socket-attached to `lowerarm_l`, so fixing the
   bone pose fixes the shield position with zero extra work.
4. **On shield break:** `OnShieldBroken` fires; blend the additive layer out over 0.2s; the left
   arm returns to the locomotion clip's natural position; the enemy is now a standard rifle-user.

This is the lowest-sourcing-cost path: one authored additive pose asset + the retargeted pistol
set covers idle, locomotion, and combat. Reload remains the hardest clip because the left arm must
temporarily break the shield raise to interact with the cylinder.

### 6.3 Reload compromise options

Three practical options in order of effort:

1. **Right-arm-only reload** — author or source a revolver reload where the left arm never moves
   (shield hangs by gravity off the forearm, cylinder loaded one-handed). Realistic for a trained
   operator; looks deliberate.
2. **Shield drops to side** — a short additive transition blends the shield arm down during the
   reload window (~2.6s), then back up. Requires a second additive pose + blend logic in ABP.
3. **Accept rifle reload visually** — cheapest, most wrong. Only acceptable as a placeholder.
   The cylinder-swing sound cue (`38_SPL_Revolver_*` audio in the kit) would be playing against
   a mag-swap animation — clearly wrong to any observer.

Option 1 is recommended for initial implementation.

### 6.4 Fire montage slot

`UEnemyAnimInstance::PlayFireMontage` (`EnemyAnimInstance.cpp:175`) plays whatever `FireMontage`
is assigned on the ABP instance and loops it via `FireMontageLoopSection`. For the Shield:

- Assign a retargeted + additive-layer-corrected one-handed fire montage to the `FireMontage` slot.
- The `HandleWeaponFired` single-shot path (`EnemyAnimInstance.cpp:230`) uses `SingleFireMontage` —
  assign the same one-handed single-fire clip there.
- The `bIsFiring` auto-trigger (`EnemyAnimInstance.cpp:138`) already handles the rising/falling edge.
  No C++ changes needed once the ABP montage slots are populated.

### 6.5 Per-weapon selection signal (C++ side)

`UEnemyAnimInstance` needs one new protected variable:

```cpp
UPROPERTY(Transient, BlueprintReadOnly, Category = "Enemy|Animation|Weapon")
EEnemyWeaponAnimType WeaponAnimType = EEnemyWeaponAnimType::Rifle;
```

Set it in `NativeUpdateAnimation` alongside the existing weapon state reads (approx.
`EnemyAnimInstance.cpp:104`). Read `OwningEnemy->GetArchetypeData()->bHasShield` to select
`Shield`; check weapon class or weapon type tag for `Pistol` vs `Rifle` for other archetypes.
ABP reads `WeaponAnimType` via a BlendSpace selector or state machine branch.

---

## Summary Table

| Category | Status |
|---|---|
| Weapon rig & socket attach | Code complete; shield mesh is placeholder |
| Current anim playing | Two-handed rifle — wrong for both revolver and shield |
| Right-arm revolver reference | [HAVE-RETARGET] Mannequin Pistol set → needs RTG_Pistol_to_Military |
| Left-arm shield pose | [MUST-SOURCE] — nothing on disk; additive pose authored or sourced |
| Shield-advance locomotion | [MUST-SOURCE] — no on-disk clip; additive layer over pistol walk is lowest-cost |
| One-handed revolver reload | [MUST-SOURCE] — most complex; no close analogue on disk |
| Death / hit-react base | [HAVE-RETARGET] RifleMega death clips acceptable |
| ABP weapon-type selection | Not wired; needs C++ signal + ABP branch |
| Shield mesh (SM) | [MUST-SOURCE] — real riot-shield SM; engine cube placeholder breaks archetype |
