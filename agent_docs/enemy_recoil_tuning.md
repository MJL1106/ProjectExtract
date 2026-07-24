# Enemy Firing Recoil — System & Tuning Guide

**What it is:** the visual body kick an enemy plays while firing (torso absorbs the recoil, gun stays gripped). Visuals-only — it does NOT affect accuracy, ammo, or fire logic. Single-player. A small C++ spring solver drives it; tuning is pure data on each weapon's DataAsset (no recompile to tune).

This replaced KINEMATION's `AC_RecoilAnimation` + 28-field `DA_RecoilData` (untweakable, FP-tuned, and the LMG read dead). Status: implemented, built, wired, **user-confirmed working 2026-06-20**.

---

## How it works (the pipeline)

1. **Per shot** — `AWeaponBase::OnWeaponFired` → `UEnemyAnimInstance::HandleWeaponFired()` → `AddRecoilImpulse()` adds a kick impulse to the spring's target (scaled down while aiming).
2. **Per frame** — `UpdateRecoilSolver()` eases the current kick toward the target (`Sharpness`), then decays the target back to zero (`RecoverySpeed`). It outputs two values: `RecoilSpineRotation` (an `FRotator` — the pitch lean + yaw/roll wander) and `RecoilSpineOffset` (an `FVector`, cm — the backward translational piston, driven by `WeaponKickback`).
3. **Apply** — `ABP_Enemy_Grunt`'s **last node before Output Pose** is an additive, component-space `Transform (Modify) Bone` on `spine_03`. Its **Rotation** pin reads `RecoilSpineRotation` and its **Translation** pin reads `RecoilSpineOffset` (both Add-to-Existing, **Component Space** — the translation space MUST match the rotation's, or the jolt mis-aims). Rotating + translating `spine_03` moves the whole upper body — torso, both arms, and the attached gun — as one unit, so the gun stays in the hands while the body leans and pistons back.

The spine kick — **rotation (lean) + translation (forward/back piston)** — is the whole visual; both are applied to `spine_03`, so the gun rides with the body. An earlier approach that moved the *weapon mesh* independently was removed (it slid the gun off the fixed right-hand grip — only the left hand IK-follows the gun); the spine translation does NOT have that problem because it carries the arms + gun along with the torso.

---

## Where to tune

**All tuning lives in ONE struct: `EnemyRecoilProfile` on each weapon's `UWeaponDataAsset`.**

1. Content Browser → `/Game/Core/Enemies/Weapons/Data/` → open the weapon's DA (e.g. `DA_LMG`).
2. In Details, expand **Enemy | Animation → Enemy Recoil Profile**.
3. Edit the fields, **Save**.
4. Re-Play. **No compile/build needed** — it's pure data, picked up on the next enemy spawn.

A brand-new weapon gets recoil automatically (the struct defaults to a rifle baseline). To customise, just set its profile — no C++, no Blueprint wiring.

---

## The knobs

| Field | Unit | What it does | Typical |
|---|---|---|---|
| `PitchKick` | deg | Main kick — how far the torso pitches back per shot. The headline "punch". | 2–7 |
| `YawKickMin` / `YawKickMax` | deg | Random horizontal kick per shot (random left/right sign), picked in this range. Side-to-side wander. | 0.5–3 |
| `RollKick` | deg | Random roll per shot (random sign). Adds organic twist. | 1–2 |
| `SpineKickScale` | 0–1 | Overall **body-kick intensity** — what fraction of the accumulated kick the spine applies. Lower = subtler whole-body kick. | 0.4–0.7 |
| `Sharpness` | 1/s | Attack speed — how fast the kick snaps in. Higher = snappier/punchier. | 18–26 |
| `RecoverySpeed` | 1/s | How fast the kick settles back to rest. Higher = quicker recovery between/after shots. | 5–13 |
| `AimRecoilScale` | 0–1 | Multiplier applied while the enemy is aiming (ADS). Lower = ADS kicks less. | 0.4–0.65 |
| `MaxAccumulatedPitch` | deg | Clamp on how far sustained auto-fire can stack the pitch, so a long burst saturates instead of leaning to the floor. | 8–16 |
| `WeaponKickback` | cm | **The forward/back piston** — how far the spine (whole upper body + gripped gun) jolts backward per shot. The headline knob for back-and-forth travel. Accumulates over sustained fire, clamps at 3×. | 2.5–9 |

**Single shot** peaks near `PitchKick`; **sustained auto** climbs and saturates near `MaxAccumulatedPitch`, then recovers at `RecoverySpeed` on cease-fire.

---

## Current per-weapon baseline (starting values)

| DA (archetype) | Pitch | YawMin | YawMax | Roll | **Kickback** | SpineScale | Sharp | Recover | AimScale | MaxPitch |
|---|---|---|---|---|---|---|---|---|---|---|
| `DA_AssaultRifleEnemy` (Grunt/Officer/Grenadier) | 7 | 0.8 | 1.8 | 1.0 | 3.5 | 0.6 | 24 | 11 | 0.55 | 14 |
| `DA_LMG` (Heavy) | 9 | 1.0 | 2.2 | 1.4 | 5.0 | 0.65 | 20 | 7 | 0.65 | 20 |
| `DA_SMG` (Rusher) | 4.5 | 0.6 | 1.4 | 0.8 | 2.5 | 0.5 | 28 | 15 | 0.5 | 11 |
| `DA_SniperRifle` | 12 | 0.5 | 1.0 | 1.2 | 9.0 | 0.6 | 18 | 5 | 0.4 | 12 |
| `DA_Revolver` (Pistol) | 7 | 0.7 | 1.5 | 1.0 | 4.0 | 0.5 | 26 | 13 | 0.6 | 9 |
| `DA_ShotgunEnemy` | 10 | 0.8 | 1.8 | 1.5 | 7.0 | 0.6 | 19 | 6 | 0.6 | 11 |

**2026-06-20 punchy retune** (translational piston added; user asked for punchier + forward/back focus). `Kickback` (cm) now LIVE = the backward-piston travel. Forward/back focus on the autos (AR/LMG/SMG) = high Pitch+Kickback, trimmed Yaw/Roll; sniper + shotgun = one big single thump (big Pitch+Kickback, slow recover, low accumulation). All 6 read-back-verified in the editor. Awaiting PIE (esp. the `-X` backward-axis check — see "If it kicks the wrong way").

---

## Feel recipes

- **More punch overall** → ↑`PitchKick` and ↑`SpineKickScale`; ↑`Sharpness` makes it snappier.
- **Heavy weapon** (LMG/MG) → ↑`PitchKick`, ↑`MaxAccumulatedPitch`, ↓`RecoverySpeed` (slow settle = weighty).
- **Light/snappy** (SMG/PDW) → ↓`PitchKick`, ↑`Sharpness`, ↑`RecoverySpeed`.
- **Single big thump** (sniper/shotgun) → ↑`PitchKick`, ↓`RecoverySpeed`, keep `MaxAccumulatedPitch` low (single shots don't accumulate much).
- **More side-to-side life** → ↑`YawKickMax` and/or `RollKick`.
- **Torso leans too far** → ↓`SpineKickScale` or ↓`MaxAccumulatedPitch`.
- **ADS should be tighter** → ↓`AimRecoilScale`.

---

## If it kicks the wrong way (sign/axis)

The C++ makes the pitch **negative** so the torso leans **back** (muzzle-climb feel). If a future change makes it kick forward/sideways wrongly, the fix is a one-field sign flip in `UEnemyAnimInstance::AddRecoilImpulse` (the `PitchKick`/`YawKick`/`RollKick` sign) — a `.cpp`-only change, Live-Coding patchable (Ctrl+Alt+F11, editor stays open). That's an engineer task, not a data tweak.

---

## Where everything lives

| Piece | Location |
|---|---|
| Tuning struct `FEnemyRecoilProfile` | `Extraction/Source/Extraction/Public/Enemy/EnemyTypes.h` |
| Per-weapon field `EnemyRecoilProfile` | `Extraction/Source/Extraction/Public/Data/WeaponDataAsset.h` |
| Solver (`AddRecoilImpulse`, `UpdateRecoilSolver`, `RecoilSpineRotation`) | `Extraction/Source/Extraction/.../Animation/EnemyAnimInstance.{h,cpp}` |
| Body-kick reset on death/swap | `EnemyAnimInstance.cpp` + `EnemyCharacter::HandleDeath` |
| ABP apply node | `/Game/Core/Enemies/Animation/ABP_Enemy_Grunt` — additive component-space `Transform (Modify) Bone` on `spine_03`, **last node before Output Pose**, reads `RecoilSpineRotation` |
| Per-weapon values | `/Game/Core/Enemies/Weapons/Data/DA_*` |

---

## Gotchas / history (read before changing the apply)

- **Verify the body kick in PIE, never the editor preview.** The MCP-captured AnimBP preview is blind to skeletal-control nodes — a 70° test probe showed *zero* movement in the preview but rendered correctly in PIE. Don't conclude "the spine node doesn't work" from a preview screenshot; check in-game. (See memory `pitfall-mcp-preview-skeletal-control-blind`.)
- **The gun must move with the body, not independently.** Only the left hand IK-follows the gun; the right hand is a fixed grip pose. So any transform applied to the *gun mesh* slides it off the right hand — that's why the old weapon-mesh kick was removed. To add an in-grip gun jolt later, IK the right hand to the gun too (or rotate the gun around the grip socket), don't just offset the gun mesh.
- **`WeaponKickback` is now LIVE** (was vestigial pre-2026-06-20). It drives `RecoilSpineOffset` — the backward spine piston (`FVector(-RecoilCurrentKickback, 0, 0)` in spine_03 component space). `AWeaponBase::SetRecoilOffset` is the part that's now orphaned (zero callers; safe to prune later).
- **4 orphaned `BP_*Recoil` event nodes** linger in `ABP_Enemy_Grunt`'s EventGraph (left over from the KINEMATION version). They're disconnected, dead, and compile clean — right-click → Delete in the editor to tidy whenever.

---

## How to verify (no automated test — user playtests in PIE)

- LMG sustained burst → torso kicks back, settles between shots; gun stays gripped.
- Each archetype kicks at a magnitude matching its weapon (SMG light/fast, sniper one big thump, etc.).
- Hold full-auto → pitch climbs then holds at the clamp, recovers on cease-fire (no frozen lean).
- ADS fire kicks less than hip fire.
- Fire while strafing → recoil layers over locomotion; no foot-slide or pose snap.
- Reload / death / melee → recoil resets cleanly, doesn't fight those poses.
- Edit one DA's `EnemyRecoilProfile`, save, re-Play → feel changes with nothing else touched.
