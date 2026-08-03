# Infima weapons on the Procedural FPS Kit — rifle, then pistol

Branch: `FPS-pack-weapons`. Handoff plan — written so a fresh chat can execute cold.

## Context

Player weapons direction per roadmap: gameplay logic = our C++ (`AWeaponBase`), character + procedural arms = Procedural FPS Kit (Bodycam kit), weapon visuals + attachments = Infima Modern Guns Bundle. Goal of this effort: rifle fully working (equip / fire / reload with moving parts / ADS / attachment swapping), then the Infima Pistol, then the user buys a loadout menu from Fab once both are proven.

This branch is a **parallel experiment** alongside the Player-Setup KIN hybrid. Do not touch, merge, or reference Player-Setup work (the KIN presentation profiles, ABP_Manny islands, seat evaluator live only there). Plan and build entirely on FPS-pack-weapons.

### Architecture already in place (committed C++ + docs)

Three layers per weapon (documented in `agent_docs/weapon_mesh_pipeline.md`):
1. **Gameplay weapon** — `BP_Rifle` (C++ `AssaultRifle : AWeaponBase`): hitscan, damage, ammo, reload state machine, hidden owner-no-see 3P `WeaponMesh` (seen by companion/enemies).
2. **First-person visual** — kit `BP_Item_Base` subclass (`BP_Weapon_AmericanRifle`), spawned into the hands by the kit's `BP_FPCharacter`. Selected via `IKitWeaponInterface::GetKitVisualWeaponClass` → `UWeaponDataAsset::KitVisualWeaponClass`.
3. **Pose/ADS data** — `DT_RifleAnimationValues` (kit `DT_ProceduralAnimValues` type): hand placement, arms pose, ADS offsets. Via `GetKitProceduralValues` → `UWeaponDataAsset::KitWeaponPoseAsset`.

The kit's `BP_FPCharacter` drives any held weapon through `IKitWeaponInterface` message sends (KitBeginFire, KitReload, KitSetAmmo for HUD, etc. — see `KitWeaponInterface.h`). The bridge wiring steps are in `agent_docs/weapon_migration_phase2_inengine_handoff.md`.

### Current state (uncommitted!)

The whole existing adaptation lives as **uncommitted working-tree edits** to three uassets:
- `BP_Rifle` (Core/Weapons)
- `BP_Weapon_AmericanRifle` (kit visual — reportedly now shows the Infima AR, with ADS working via a `RearAimPoint` socket the user added to an Infima scope)
- `BP_AssaultRifle_Attachments_Example` (Infima modular-attachments demo BP)

User half-remembers the setup ("some sort of adaptation", "I think I did the Blender process for the rifle"). Ground truth must come from the editor, not memory.

### Infima pack reality (verified on disk)

- Guns are skeletal: `SK_<Gun>_Frame` + assembled `SK_<Gun>_<Variant>_Combined` meshes, plus separate attachment meshes driven by the `BP_<Gun>_Attachments_Example` BPs.
- The bundle ships **no gun animations** (only sniper-bipod open/close and TPP idle poses). Moving-parts reload can NOT be inherited from Infima assets.
- `ModernSMG/Meshes/SMG_RIGGED/SK_SMG_1` exists — likely output of the user's earlier Blender pass; audit must check which skeleton it binds (if it's the kit's `SK_AmericanRifle_Skeleton`, it's a reusable template/proof).

### Kit facts (from the dev's docs, both in `C:\Users\matth\Downloads\`)

- `BODYCAM & PROCEDURAL FPS KIT DOCUMENTATION.docx` (FAQ) and `Custom_Weapon_Rigging_Tutorial.md` (Blender re-rig walkthrough; kit dev is reachable for help).
- Procedural ADS = `FrontSight`/`RearSight` positions on the weapon BP; optics use `OpticSight`. This is what the user's `RearAimPoint` socket feeds.
- Weapon-mesh anims are triggered from arms anims via `ANS_WeaponAnim` notify; shell eject via `AN_WeaponBulletCasing`.
- New arms animsets need an `ENUM_Animset` slot + blend pose in `ABP_Manny` — NOT needed here: rifle and pistol animsets already exist in the kit.
- Per-weapon procedural tuning lives in a `ProceduralValues` DataAsset; **F12 in PIE opens the runtime tuning UI** (`UI_AnimationDebug`) — use it for recoil/pose tuning, save into the DA.
- Fire-anim blend in/out is tunable per weapon BP (per aim / per hip), no montage edits.
- `BP_CalculateWeaponSocketOffset` exists for anims that don't use `IK_Hand_Gun`.

### User constraints

- **No Blender if avoidable.** Preferred order: (1) make it work in-engine with Infima's own meshes; (2) if re-rigging is unavoidable, the agent drives the Blender MCP with user checkpoints; user may also try manually. Escalation: the kit developer.
- User runs all PIE tests themselves — hand them scenario + expected-outcome checklists.
- Plan-first, no unilateral builds. Commits stay on this branch, no pushes without instruction.

---

## Phase 0 — protect + audit (first session, mostly read-only)

1. **Commit the three modified uassets immediately** on FPS-pack-weapons (`WIP: Infima AR adaptation on kit weapon bridge`) — the adaptation currently exists nowhere else. Leave the untracked `__ExternalActors__/.../L_EnemyGym/2/KT/` files out of the commit (not this effort's work). Reconcile roadmap checkboxes per project rule.
2. Boot the editor if down (`boot-engine` skill), then map ground truth via VibeUE python (read-only; NeoStack fallback):
   - `BP_Weapon_AmericanRifle`: component tree — what mesh(es) actually render (Infima frame? child-actor of the attachments BP? still `SKM_AmericanRifle`?), where `FrontSight`/`RearSight`/`OpticSight` sit, how `RearAimPoint` is consumed.
   - `BP_Rifle`: which `UWeaponDataAsset` it uses (`DA_AssaultRifle`?) and that DA's `KitVisualWeaponClass` / `KitWeaponPoseAsset` values; what the hidden 3P `WeaponMesh` is.
   - `BP_AssaultRifle_Attachments_Example`: delta vs pack default (sockets added, components changed).
   - Skeleton bone lists: `SK_AssaultRifle_Frame`, `SK_Pistol_Default_Combined`, `SK_Pistol_Frame` — do they have mag/bolt/slide bones? Are magazines separate meshes/components rather than skinned parts?
   - `SMG_RIGGED/SK_SMG_1`: which skeleton it binds.
   - How the kit's own reload moves the AmericanRifle's parts (which `ANS_WeaponAnim` on which arms anim, which weapon AnimSequences) — this is the pattern the Infima visual must mirror.
   - Which pawn/map the user tests with and how the rifle gets into a slot (`BeginSetup` macro / `PrimarySlot` default).
3. Write findings to `agent_docs/fps_pack_weapons_audit.md` — it becomes the source of truth for phases 1–3 and the doc the kit dev can be shown when asking for help.
4. User PIE checklist: equip / fire / reload / ADS / HUD ammo — record exactly what works and what's broken today.

## Phase 1 — rifle: close the full loop (no Blender)

Work items expected (audit refines):
1. **Fire/ADS/HUD gaps**: verify every bridge call lands on the Infima visual — muzzle FX position, casing eject notify, `KitSetAmmo` HUD counts, fire blend in/out values.
2. **Moving-parts reload without Blender**: Infima mags are separate meshes → drive the mag at the BP level, synced to the kit arm-reload timing (hide/detach on the reload notify window, optional dropped-mag spawn, reattach at reseat; mirror timing from the kit's AmericanRifle reload). Bolt/slide: if the frame skeleton has a bolt bone, author a tiny AnimSequence in-engine (python AnimDataController keyframes or Sequencer bake — no Blender) or accept a static bolt for now.
   - **Fallback only if this looks bad in PIE**: Blender re-rig of the Infima AR onto `SK_AmericanRifle_Skeleton` per `agent_docs/weapon_mesh_pipeline.md` (kit reload/fire anims then work for free). Agent drives Blender MCP; get user approval before starting this.
3. **ADS formalization**: per-sight aim-point sockets (the `RearAimPoint` pattern) on each sight the rifle will use, and update `RearSight`/`OpticSight` when the sight changes. Verify socket additions persist to disk (save + reload — known MCP serialization pitfall).
4. **Hand/pose tuning**: `DT_RifleAnimationValues` `BasePoseLoc/Rot` + left-hand values; recoil/procedural feel via F12 runtime tuning UI saved into the ProceduralValues DA.
5. User PIE pass on the full loop → fix → commit.

## Phase 2 — rifle: attachment swapping

The bar for "done" (user decision): attachments swappable so the future loadout menu has something to drive.
1. Enumerate the Infima AR attachment inventory (scopes, grips, muzzles, mags, stocks) + slot sockets from the attachments BP.
2. **Ownership decision (audit-informed, use existing levers first)**: Infima's attachments BP already selects meshes per slot; the kit has `ST_Attachments`/`ENUM_Sights`. Prefer driving the Infima BP's existing selection vars; C++/`UWeaponDataAsset` only carries gameplay effects (recoil/spread/damage modifiers — roadmap wants "attachments with effects").
3. Build one entry point on the visual weapon: apply-attachment-set (per-slot mesh + ADS aim-point update + notify the C++ weapon of stat effects). This function is the contract the purchased loadout menu will call later.
4. User PIE pass (swap scope → ADS still aligns; swap muzzle/grip → no hand break) → commit.

## Phase 3 — pistol (Infima Pistol, `SK_Pistol_*` lane)

1. The kit already has a pistol lane: `BP_Weapon_Pistol`, `DT_PistolAnimationValues`, existing pistol animset — no `ENUM_Animset` work.
2. Mirror the rifle pattern: duplicate `BP_Weapon_Pistol` → Infima-visual version (frame + parts or a `_Combined` mesh, per audit); duplicate `DT_PistolAnimationValues`; create the gameplay side — new `UWeaponDataAsset` (`DA_Pistol`) + a BP on `AWeaponBase` (check whether `AWeaponBase` is concrete-spawnable or needs a thin C++ subclass like `AssaultRifle`; if C++ is needed, main chat writes it directly, review via `ue5-code-review` skill, then the close→build→reboot loop).
3. Slide + mag: `_Combined` meshes likely carry slide/mag bones — same in-engine anim-authoring route as the rifle bolt; otherwise component-level mag handling. Kit's own HeavyPistol shows the expected slide behavior to mirror.
4. ADS sockets + hand tuning as per rifle. Assign to `SecondarySlot`; verify weapon switching.
5. User PIE pass → commit.

## Phase 4 — loadout-menu readiness (purchase gate)

1. Ready = rifle + pistol full loop, attachment apply-function callable with a data payload, and `agent_docs/weapon_adding_recipe.md` written (per-new-weapon checklist: visual BP dup, DT dup, DA fields, sockets, tuning — the repeatable recipe for the rest of the arsenal).
2. Then the user buys the loadout menu; its integration is a separate plan against the attachment contract from Phase 2.

## Verification (every phase)

- Agent never runs PIE — numbered scenario + expected-outcome checklists to the user.
- Asset edits verified by save + disk reload where MCP serialization is suspect.
- Any C++ change: review first (inline, `ue5-code-review` skill), build only after review is clean, confirm `Result: Succeeded`, reboot editor before calling anything ready.

## Risks / edge cases

- Infima frame skeletons may have no part bones → bolt/slide stays static short-term; Blender fallback is the quality lever (user-approved only).
- Timing sync for BP-driven mag drop depends on how the kit fires `ANS_WeaponAnim` — if notifies aren't exposed cleanly, sync off the reload state (`GetKitReloading`) with tuned delays; audit decides.
- The user's earlier half-remembered work may conflict with a "clean" redo — audit before changing anything in the three modified BPs.
- Kit BP edits (`BP_FPCharacter` slot retyping) mean kit marketplace updates would clobber the adaptation — never "update the kit" casually; keep dev-supplied fixes as targeted edits.
- PIE locks BP edits (close PIE before editing); screenshot/Game-View and CDO-stale-on-placed-actors gotchas per `agent_docs/UnrealWorkflow.md`.

## Key paths

- Docs: `agent_docs/weapon_mesh_pipeline.md`, `agent_docs/weapon_migration_phase2_inengine_handoff.md`, `agent_docs/player_weapon_system_architecture.md`, `C:\Users\matth\Downloads\Custom_Weapon_Rigging_Tutorial.md`, `C:\Users\matth\Downloads\BODYCAM & PROCEDURAL FPS KIT DOCUMENTATION.docx`
- C++: `Extraction/Source/Extraction/Public/Weapon/KitWeaponInterface.h`, `Public/Weapon/WeaponBase.h`, `Public/Data/WeaponDataAsset.h` (Kit Weapon Bridge fields at ~line 243)
- Ours: `Extraction/Content/Core/Weapons/` (`BP_Rifle`, `BP_AssaultRifle`, `DA_AssaultRifle`)
- Kit: `Extraction/Content/ProceduralFPSKIT/Blueprints/` (`Interactables/AmericanRifle/*`, `Interactables/Pistol/*`, `BP_CalculateWeaponSocketOffset`, `Enums/ENUM_Animset`)
- Infima: `Extraction/Content/InfimaGames/ModernGunsBundle/` (`ModernAssaultRifle/`, `ModernPistol/`, `_Demo/Blueprints/Weapons/BP_AssaultRifle_Attachments_Example`, `ModernSMG/Meshes/SMG_RIGGED/SK_SMG_1`)
