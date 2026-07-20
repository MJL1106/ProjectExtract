---
name: kit-weapon-blender-rerig
description: Re-rig a marketplace weapon (Infima-style modular gun) onto a Procedural FPS Kit weapon skeleton via the Blender MCP, so kit hand poses, reload/fire anims, and moving parts work out of the box. Use when putting a new gun on the kit rifle/pistol skeleton, when a kit weapon's mag/bolt won't animate, or for the polish loop that nudges a re-rigged gun's position from a user PIE reference. Proven end-to-end on the Infima AR → SK_AmericanRifle_Skeleton (07-2026).
---

# Kit weapon re-rig (Blender MCP + VibeUE)

Whole flow is driveable from the CLI: UE exports → Blender MCP → UE deferred import → verification. No hand-clicking except user PIE judgment.

## Why this works
The kit's char anims (arms + gun) are authored against the kit weapon skeleton (`w_frame` root + `w_magazine`, `w_bolt`, `w_charginghandle`, `w_trigger`, `w_selector`, `w_dustcover`, `w_magrelease`, `w_boltrelease`, `w_sparemag`, `w_bullet` for the rifle). Skin the new gun's geometry to those bones at the kit gun's grip position and every kit anim + hand pose just works. Gameplay stays on the C++ bridge; only the item BP's `Mesh` var changes.

## Phase 1 — assemble the donor gun in UE, export everything
1. Donor parts (frame SK + fixed static parts like handguard/mag) placed in an empty level at the BP's component offsets → select → `File → Export Selected` = ONE pre-assembled FBX (bakes relative placement; sidesteps all axis-conversion math).
2. Export the kit template SKM (e.g. `SKM_AmericanRifle`) via Asset Actions → Export. This is the rig + alignment target.
3. Only bake FIXED parts. Anything the kit modding system spawns (sights, scope, suppressor, laser, foregrips) stays a component or you get doubles.

## Phase 2 — Blender (via `mcp__blender__execute_blender_code`)
1. Import kit FBX first, then the assembled-donor FBX. Delete `UCX_*` collision meshes.
2. Strip donor rig: select donor mesh → clear parent KEEP TRANSFORM → delete donor armature + its empty.
3. **Vertex groups: rename, never repaint.** Donor part groups map 1:1 (`Magazine→w_magazine`, `Bolt→w_bolt`, `Charging_Handle→w_charginghandle`, `Trigger→w_trigger`, `Dust_Cover→w_dustcover`, `Magazine_Release→w_magrelease`, `Fire_Selector→w_selector`). **Delete the body group** (e.g. Infima `Grip`) — kit convention = body verts UNWEIGHTED (UE binds them to the root bone on import). No `w_frame` group, no `w_sparemag` geometry (the insert mag is the item BP's `SpareMagMesh`, not skinned geometry).
4. Static parts joined in: mag verts → all-1.0 `w_magazine`; handguard → no groups. Join order: parts first, frame LAST (active).
5. **⚠️ UV merge after join** — multi-FBX joins leave multiple UV layers (`DiffuseUV` + `UVmap_0` + `LightMapUV`); UE reads layer 0 only → joined parts render degraded/washed. Copy each part's UVs into layer 0 by material-slot loops, delete extra layers.
6. Align: translate the whole gun so its trigger/grip sits exactly on the kit gun's (compute first pass from the two Trigger bones' world positions; overlay-screenshot with per-object viewport colors + x-ray for the eyeball check).
7. **⚠️ NEVER `transform_apply` the kit armature.** The kit's `w_frame` -180° Z lives on the armature OBJECT (`rot (0,0,-3.142)`); flattening bakes a wrong bind pose — the mesh looks fine statically and flies apart the moment any anim evaluates. If transforms were touched, re-import the kit FBX fresh and re-parent to the untouched armature.
8. Parent mesh → kit armature With Empty Groups (`parent_set(type='ARMATURE_NAME')`). Pose-test: rotate `w_magazine` — only the mag may move.
9. Export: selected only, Armature+Mesh, `-Z forward / Y up`, `add_leaf_bones=False`, `mesh_smooth_type='FACE'`, `apply_unit_scale=True`. Save the .blend — it is the permanent polish-loop source.

## Phase 3 — UE import + wiring (VibeUE python)
1. **FBX import MUST use the slate-post-tick deferred pattern** (UnrealWorkflow §1.15) — direct `import_asset_tasks` in `execute_python_code` = TaskGraph assertion crash. Options: `import_as_skeletal`, `skeleton = <kit skeleton>`, `import_animations=False`, `import_materials/textures=False`, `normal_import_method = FBXNIM_IMPORT_NORMALS` (recomputed normals soften machined edges).
2. Re-point material slots to the donor's MIs (build fresh `SkeletalMaterial` array + `set_editor_property('materials', ...)` — in-place struct edits don't persist).
3. Item BP (duplicate of the kit weapon BP, kit original untouched): `Mesh` = new SKM; `ik_hand_gun_SocketOffset` = stock zero; delete any SCS part comps + UCS socket-attach hacks (stock UCS = entry → parent call only); `SpareMagMesh` = donor mag SM **with rel rot roll +91.7°** (kit mag bones rest at roll −91.7 in component space; kit's own SM is pre-rotated, donor's isn't). Force always-bolt-cycle reloads by pointing `Arms_ReloadAnim` at the Empty arms anim (the gun anim rides the arms anim's `AN_WeaponAnim` notify).
4. Spawn lever: our `WeaponDataAsset.KitVisualWeaponClass` → the new item BP.

## Verification (do these, they caught every real bug)
- **Bind-pose roundtrip:** export the imported SKM back to FBX, import into Blender, diff every bone's world rest matrix vs the kit armature — must be 0.0.
- **Anim check in UE:** preview the kit reload on the SKM — mag drops + reinserts, bolt slams; a mag that vanishes = bind-pose mismatch (see Phase 2 ⚠️).
- **Ground truth if hands look wrong:** hand pose ≠ gun mesh. Measure live in PIE: `hand_l` position in gun space (make_relative_transform of Item_Mesh world vs `ik_hand_gun` socket). Kit-correct rifle reference: hand_l ≈ (7.6, 11.8, −1.2). If the measurement matches but looks wrong, suspect the project's anim-stack assets diverging from kit-stock — diff CDO T3D exports and AnimPose samples against a pristine kit project before touching the gun.

## Polish loop — TWO different levers, pick by intent
The kit IKs the hands onto the ITEM, so F8-moving the item in PIE moves gun **and hands together**.
- **User F8-placed the item in PIE ("gun should sit here")** → that's an ASSEMBLY move: read `item.root_component.relative_location/rotation` and write it into the item BP's `ik_hand_gun_SocketOffset` transform (translation cm + rotation). Do NOT bake this into the mesh — mesh baking shifts gun-vs-hands and reads as "the hands moved".
- **Gun wrong relative to the HANDS (palm floating off the handguard, trigger finger misses)** → THAT's the Blender mesh bake (kit doc: "do not fix this in Unreal"): `mesh.location += (UE_X, −UE_Y, UE_Z) / 100` (UE comp → Blender axis map; Z is 1:1), save .blend, re-export, deferred reimport (materials/slots survive `replace_existing`).

## Misc gotchas
- Editor-locked uassets can't be overwritten by file copy — reimport the FBX inside that editor instead.
- BP graph edits and FBX imports fail during PIE — have the user exit first.
- `AssetExportTask` T3D of a **CDO** carries defaults + component templates; of the **BP asset** carries graphs. One object per file — a second export to the same filename overwrites.
- Validate a re-rig in a pristine kit project when available: point their weapon BP's `Mesh` at the SKM (copied uasset; skeleton path must match). Grey materials are expected and irrelevant.
