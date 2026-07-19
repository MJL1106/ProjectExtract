# FPS-pack-weapons audit — Phase 0 ground truth (2026-07-19)

All facts below verified live in the editor (VibeUE python) or in C++ source on branch `FPS-pack-weapons`. This supersedes guesses in the plan (`agent_docs/fps_pack_weapons_plan.md`) wherever they differ.

## How the rifle currently works (the adaptation, end to end)

- **Player pawn** = `/Game/ProceduralFPSKIT/Blueprints/BP_ExtractionCharacter` (the kit character, adapted). Kit slot vars (`PrimarySlot` etc.) all default **None** — slots are not the grant path.
- **Grant path is C++**: the character carries `UWeaponComponent` with `DefaultWeaponClass = BP_Rifle_C`; it equips on BeginPlay (`WeaponComponent.cpp:46` → `EquipWeapon`).
- `BP_Rifle` (C++ `AssaultRifle : AWeaponBase`) → `WeaponData = DA_AssaultRifle` → bridge fields:
  - `KitVisualWeaponClass = BP_Weapon_AmericanRifle_C`
  - `KitWeaponPoseAsset = DT_RifleAnimationValues`
- **⚠️ `BP_Rifle`'s hidden 3P `WeaponMesh` has NO mesh assigned** (CDO `skeletal_mesh_asset = None`) — companion/enemies likely see the player holding nothing. Phase 1 wiring item.
- **The visual weapon** `BP_Weapon_AmericanRifle` is the kit weapon BP rebuilt around Infima parts:
  - Inherited kit components: `Item_Mesh` (skeletal), `FrontSight`/`RearSight` (kit `SM_FrontSight`/`SM_RearSight` — these drive kit procedural ADS), `OpticSight` (empty), `Muzzle`, `Grip`, `Laser`, `SpareMag`.
  - SCS-added Infima static parts at hand-tuned offsets: `SM_AssaultRifle_Magazine` (loc ≈ 0, 12.4, −6.9), `SM_AssaultRifle_Sight_Rear`, `SM_AssaultRifle_Handguard` (Extended mesh), `SM_AssaultRifle_Sight_Front_Folded`, `SM_AssaultRifle_Attachment_Grip_Angled` (actually `SM_Attachment_Grip_Vertical`).
  - `Item_Mesh` template mesh = None, but the BP **references `SK_AssaultRifle_Frame`** — the Infima skeletal frame is assigned at runtime somewhere in the BP graph (exact node not located; find it when first editing the BP).
- **ADS**: user-added sockets exist on the Infima iron-sight meshes — `RearAimPoint` on `SM_AssaultRifle_Sight_Rear`, `FrontAimPoint` on `SM_AssaultRifle_Sight_Front`. Current ADS = Infima irons; **no scope is mounted** (`OpticSight` mesh None).
- **`BP_AssaultRifle_Attachments_Example` is a red herring**: nothing of ours references it (only Infima demo maps). Its working-tree modification was incidental. Ignore it going forward; the adaptation is `BP_Weapon_AmericanRifle` + `BP_Rifle` + the two sight-mesh sockets.

## Skeleton + animation facts that decide Phase 1

- **Infima frames are fully rigged** (pack ships the bones, NOT the anims):
  - `SK_AssaultRifle_Frame` (skeleton `SKEL_AssaultRifle`): Root, Grip, Trigger, Handguard, **Bolt**, **Magazine**, Sight_Rear, Eject_Casing, **Scope**, Dust_Cover, **Charging_Handle**, Magazine_Release, Fire_Selector.
  - `SK_Pistol_Frame` / `SK_Pistol_Default_Combined` (skeleton `SKEL_Pistol`): Root, Grip, Trigger, Slide_Release, Ejection_Port_Cover, **Slide**, Sight_Rear, Rail, **Magazine**, Barrel, Eject_Casing, Safety, Magazine_Release_L/R.
- **Infima pack ships zero gun AnimSequences** (only sniper bipod + TPP poses). Any moving-parts anim must be authored in-engine (python AnimDataController keyframes or Sequencer bake onto `SKEL_AssaultRifle` / `SKEL_Pistol`). Frames have the bones to support it — **no Blender required**.
- **Kit weapon-anim mechanism (confirmed)**: an `AN_WeaponAnim` notify-STATE on the **arms** animation carries property `Anim` = the weapon-mesh AnimSequence, which the kit plays on `Item_Mesh`.
  - `Anim_Arms_AmericanRifle_Reload` → `Anim = Anim_Weapon_AmericanRifle_Reload` (targets the kit `SK_AmericanRifle_Skeleton`, `w_*` bones).
  - `Anim_Arms_AmericanRifle_Fire` → `AN_WeaponAnim` with `Anim = None` + `AN_WeaponBulletCasing_C` (shell eject). How the weapon fire anim gets triggered (if at all) — check `BP_Item_Base` fire path in Phase 1.
- **The mismatch producing today's static-gun reload**: `Item_Mesh` holds the Infima frame (`SKEL_AssaultRifle` bones) but the notify plays a `w_*`-skeleton anim → silently animates nothing. Arms move, gun parts don't.
- Kit rifle anim set lives at `/Game/ProceduralFPSKIT/Character/Animations/WeaponAnims/Rifle/` (4 arms anims + 4 weapon anims + `SK_AmericanRifle_Skeleton` + `SKM_AmericanRifle`).
- **`SK_SMG_1` (`ModernSMG/Meshes/SMG_RIGGED/`) binds the KINEMATION `SK_AK105` skeleton** (sockets Muzzle/Ejector/AimPoint/MagSocket). That's the user's earlier Blender pass — it belongs to the **KIN lane (Player-Setup branch)**, not this kit lane. No kit-skeleton re-rig of any Infima gun exists.

## Phase 1 route (concrete, no Blender)

1. **Mag drop**: attach `SM_AssaultRifle_Magazine` to `Item_Mesh` at the `Magazine` bone (bone name works as attach socket) instead of its fixed offset → author a reload AnimSequence on `SKEL_AssaultRifle` (Magazine bone drop/insert; Bolt + Charging_Handle for the empty variant) → repoint the reload arms-anim's `AN_WeaponAnim.Anim` at it. Decision to make in-phase: edit the shared kit arms anims' notify (also affects the kit AmericanRifle — currently unused by us) vs duplicating the 4 arms anims into an Infima rifle set (kit convention, cleaner for multi-weapon).
2. **Fire polish**: verify muzzle FX position (`Muzzle` component — Infima frame has no sockets), casing eject notify, HUD ammo via `KitSetAmmo`.
3. **3P mesh**: assign a rifle mesh to `BP_Rifle.WeaponMesh` so companion/enemies see the gun.
4. **ADS/attachments**: formalize the `RearAimPoint`/`FrontAimPoint` socket pattern; scope mounting = `OpticSight` + Infima `SM_Attachment_Scope`(+`_Small`) (no aim sockets on them yet — add per scope; note `SK_AssaultRifle_Frame` also has a `Scope` bone).
5. **Tuning**: `DT_RifleAnimationValues` (`BasePoseLoc/Rot`, left-hand fields) + F12 runtime tuning UI.

## Open questions (not blocking, resolve when touching the assets)

- Exact graph node assigning `SK_AssaultRifle_Frame` to `Item_Mesh`.
- Kit weapon-FIRE anim trigger path (`BP_Item_Base` fire graph).
- `DT_Item` is a Blueprint asset, not a real DataTable — item data appears construction-driven; irrelevant to our C++ grant path but relevant if pickups are ever wanted.

## Editor/session state at audit time

Editor PID 54988 on `Extraction.uproject`, level `L_EnemyGym` open. Adaptation + plan committed: `fc64cf69` (3 uassets + roadmap), `99ca3775` (plan doc).
