---
name: enemy-weapon-grip-blend
description: Use when an enemy's HELD weapon looks wrong at rest — hands not on the grip/foregrip, gun floating/clipping, robot-wrist hold, or you're putting a new weapon's hold pose on an enemy. Layers a weapon-correct arm pose (e.g. a KINEMATION A_FP_<gun>_Idle) onto the enemy's arms only while keeping locomotion legs/torso. In-engine ABP only, no C++. Triggers: "hands don't grip the gun", "fix the weapon hold/grip", "use the arms from this anim", "layer the grip pose", "gun floats in the hands".
---

# Enemy Weapon Grip Blend (arms-from-grip over locomotion)

A **Layered blend per bone** in the enemy ABP takes the ARMS from a weapon-correct grip clip while legs, torso bob, and root stay from the locomotion blendspace. No C++. This is the *rest/hold* grip — for the gun swinging off-angle DURING FIRE use [[enemy-weapon-fire-align]] instead.

## Prerequisite — same skeleton

The grip clip MUST be on the enemy skeleton `SK_Military_Character_Skeleton`. KINEMATION FP body clips (`A_FP_<gun>_Idle`, despite the "FP" name they're full-body on UE5 Manny) need a Manny→Quantum retarget first — **user owns retargeting, verify by screenshot not metadata** ([[project_enemy_retarget_pipeline]]). Same skeleton ⇒ the blend works with no further retarget. Confirm via VibeUE: `load_asset(clip).get_editor_property('skeleton').get_name() == 'SK_Military_Character_Skeleton'`.

## Recipe (in `ABP_Enemy_Grunt`, shared by all rifle archetypes)

`ABP_Enemy_Grunt` is a **cached-pose graph, NOT a linear chain** — it already has ~3 `LayeredBoneBlend` nodes (montage slots + aim offset) and saves the locomotion to `CachedLocomotion`, which feeds BOTH the final output blend's base AND the montage/aim lineage.

1. Insert one `Layered blend per bone` between the **Locomotion State Machine pose** and the **Save-cached-pose `CachedLocomotion`** node (grip at the source so it propagates everywhere).
   - Base Pose = the locomotion pose.
   - Blend Poses[0] = a Sequence Player of the grip clip (`A_FP_<gun>_Idle`), looping.
   - Layer Setup → Branch Filters: `clavicle_l` (depth 0) + `clavicle_r` (depth 0) — masks both full arms + hands.
   - Blend weight/alpha = 1.0 constant (no driving variable).
2. Compile + save. The additive aim offset downstream still rides on the gripped arms; fire/reload montages still override the arms while they play.

## THE KEY GOTCHA — gun aims up? turn on Mesh Space Rotation Blend

With a clavicle-rooted (arms-only) mask + LOCAL blend, the arms inherit the locomotion **spine's** pitch, so the gun cants up/down. Set **`Mesh Space Rotation Blend = true`** on the node → the masked arms take the grip's MESH-space (component-relative) orientation regardless of spine pose → gun sits level, and the torso still bobs with locomotion (because the mask is still arms-only).

Alternative (only if you WANT the whole upper body static): re-root the branch filter to a single `spine_01` entry — takes torso+arms from the grip, but loses torso locomotion motion. Prefer Mesh Space Rotation Blend.

## Verify

- ABP preview, `Speed > 0`: legs/torso animate, both hands on grip + foregrip, gun level, no wrist twist at the clavicle seam.
- Set `bIsAiming` + sweep `AimYaw/AimPitch`: arms keep the grip and still track aim.
- View an enemy in `L_EnemyGym` and screenshot (front + 3/4). Editor screenshots need this project's editor focused (another foreground UE project starves VibeUE viewport capture).
- Hand the user a press-Play checklist for moving confirmation (you don't run PIE).

## Per-weapon / per-archetype

The grip clip can be switched per weapon by feeding Blend Poses[0] from a pose selector driven by the existing `UEnemyAnimInstance::WeaponAnimType` signal — no new node. Out of scope unless asked.

Related: [[enemy-weapon-fire-align]] (gun angle during fire), [[enemy-weapon-reload]] (mag drop).
