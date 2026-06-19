---
name: enemy-weapon-reload
description: Set up an enemy weapon so its magazine physically drops out and reseats during reload (KINEMATION weapon-mesh reload montage driving the gun's mag bone), synced to the character hand reload. Invoke whenever giving an enemy a new KINEMATION weapon's reload, wiring EnemyAnimSet.WeaponReload, "make the mag drop on reload", or adding/swapping an enemy gun that should reload visually. Proven on the AK105 (Grunt/Officer/Grenadier) and Mk14EBR (Sniper).
---

# Enemy Weapon Reload (magazine drop) Setup

The magazine drop is **not** the bullet-depletion ABP — that's a different asset (`A_W_<gun>_Mag` / `ABP_<gun>_Mag`, driven by an `Ammo` int) that only empties the visible rounds and **cannot remove the mag**. The removal comes from KINEMATION's per-weapon **weapon-mesh reload montage** (`AM_W_<gun>_Reload_Tactical`), which animates a **mag bone** on the gun skeleton; the `Mag` component rides that bone, so playing the montage on the gun mesh yanks the mag out and reseats it.

## The C++ is already done — weapon-agnostic, no build needed

`AWeaponBase` caches the gun visual's mesh (`WeaponVisualMeshName`, default `"WeaponMesh"`) at BeginPlay and plays `WeaponData->EnemyAnimSet.WeaponReload` on it from `UEnemyAnimInstance::PlayReloadMontage()`, at the **same `EffectiveRate`** as the character reload montage. `StopVisualWeaponReload()` tears it down at every reload-cancel site. **A new weapon needs DATA only — no code, no compile, no editor reboot.** All steps below are in-engine via VibeUE.

## Recipe (per weapon)

1. **Character reload montage.** The retargeted char reload is a *sequence* (`A_FP_<gun>_Reload_Tactical` on `SK_Military_Character_Skeleton`); the `reload` field needs a *montage*. Create `AM_Enemy_<gun>_Reload` from it **in slot `UpperBody`** (group `DefaultGroup`) — **NOT `DefaultSlot`**, or `ABP_Enemy_Grunt` won't play it. Assign to `<WeaponDataAsset>.EnemyAnimSet.reload`.
2. **Weapon montage.** Set `<WeaponDataAsset>.EnemyAnimSet.weapon_reload` = KINEMATION `AM_W_<gun>_Reload_Tactical` (group `DefaultGroup`).
3. **Visual.** Set `BP_Enemy<Weapon>.ThirdPersonVisualActorClass` = KINEMATION `BP_<gun>`. Keep `WeaponVisualMeshName` = `WeaponMesh` and `MagazineComponentName` = `None` (the old detach-to-hand path is dormant/unused).
4. **Verify the gun rig** (spawn a live edit-time instance — SCS/CDO template reads return `None` because KINEMATION sets mesh/ABP in the construction script): `WeaponMesh` = `SKM_<gun>` with anim class `ABP_W_<gun>` (or `ABP_<gun>`) that has a **DefaultSlot**; the `Mag` component attached to the gun's **mag bone**; gun component named `WeaponMesh`. Assign the ABP only if genuinely missing.

`EnemyAnimSet` is a **nested struct** — read it, set the sub-field, write the whole struct back, then save the asset.

## Gotchas (these are what bite)

- **Slot = `UpperBody`**, not DefaultSlot.
- **Mag bone name varies** — AK105 `Magazine_Parent`, Mk14EBR `Mag`. Confirm by sampling which bone the weapon montage actually translates (`get_bone_pose_for_time` across t).
- **Char & weapon montage lengths need NOT match.** They share the t=0 mocap origin; same-rate-from-reload-start aligns the mag pull with the hands, the char just has a longer tail. (AK105 matched 3.317/3.317; Mk14EBR doesn't, 4.133/3.633 — both sync fine.)
- **Find the real DataAsset path** via the weapon BP's `WeaponData` — they differ (`/Game/Core/Weapons/DA_AssaultRifle` vs `/Game/Core/Enemies/Weapons/Data/DA_SniperRifle`).
- **The weapon visual spawns at BeginPlay**, so it does NOT appear on a placed enemy in-editor — the mag drop and grip are **only visible in PIE (user playtest)**. Don't try to screenshot it in the editor; a static geometry check (weapon socket centered between `hand_l`/`hand_r`) is the most you can verify headless.
- **Grip alignment is separate.** Enemy hands are tuned per-weapon; a new gun may sit off (float/clip/rotation) and need a socket-alignment pass — flag it, don't block the reload work on it.
- Revolver / non-detachable-mag weapons: skip — no mag to drop.

## Verify before handing off

- Read-back: `EnemyAnimSet.reload` and `.weapon_reload` both non-None; `BP_Enemy<Weapon>.ThirdPersonVisualActorClass` = `BP_<gun>`; `Mag` on the mag bone; ABP has a DefaultSlot.
- User PIE checklist: empty the mag to force a reload → mag drops out and a fresh one seats, synced to the hands; then check grip alignment.
