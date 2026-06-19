---
name: enemy-weapon-fire-align
description: Use when an enemy's gun looks right at rest but swings OFF-ANGLE WHILE FIRING — the fire montage is from a different pack than the grip, poses the firing hand differently, and the socket-attached gun cants/pitches during the burst. Eases the weapon toward a fire-tuned socket while the fire montage plays. C++ is built + weapon-agnostic; per weapon = one tuned socket + one ABP field. Triggers: "gun canted/pitched when firing", "fire montage doesn't match the gun", "weapon angle wrong when shooting", "angle the gun for the fire montage", "gun pops when fire starts".
---

# Enemy Weapon Fire Align (per-fire gun angle)

The visible gun snap-attaches to the enemy at `WeaponSocket` on **`hand_r`**, which follows the animated hand. A fire montage authored for a different weapon poses the hand differently than the rest grip → the gun swings off-angle during fire. Fix: while the fire LOOP montage plays, C++ **eases** the weapon's relative transform from the rest socket toward a fire-tuned socket (`WeaponSocket_Fire`, same bone) and back. For the *rest* grip use [[enemy-weapon-grip-blend]].

## The C++ is already done — weapon-agnostic, build only on the gotchas below

- `AWeaponBase::SetupFireAlign(EnemyMesh, FireSocket)` (caches the rest relative + the fire-relative once) + `SetFireAlignAlpha(float)` (blends WeaponMesh relative rest↔fire; weapon STAYS attached to `WeaponSocket`, no re-attach).
- `UEnemyAnimInstance::NativeUpdateAnimation` drives `FireAlignAlpha = FInterpTo(...)` toward `Montage_IsPlaying(GetEffectiveFireLoopMontage()) ? 1 : 0` and calls `SetFireAlignAlpha`. Reset to 0 in `AEnemyCharacter::HandleDeath` (before ragdoll) + `AWeaponBase::EndPlay`.
- Designer fields on the ABP (UEnemyAnimInstance, EditDefaultsOnly): `FireAlignSocketName`, `FireAlignBlendSpeed`.

A new weapon/enemy needs **asset/data only** (no code) — unless you're re-deriving the math, in which case read the gotchas.

## Three gotchas that each cost a build

1. **NOT an anim notify.** The fire loop re-chains its section (`Montage_SetNextSection("Default","Default")`), so a `UAnimNotifyState` fires End→Begin at every loop seam → gun flickers each loop. Drive off the montage's *play-state* in `NativeUpdateAnimation` instead (survives the loop). You cannot catch this in-editor — the loop only happens at runtime.
2. **NOT a hard re-attach.** Snapping the gun to the fire socket pops visibly at fire start/stop. Keep it on `WeaponSocket` and lerp a relative offset by an FInterpTo alpha.
3. **THE MATH (the one that bit hardest).** The socket-delta offset is **`T_fire * T_rest.Inverse()`**, NOT `T_rest.Inverse() * T_fire`. Both sockets are on the same bone, so `T_x = S_x * B`; only `T_fire * T_rest.Inverse() = S_fire * S_rest.Inverse()` cancels the bone `B` → constant + cacheable. The reverse order keeps `B` → it's **bone-pose-dependent**, so caching it bakes the setup-frame hand pose and the gun is canted during fire. UE convention: `child_world = relative * parent`, `A*B` = apply A then B. Final relative at alpha=1: `R1 = FireAlignRestRelative * (T_fire * T_rest.Inverse())`. `T_x = EnemyMesh->GetSocketTransform(x, RTS_Component)`. (Verifying by algebra alone is a trap — assuming `FireAlignRestRelative ≈ T_rest` is false; the captured relative is socket-relative ≈ identity.)

## Per-weapon recipe (asset/data only)

1. **Socket.** Add `WeaponSocket_Fire` on the **same bone as `WeaponSocket`** (hand_r), starting as an EXACT copy of `WeaponSocket` (no-op baseline). Put it on the **skeleton** `SK_Military_Character_Skeleton` so every enemy mesh on that skeleton inherits it; delete any per-mesh copy so the skeleton socket is the single source (else the un-tuned mesh socket shadows it).
   - **MCP CANNOT add/remove skeleton sockets** — the Skeleton Python API is locked (no `add_socket`, `Sockets` protected, `SocketName` read-only). SkeletalMesh sockets ARE scriptable (`mesh.add_socket`). So the skeleton socket is a **manual** step (Skeleton Tree → right-click `hand_r` → Add Socket → rename → set transform). Hand the user a checklist.
2. **ABP field.** Set the archetype ABP's `FireAlignSocketName` = `WeaponSocket_Fire`. Tune `FireAlignBlendSpeed` (default 12 ≈ 0.1–0.15s) for ease speed.
3. **Tune the angle** by eye against the fire pose (no PIE needed): open the body mesh `SKM_Character_<NN>` → select `WeaponSocket_Fire` → Details → Preview Asset = the gun's `SKM_<gun>` → Asset Browser → play the fire montage (`AM_Companion_Fire_Loop`, already on this skeleton — no retarget) → pause at the fire pose → rotate the socket's Relative Rotation until the gun sits in both hands. Save.

## Verify

User PIE: fire → gun eases into the tuned angle, holds level through the burst, eases back when it stops — no pop, no cant. Death resets it synchronously. If still off, get a screenshot + which way it's canted.

## Limits / extension points

- Alpha drives off the **loop** montage only — single-fire weapons (sniper/shotgun via `SingleFireMontage`) are a later opt-in.
- One `FireAlignSocketName` per ABP — different weapons needing different fire angles on the same ABP would need per-weapon socket selection (off `WeaponAnimType`).
- Only rotation is tuned by default; tune the socket **location** too if the gun floats positionally.

Related: [[enemy-weapon-grip-blend]] (rest grip), [[enemy-weapon-reload]] (mag drop).
