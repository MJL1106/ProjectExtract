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

---

# Phase 1 round 1 — SHIPPED in-engine 2026-07-19, awaiting PIE (uncommitted)

All edits disk-verified via package reload. NOT committed yet — commit after user PIE pass.

1. **Authored weapon anims** (python AnimDataController, 30fps, smoothstep-interpolated keys from the kit's sampled timing):
   - `/Game/Core/Weapons/Anims/Anim_Weapon_InfimaAR_Reload` (1.567s) — `Magazine` bone: drop starts 0.67s, bottom −10.6cm with −20° pitch tilt @0.86s, reinsert, seated 1.18s.
   - `/Game/Core/Weapons/Anims/Anim_Weapon_InfimaAR_ReloadEmpty` (1.867s) — mag free-falls out (z−36 @0.33s), 1-frame swap to hand position (18.7,−27,−26.4 comp delta), arcs back, seats 1.21s; `Bolt` held −9.9cm back from t=0, slams forward 1.79→1.867s.
   - Kit-timing source data and the keyframe tables are re-derivable: sample `Anim_Weapon_AmericanRifle_Reload*` w_magazine/w_bolt LOCAL per frame; kit local→comp delta = flip X,Y (w_frame is Y-180). Infima bone-local == component space (identity rots, `Grip` parent at origin).
2. **Mag follows the bone**: `BP_Weapon_AmericanRifle` UserConstructionScript, after `Parent: Construction Script` → `Attach Component To Component` (Target=`SM_AssaultRifle_Magazine`, Parent=`Item_Mesh`, Socket=`Magazine`, rules KeepWorld×3, Weld=false). Node handle `F0D12D5842619DC59F4AA7A13071D8A0`. SCS `AttachToName` is NOT scriptable (no python/NeoStack surface) — the UCS attach is the scriptable equivalent.
3. **Notify repoint (in-place)**: `Anim_Arms_AmericanRifle_Reload`/`_ReloadEmpty` `AN_WeaponAnim.Anim` → the two Infima anims above. In-place because BOTH play paths (weapon BP + our `AM_Kit_Rifle_Reload` montage) source these arms sequences; the kit originals (`Anim_Weapon_AmericanRifle_*`) are dead to us. If a second rifle-family gun (SMG) arrives sharing these arms anims, THAT is the point to duplicate per weapon.
4. **3P gun**: `BP_Rifle.WeaponMesh` (CDO subobject) = `SK_AssaultRifle_Frame` (same as `BP_EnemyAssaultRifle`). Frame only — no mag on the 3P gun (enemies ship the same way).

Known-unchecked: casing eject spawn point (`AN_WeaponBulletCasing` on the fire arms anim — Infima frame has no sockets; kit probably uses the weapon BP's `Muzzle`/component, user reports fire looks fine). Possible double-mag if `SK_AssaultRifle_Frame` turns out to contain skinned mag geometry (unlikely — Infima ships the mag as the separate SM we attached).

Next after PIE: grip/left-hand + gun position tuning (`DT_RifleAnimationValues` BasePoseLoc/Rot, NormalLeftHandLoc/Rot, F12 runtime UI) — needs user in the loop; then Phase 2 attachments.

## "Only the first reload animates" — long-standing bug, SOLVED (user-confirmed in PIE)

The reload arms+gun animation is owned by the KIT ITEM actor, not by any montage on the character:
`SpawnedItem` (spawned in `BP_ExtractionCharacter` `Event OnWeaponEquipped` from `GetKitVisualWeaponClass`) is a
`BP_Item_Base → BP_Weapon_AutomaticBase → BP_Weapon_AmericanRifle` actor. The char's `IA_Reload` → direct `Reload`
call on it → **`BP_Weapon_AutomaticBase` Event Reload** → gates `MaxAmmo <= 0` (reserve) and
`AmmoCount < WeaponCapacity` → Do Once → `AC_ProceduralAnimation.Play Animation` (dynamic slot montage on the
linked anim instance tagged `upperbody`, slot `defaultslot`) + plays the item's `WeaponAnim` on `Item_Mesh`.

**Root cause:** the kit item's LOCAL `AmmoCount`/`MaxAmmo` never deplete (our C++ owns firing), so the first reload
tops `AmmoCount` to `WeaponCapacity` and every later reload fails the `AmmoCount < WeaponCapacity` gate. (Item CDO
spawns at 12/120 — why reload #1 always passed.)

**Fix (WeaponBase.cpp, cpp-only):** two file-static reflection helpers —
- `SyncKitVisualItemAmmo()` after every `OnAmmoChanged.Broadcast`: pushes our `CurrentAmmo`/`ReserveAmmo` into the
  kit item via `FindFunction("SetAmmo")` + `ProcessEvent` (owner property `SpawnedItem` looked up by name; no-op for
  AI weapons whose owners lack it).
- `TriggerKitVisualItemReload()` in `Reload()`: C++-initiated reloads (auto-reload on empty / held-fire dry reload)
  never pass through the char's `IA_Reload` chain, so C++ fires the kit item's `Reload` event directly; the kit
  chain's Do Once + gates make the duplicate from the manual R path a no-op.

Gotchas discovered on the way: `BP_Item_Base` does NOT implement `KitWeaponInterface` (migration doc section A never
landed) — char→item calls are direct (SpawnedItem is BP_Item_Base-typed), and `KitSetAmmo` messages to the kit item
no-op. NeoStack `find_nodes` cannot surface another BP's functions/vars (action DB is context-blind) — cross-BP calls
are unauthorable via MCP; use the C++ reflection push pattern instead. The kit-item reload logic lives in
`Interactables/WeaponBases/BP_Weapon_AutomaticBase` — findable only by binary-grepping uassets (`findstr /M /S`).
