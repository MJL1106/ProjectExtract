# Weapon Mesh Pipeline — swapping the held weapon visual

Goal: replace the rifle the player holds with an imported mesh, **and** establish a
repeatable convention so every future imported weapon equips and lines up in the hands
with near-zero re-tuning.

Chosen fidelity: **moving-parts reload** (magazine physically drops, bolt cycles). That
requires the new mesh to be **skeletal**, rigged to the kit's shared weapon skeleton —
which is also what makes alignment automatic. No C++ changes are needed anywhere; this is
Blender + asset/Blueprint wiring only.

Practice weapon: `SM_Rifle_Short` (`/Game/Military_Mega_Bundle/Mesh/Weapon/Rifle/`).
Fallback if its magazine turns out welded to the body: `SM_Rifle`
(`/Game/ImportedAssets/RifleAnims/ShowcaseAssets/Meshes/Rifle/`), which ships as 7 material
slots and is purpose-built for animation.

---

## 1. What you are actually swapping (two meshes, not one)

The weapon you see is **not** the same object that deals damage. There are two layers:

- **First-person visual** — the kit's `BP_FPCharacter` spawns a `BP_Item_Base` subclass
  (`BP_Weapon_AmericanRifle`) into your hands. It displays a **skeletal** mesh
  (`SKM_AmericanRifle`) that plays its own fire / reload / empty animations. This is the
  AmericanRifle you currently see. The C++ side picks it via
  `IKitWeaponInterface::GetKitVisualWeaponClass` → `WeaponData->KitVisualWeaponClass`.
- **Third-person / gameplay** — your project weapon `BP_Rifle`
  (C++ `AssaultRifle : AWeaponBase`) owns a second skeletal `WeaponMesh` that is
  **hidden from the owning player** (`SetOwnerNoSee(true)`) and seen only by the companion
  and enemies. This actor owns the real hitscan, damage, ammo, and reload state machine.
- **Pose / alignment data** — `DT_RifleAnimationValues` (a `DT_ProceduralAnimValues_C`
  holding an `ST_ProceduralWeaponValues` struct) carries the hand placement, the arms pose
  animation, and the ADS offsets. The C++ weapon hands it to the kit via
  `GetKitProceduralValues` → `WeaponData->KitWeaponPoseAsset`.

To replace the held weapon you change the **first-person visual mesh** (the important one)
and, for consistency in third person, the **gameplay weapon's hidden mesh**. Everything
else is wiring.

---

## 2. The convention (this is the "system")

The kit animates the weapon and places the hands **by bone name and by where the grip sits
relative to the weapon root**. If every gun obeys the same contract, the kit's existing
animations and hand poses apply to it unchanged. The contract:

- **One shared skeleton:** `SK_AmericanRifle_Skeleton`. 11 bones —
  `w_frame` (root) with children `w_bullet`, `w_sparemag`, `w_magazine`, `w_trigger`,
  `w_magrelease`, `w_boltrelease`, `w_selector`, `w_dustcover`, `w_bolt`,
  `w_charginghandle`. Reuse these **exact names**; UE animations target bones by name, so a
  matching skeleton inherits the reload (mag → `w_magazine`, bolt → `w_bolt`,
  charging handle → `w_charginghandle`) and fire animations for free.
- **Grip + axis:** barrel points **+Y** (the kit mesh's forward axis), and the grip sits in
  the same place relative to `w_frame` as the reference rifle. You guarantee this by
  **overlaying the reference rifle in Blender** and matching its position and size — no axis
  math required.
- **Reuse the pose data:** duplicate `DT_RifleAnimationValues` per weapon. If the grip
  matches the reference, every value transfers unchanged. The single live tuning knob if a
  gun sits slightly off is `WeaponValues → BasePoseLoc` / `BasePoseRot` (nudges the whole gun
  in the hands). Left-hand placement lives in the same struct (`NormalLeftHandLoc/Rot`,
  the grip-variant hand offsets, `AkimboLeftHandLoc/Rot`).
- **Reuse the Blueprints as templates:** duplicate `BP_Weapon_AmericanRifle` and your
  `BP_Rifle`; point the copies at the new mesh + new pose asset. The originals stay as
  clean templates.

**Per future rifle, the whole job becomes:** ~10 min in Blender (overlay + hard-skin to a
pre-made rig), import on the shared skeleton, duplicate two Blueprints and one data asset,
assign the mesh. No animation authoring, ever.

---

## 3. Scope and limits (worth knowing before you start)

- **AR-style layouts only, for free.** The *arm* reload motion (the support hand reaching to
  the mag well) is baked for the AmericanRifle's mag-well position. AR-pattern rifles fit.
  A bullpup or a side-feeding AK will still mag-drop correctly, but the hand-reach will look
  off until it gets its own arm reload animation — a separate, later job.
- **The magazine must be separable geometry.** To drop, the mag has to be its own vertex
  island so it can be skinned to `w_magazine`. A single material is fine — you select the
  island by linked geometry. Only if the modeller fused the mag to the body do you separate
  those faces by hand (covered in Phase A).
- **Same skeleton across rifles only.** Pistols, shotguns, etc. already have their own kit
  skeletons and anim sets; this convention is the rifle lane. Other classes get their own
  shared skeleton the same way.

---

## 4. Phase A — Blender: rig the new gun to the shared skeleton

Outcome: an FBX of the new gun, skinned to a copy of the AmericanRifle armature, sitting
where the reference gun sat.

1. In UE, export the reference: `SKM_AmericanRifle` → right-click → **Asset Actions →
   Export** → `.fbx`. This carries the mesh **and** the armature.
2. Get the new gun as FBX: export `SM_Rifle_Short` the same way (or use its original source
   FBX if you have it).
3. New Blender scene. **Import** the reference FBX, then **import** the new gun FBX, using
   **identical import settings** for both so they share orientation and scale.
4. **Overlay:** move / rotate / scale the new gun so it sits exactly on top of the reference
   — grip on grip, barrel along the same axis, matching overall size. Eyeball it against the
   reference silhouette; this is the step that makes the hands line up later.
5. **Separate moving parts:** in Edit Mode, hover the magazine and press **L** to select the
   linked island → **P → Selection** to split it into its own object. Repeat for the bolt and
   charging handle if you want them to animate. (If the mag is welded to the body, box-select
   its faces manually before **P**.)
6. **Parent** the new gun objects to the reference **armature**: select gun parts, then the
   armature last, **Ctrl+P → With Empty Groups**.
7. **Hard-skin** (rigid parts, weight 1.0, no painting): body → `w_frame`; magazine →
   `w_magazine`; bolt → `w_bolt`; charging handle → `w_charginghandle`. Select each object's
   verts, pick the matching vertex group, **Assign** at weight 1.0.
8. **Delete the reference AmericanRifle mesh** (keep the armature).
9. **Export FBX:** select the armature + new gun → export, scale 1.0, **Add Leaf Bones OFF**,
   smoothing **Face**.

I will walk this phase live — Blender is fiddly and step 4/5/7 are easier with eyes on it.

---

## 5. Phase B — UE: import as a skeletal mesh

1. Import the FBX. **Skeletal Mesh = on**; **Skeleton = `SK_AmericanRifle_Skeleton`**
   (pick the existing one — do **not** let it create a new skeleton).
2. Confirm it binds to that skeleton and the `Muzzle` / `BulletCasing` sockets are present
   (inherited from the skeleton).
3. Assign the gun's materials.

## 6. Phase C — UE: wire the weapon (exact clicks handed over at execution)

1. Duplicate `BP_Weapon_AmericanRifle` → `BP_Weapon_<NewName>`; set its inherited skeletal
   mesh to the new `SKM`.
2. Duplicate `DT_RifleAnimationValues` → `DT_<NewName>AnimationValues` (start identical;
   tune `BasePoseLoc/Rot` later only if needed).
3. On your gameplay weapon (`BP_Rifle`, or a duplicate of it): set the hidden 3P
   `WeaponMesh` to the new `SKM`; point `WeaponData`'s `KitVisualWeaponClass` at
   `BP_Weapon_<NewName>` and `KitWeaponPoseAsset` at `DT_<NewName>AnimationValues`.
4. Make sure that weapon is the one the character equips in its slot.

## 7. Phase D — verify

- Equip: the new gun should sit in the hands. If the grip/foregrip is off, nudge
  `WeaponValues → BasePoseLoc` / `BasePoseRot` on the data asset and re-check.
- Reload: the magazine should drop and the bolt cycle (from the inherited weapon animation).
- Third person (companion/enemy view): the new gun shows there too.

---

## 8. Future-weapon quick path (the payoff)

Keep a Blender file with the AmericanRifle armature + reference mesh as a template. For each
new rifle: import it, overlay, separate mag/bolt, hard-skin, export → import on the shared
skeleton → duplicate the two Blueprints + the data asset → assign the mesh. Lines up on
equip because the grip and bone names match the contract.
