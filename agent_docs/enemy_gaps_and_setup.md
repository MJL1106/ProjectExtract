# EXTRACTION — Enemy AI: Gap Closure & In-Engine Setup

**Status:** Action plan. Written 2026-06-10 on `AI-Companion-Prototype`.
**Companion to:** `enemy_gameplay_as_built.md` (§11 is the evidence behind every gap here), `enemy_design.md`, `enemy_code_plan.md`, `enemy_test_levels.md` (test-level layouts).
**Scope:** every gap between the design doc and what runs today, how to close each one, the complete in-engine setup checklist, and the full asset list. Ordered by what blocks the first playtest.

**Who does what:** `[AGENT]` = the NeoStack editor loop can do it autonomously (DA edits, BP graph wiring, actor placement). `[HUMAN]` = needs a person (mesh/animation/VFX choice and import, aesthetic judgement). `[CLI]` = C++ work for a Claude Code session. Many `[HUMAN]` rows become `[AGENT]` once the asset itself exists in the project.

---

## 1. Priority overview

**P0 — blocks the first meaningful playtest.** **P1 — blocks it *feeling* right.** **P2 — design-completeness, mostly C++.**

| # | Item | Pri | Who | Effort |
|---|---|---|---|---|
| 1 | Bark lines: add `GrenadeOut` + `Suppressing` to DA_Barks_Grunt | P0 | AGENT | 2 min |
| 2 | DA_Enemy_Grunt `DisplayName` = "Grunt" (blank bark labels) | P0 | AGENT | 1 min |
| 3 | Sniper weapon: DA + BP child, assign to DA_Enemy_Sniper | P0 | AGENT | 15 min |
| 4 | Shield: real mesh + relative offset (cube currently blocks 360°) | P0 | HUMAN mesh, AGENT wiring | 30 min |
| 5 | Sniper laser beam bound to `OnLaserChanged` | P0 | HUMAN FX, AGENT wiring | 1–2 h |
| 6 | Grenade landing indicator bound to `OnGrenadeTelegraph` | P0 | HUMAN FX, AGENT wiring | 1–2 h |
| 7 | Meshes + ABP + physics asset + WeaponSocket on the 6 new BP children | P0 | HUMAN | 1–3 h |
| 8 | Spawn zones placed + DirectorConfig hookup + mission-phase triggers | P0 | AGENT (placement intent from human) | 30 min |
| 9 | `SquadId` on placed groups (incl. one officer-led squad) | P0 | AGENT | 10 min |
| 10 | NavMesh + stand-height cover slots over new arenas | P0 | HUMAN/AGENT | 30 min |
| 11 | Suppressed player weapon variant (stealth loop is untestable without it) | P0 | AGENT | 15 min |
| 12 | Ragdoll physics asset sanity check on the enemy mesh | P0 | HUMAN | 10 min |
| 13 | Hit-react / suppression-flinch / takedown montages bound to delegates | P1 | HUMAN | 2–4 h |
| 14 | Heavy plate-break visual on `OnPlateBroken` | P1 | HUMAN FX | 1 h |
| 15 | Rusher melee swing on `OnMeleePerformed` | P1 | HUMAN | 30 min |
| 16 | Grenade visuals: body mesh, trail, explosion FX + SFX on BP_EnemyGrenade | P1 | HUMAN | 1–2 h |
| 17 | Bark SFX per line (subtitle-first shipped; audio optional) | P1 | HUMAN | open |
| 18 | Heavy LMG weapon variant (identity currently = burst length only) | P1 | AGENT | 15 min |
| 19 | Per-archetype bark sets (all seven share DA_Barks_Grunt) | P1 | AGENT | 30 min |
| 20 | Delete `AEnemyBase.h/.cpp` (permission-blocked in autonomous run) | P1 | CLI | 5 min |
| 21 | Grenade blasts: AI-hearing noise + suppression + morale event | P2 | CLI | small |
| 22 | Officer aura: passive morale-floor raise + squad-only filter | P2 | CLI | small |
| 23 | Morale events: "outnumbered" (loss), "reinforcements arrived" (gain) | P2 | CLI | small |
| 24 | Lighting factor in sight fill | P2 | CLI | medium |
| 25 | Side-aware "Flanking left/right!" bark | P2 | CLI | small |
| 26 | Post-search "edgy" lingering state | P2 | CLI | small |
| 27 | Rusher suppression response — confirm "fearless" intent or add a read | P2 | decision first | — |

---

## 2. Gap-by-gap closure plan

### 2.1 — P0 blockers

#### Gap 1+2: silent telegraphs + blank speaker labels (data-only, do these first)

Two bark types were never authored (code requests them; the subsystem silently drops types with no lines), and the grunt's DisplayName is empty.

1. Content Browser: `/Game/Enemy/AI/DA_Barks_Grunt` — open.
2. `Barks` map: add key `Grenade Out` — Lines: "Grenade out!", "Frag going in!" — CooldownSeconds 6.
3. `Barks` map: add key `Suppressing` — Lines: "Suppressing — move up!", "Covering fire!" — CooldownSeconds 6.
4. Save. Open `/Game/Enemy/AI/DA_Enemy_Grunt` — Identity, `DisplayName` = "Grunt". Save.

Done when: grenadier wind-up prints "Grenade out!" in the bark feed; overwatch suppressor prints "Suppressing — move up!"; grunt barks show "Grunt" as speaker.

#### Gap 3: sniper fires a 25-damage rifle

A 2-second-telegraphed quarter-health-bar poke doesn't control sightlines. Give him his own weapon.

1. Duplicate `/Game/Core/Weapons/DA_AssaultRifle` to `DA_SniperRifle`.
2. Set: `BaseDamage` 90 (head ×2.0 = lethal on a 100 HP target; body = brutal-but-survivable — tune vs final player HP), `MagazineSize` 5, `NoiseLoudness` 1.0, `NoiseRange` 3500.
3. Duplicate `/Game/Core/Weapons/BP_Rifle` to `BP_EnemySniperRifle`; set its `WeaponData` = `DA_SniperRifle`.
4. Open `/Game/Enemy/AI/DA_Enemy_Sniper` — Weapon, `WeaponClass` = `BP_EnemySniperRifle`. Save.

Note: the sniper task fires exactly one round per telegraph (StartFiring+StopFiring) — fire rate doesn't matter, damage does.

Done when: a telegraphed body hit takes ~90% of a health bar; a head hit kills.

#### Gap 4: shield is an engine cube blocking 360°

The shield blocks by physical interception, and the runtime-created component sits at the mesh origin — until the mesh is replaced and pushed forward, the carrier is invulnerable from every angle and the archetype's counter (flanking) doesn't exist.

1. Import or pick a riot-shield static mesh (~120cm tall × 50cm wide; an engine/marketplace placeholder is fine if the pivot is on the grip side).
2. `/Game/Enemy/AI/DA_Enemy_Shield` — Shield, `ShieldMesh` = the new mesh. Save.
3. Open `/Game/Enemy/BP_Enemy_Shield` — Event Graph: add event `OnBoltOnComponentsReady` (BlueprintImplementableEvent on AEnemyCharacter — it fires right after the component is created at possess).
4. From it: `GetShieldComponent` → `SetRelativeLocation` (X=45, Y=0, Z=40) → `SetRelativeRotation` (yaw so the face points along actor forward). Adjust in PIE until side/rear shots land on the body.
5. Compile, save.

Done when: frontal shots visibly hit the shield (no damage), a shot from 90° hits the carrier, a grenade pops the shield (400 shield HP, radial ×2) and he drops to cover-fighting.

#### Gap 5: sniper laser is invisible

`OnLaserChanged(bool bOn, AActor* Target)` broadcasts at telegraph start/cancel/shot — nothing is bound. The design's one hard rule for this archetype ("never a sniper without it") is currently violated.

1. Create a Niagara beam system `NS_SniperLaser` (red beam, two user params: Start, End — the engine beam template works).
2. `/Game/Enemy/BP_Enemy_Sniper` — Event Graph: `GetSniperTelegraphComponent` → `Bind Event to OnLaserChanged`.
3. On `bOn=true`: spawn `NS_SniperLaser` attached to the weapon muzzle; on Tick-while-active (or a 0.05s timer), set End = Target eye location. On `bOn=false`: deactivate/destroy.
4. Optional but recommended: a quiet rising aim SFX during the 2.0s telegraph.

Done when: every sniper shot is preceded by 2 full seconds of visible beam, and breaking LOS or suppressing him kills the beam without a shot.

#### Gap 6: grenade flush has no visual telegraph

`OnGrenadeTelegraph(FVector Landing, float TimeToImpact)` and `OnGrenadeCancelled` are exposed on the grenadier component — unbound. With Gap 1 fixed you get the bark; this adds the ground marker.

1. Create a decal material (or Niagara ring) `M_GrenadeWarning` — red ring, ~350 radius (the blast radius).
2. `/Game/Enemy/BP_Enemy_Grenadier` — Event Graph: `GetGrenadierComponent` → `Bind Event to OnGrenadeTelegraph` → spawn decal at Landing, lifetime = TimeToImpact; bind `OnGrenadeCancelled` → destroy it early.
3. Compile, save.

Done when: camping >4s against a grenadier paints a ring on your cover roughly 3.5–4.5s before detonation (1s wind-up + flight + 2.5s fuse).

#### Gap 7: six BP children have no mesh/anims

1. For each of `BP_Enemy_Rusher/Heavy/Sniper/Officer/Grenadier/Shield`: Mesh component → assign the soldier/mannequin skeletal mesh; `Anim Class` = the grunt's ABP.
2. Until distinct meshes exist, make 6 material-instance tints (e.g. Rusher red, Heavy dark, Sniper pale, Officer trim, Grenadier olive, Shield blue) so archetypes read at a glance — legibility is a design pillar.
3. Verify the skeleton has a `WeaponSocket` (right hand); without it the rifle attaches to the capsule root.
4. Heavy: consider mesh scale 1.1 until a real heavy mesh exists.

Done when: all seven types animate, hold the rifle correctly, and are tell-apart-able at 1500cm.

#### Gap 8+9+10: the level doesn't feed the systems yet

Without spawn zones the director never spawns (one warning log). Without `SquadId`s, placed enemies have **no squad behaviour at all** — no shared sightings, no flanking, no focus fire, no bounding (easy to misread as "AI broken"). Mission phases only change when level scripting says so.

1. Place 3+ `AEnemySpawnZone` actors at out-of-sight ingress points (behind buildings, stairwells, map edge); size the box, keep it over navmesh. `ActivePhases` empty = all phases.
2. On ONE zone set `DirectorConfig` = `/Game/Enemy/AI/DA_DirectorConfig` (or call `SetDirectorConfig` from the level BP at BeginPlay).
3. Level BP: objective trigger → get `EnemyDirectorSubsystem` → `SetMissionPhase` Objective; extraction trigger → `SetMissionPhase` Extraction; optional alarm interactable → `TripAlarm`.
4. Placed groups: select each enemy, set `SquadId` per group ("Alpha", "Bravo"...). Build one officer-led squad: 1 Officer + 3 Grunts sharing one id (bounding needs the officer plus ≥2 grunt-archetype members in combat).
5. Each patrol enemy: assign its `PatrolRoute` actor (3+ points).
6. NavMesh bounds over patrols, cover, zones, flank ring (~900cm around likely player positions). Sniper arenas need unclaimed **stand-height** cover slots within 3000 of his perch-search origin and a long sightline.
7. Remember the cap trap: Infiltration `MaxAlive` is 8 and **placed enemies count** — a 10-enemy stealth level gets zero reinforcements until kills open headroom.

Done when: going Loud spawns a grunt pair within ~45s from off-screen; they arrive Searching toward you; two squads don't share sightings; the officer squad bounds.

#### Gap 11: no suppressed player weapon

`bSuppressed`, `NoiseLoudness`, `NoiseRange` exist on `UWeaponDataAsset` — no variant uses them. The stealth counter-loop (suppressed kills that don't alert the camp) is untestable.

1. Duplicate the player's rifle/pistol DA → `DA_<Weapon>_Suppressed`: `bSuppressed` true, `NoiseLoudness` 0.15, `NoiseRange` 400 (vs 1.0/3000 unsuppressed).
2. Duplicate the weapon BP, assign the DA, add it to the player loadout path used in the test level.

Done when: an unsuppressed shot alerts enemies within 3000 (+30 suspicion each); the suppressed variant only nudges enemies within 400.

#### Gap 12: ragdoll check

1. Open the enemy skeletal mesh → confirm a physics asset is assigned and its `Ragdoll` collision profile tumbles (kill one in PIE).
2. Takedown kills ragdoll 0.8s late by design (anim window) — that pause is normal.

### 2.2 — P1 polish (the game works without these; it doesn't feel finished)

- **Hit feedback** (design §8 "the player must *see* shots land"): bind `OnHitReact(EHitRegion)` to flinch montages (head gets its own), `OnSuppressedStateChanged(true)` to a duck/cower additive, `OnTakedownExecuted` to the enemy-side takedown reaction (you have 0.8s before ragdoll).
- **Heavy plates**: `OnPlateBroken(PlatesRemaining)` → chunk burst FX + material swap per stage (3 plates → 3 stages). This is also gameplay feedback: it tells the player frontal damage is working.
- **Rusher melee**: `OnMeleePerformed` → swing montage + impact SFX.
- **Grenade**: body mesh + trail on `BP_EnemyGrenade`, explosion Niagara + SFX on detonate.
- **Heavy LMG variant**: duplicate rifle DA — `MagazineSize` 100, `BaseDamage` 18–20, `NoiseRange` 3200 — so his 2–3.5s bursts stop being mag-clipped at 3.0s and he reads as a weapons platform, not a grunt who holds the trigger.
- **Per-archetype bark sets**: duplicate DA_Barks_Grunt per type, re-voice lines (officer: commands; rusher: aggression; sniper: muttering). Assign in each `DA_Enemy_*` → Barks → `BarkSet`.
- **Bark SFX**: optional `Sound` per FBarkDefinition line set — subtitle text is the shipped contract, audio is gravy.

### 2.3 — P2 design-completeness (C++ — hand to a Claude Code session)

| Item | Sketch |
|---|---|
| Grenade blast: noise + suppression + morale | `AEnemyGrenadeProjectile::Detonate`: `ReportNoiseEvent` (loudness 1.5, range 3500); radius-query AI pawns → `RegisterNearMiss(2.0)`; optional direct morale event so "nading his cluster dents squad morale" works without damage. |
| Officer passive morale floor | `USquadAuraComponent::Scan` also calls a `SetAuraFloor(25)` on members' morale comps (clear on leave/death) — design §7 "raises nearby allies' floor". DA field exists conceptually via `MoraleFloor`, currently 0 everywhere and never aura-driven. |
| Aura squad filter | Same scan: skip members whose `SquadId` differs from the officer's (today it buffs any enemy within 1500). |
| Outnumbered / reinforcements morale events | Squad-level headcount vs visible hostiles on the 1s morale tick (loss), `UEnemySquad` creation near an existing engaged squad → gain broadcast. Both have DA-weight homes already (add two fields). |
| Lighting sight-fill factor | Needs a light-level probe (cheap: precomputed light volume samples or surface luminance at target) × DA factor — medium effort, design §4 lists it, P2 notes deferred it. |
| Side-aware flank bark | `BTTask_EnemyFlank` knows the chosen ring point — compare its bearing vs target facing, pick "Flanking left!"/"right!" line variant (needs 2 extra bark lines too). |
| Post-search edginess | After Searching times out, hold a 30–60s "edgy" flag: suspicion fill ×1.5, patrol speed = combat speed. One float + timestamp in the awareness component. |
| Rusher vs suppression | Decision first: design says "barely suppresses" — current build = spread-only effect, charge never stops. If the companion-pins-the-rusher beat matters, add a brief stumble at full suppression; otherwise document as intended. |
| Delete `AEnemyBase` | Remove `Public/Enemy/EnemyBase.h` + `Private/Enemy/EnemyBase.cpp` (self-referenced only), delete any placed `BP_EnemyBase`, build. |

---

## 3. Consolidated in-engine session (run top to bottom)

**3.1 Data (10 min):**
1. DA_Barks_Grunt: add `GrenadeOut` + `Suppressing` entries (lines above).
2. DA_Enemy_Grunt: `DisplayName` "Grunt".
3. Create `DA_SniperRifle` + `BP_EnemySniperRifle`; wire into DA_Enemy_Sniper.
4. Create suppressed player weapon DA + BP.
5. Optional: `DA_LMG` + BP for the heavy.

**3.2 Blueprint bindings (the five delegates):**
6. BP_Enemy_Sniper: `OnLaserChanged` → laser beam on/off.
7. BP_Enemy_Grenadier: `OnGrenadeTelegraph`/`OnGrenadeCancelled` → ground ring.
8. BP_Enemy_Shield: `OnBoltOnComponentsReady` → shield component offset; DA_Enemy_Shield `ShieldMesh` → real mesh.
9. BP_Enemy_Heavy: `OnPlateBroken` → plate FX (P1).
10. All enemy BPs (or the shared ABP): `OnHitReact`, `OnSuppressedStateChanged`, `OnTakedownExecuted`, `OnMeleePerformed` → montages (P1).

**3.3 Characters:**
11. Assign mesh + ABP + tint MI on the 6 new BP children; verify `WeaponSocket`; verify physics asset.

**3.4 Level:**
12. Spawn zones ×3+, one carrying DA_DirectorConfig; phase triggers in level BP; PatrolRoutes; SquadIds (incl. officer squad); navmesh; stand-height cover for sniper arenas.

**3.5 Smoke test (15 min, PIE):** follow `enemy_test_levels.md` layouts; the per-phase QA lists live in `enemy_code_plan.md`. Minimum pass: grunt patrol→spot→cover→fire; rusher charge+melee; heavy front-shrug/rear-melt/slow-turn; sniper laser→shot→relocate; officer aura+focus+rally+bounding; grenadier 4s-camp→ring+bark→lob; shield block→break→fallback; officer death → audible squad break; go-Loud → director waves; suppression ducks a peeker.

---

## 4. Complete asset list

### Characters & animation

| Asset | Status | Used by | Notes |
|---|---|---|---|
| Soldier skeletal mesh (shared OK) | **Missing on 6 BPs** (grunt = mannequin) | all BP_Enemy_* | UE5 Quinn/Manny placeholder fine |
| 6 tint material instances | Missing | per archetype | cheap archetype legibility until real meshes |
| ABP_Enemy (locomotion) | Exists for grunt — reuse | all | walk/run/crouch + aim offset |
| Physics asset (ragdoll) | Verify | all | death + takedown (delayed 0.8s) |
| `WeaponSocket` on skeleton | Verify | all | else rifle attaches to capsule |
| Flinch montages (head + body) | Missing | `OnHitReact(EHitRegion)` | P1 |
| Suppression duck/cower additive | Missing | `OnSuppressedStateChanged` | P1 |
| Takedown reaction montage | Missing | `OnTakedownExecuted` | 0.8s window before ragdoll |
| Melee swing montage | Missing | `OnMeleePerformed` (rusher) | P1 |
| Death anims | Not needed | — | ragdoll covers it |

### Weapons

| Asset | Status | Used by | Notes |
|---|---|---|---|
| `DA_SniperRifle` + `BP_EnemySniperRifle` | **Missing — P0** | DA_Enemy_Sniper | dmg 90, mag 5, noise 1.0/3500 |
| Suppressed player weapon DA + BP | **Missing — P0** | stealth testing | bSuppressed, 0.15/400 |
| `DA_LMG` + BP (heavy) | Optional P1 | DA_Enemy_Heavy | mag 100, dmg 18–20 |
| Riot shield static mesh | **Placeholder (engine cube) — P0** | DA_Enemy_Shield `ShieldMesh` | pivot at grip; offset via `OnBoltOnComponentsReady` |
| BP_Rifle / DA_AssaultRifle | Exists | all seven today | 25 dmg, 10rps, 30 mag, noise 1.0/3000 |

### VFX

| Asset | Status | Used by | Notes |
|---|---|---|---|
| `NS_SniperLaser` beam | **Missing — P0** | `OnLaserChanged` | red beam muzzle→target, 2.0s window |
| Grenade warning ring (decal/Niagara) | **Missing — P0** | `OnGrenadeTelegraph(Landing, ETA)` | ~350 radius = blast radius |
| Grenade body mesh + trail | Basic/missing | BP_EnemyGrenade | P1 |
| Explosion Niagara + impact decal | Missing | grenade detonate | P1 |
| Plate-break chunks/material stages | Missing | `OnPlateBroken(N)` (heavy) | P1, doubles as damage feedback |
| Muzzle flash / tracers | Exists | weapon `Multicast_PlayFireFX` | — |

### Audio (all optional — subtitles are the shipped contract)

| Asset | Used by |
|---|---|
| Bark VO/SFX per `FBarkDefinition.Sound` | bark feed |
| Explosion SFX | grenade |
| Sniper aim hum (rising, 2s) | laser telegraph readability |
| Melee impact | rusher |

### Data edits (no new assets)

| Edit | Pri |
|---|---|
| DA_Barks_Grunt += `GrenadeOut`, `Suppressing` | **P0** |
| DA_Enemy_Grunt `DisplayName` | **P0** |
| Per-archetype BarkSet DAs | P1 |

### Level actors (placement, not assets)

| Item | Notes |
|---|---|
| `AEnemySpawnZone` ×3+ | off-sightline ingress; ONE carries `DirectorConfig=DA_DirectorConfig` |
| Mission-phase triggers (level BP) | `SetMissionPhase` Objective/Extraction; optional `TripAlarm` |
| `APatrolRoute` per patroller | 3+ points |
| `SquadId` per placed group | incl. one officer + 3 grunts squad for bounding |
| NavMesh bounds | patrols, cover, zones, ~900cm flank ring around fight spaces |
| `AICoverSlot` coverage | sniper arenas need **stand-height** slots within 3000 + long sightlines |
| Test levels | layouts in `enemy_test_levels.md` (EnemyGym zones + ExtractionSlice) |

---

## 5. Definition of done (P0)

| Fix | Proven by |
|---|---|
| Bark lines + DisplayName | Grenadier wind-up and overwatch pin both print labelled subtitles |
| Sniper weapon + laser | Laser paints you 2.0s, then a shot that actually threatens; suppress him → beam dies, no shot; he relocates after 2 |
| Shield mesh/offset | Frontal blocked, 90° flank kills, grenade pops the plate |
| Meshes/ABP | Seven visually distinct enemies animating, rifles in hands |
| Zones/phases/SquadIds | Loud → first wave ~45s from off-screen arriving Searching; officer squad bounds; "Bravo" keeps patrolling while "Alpha" fights |
| Suppressed weapon | Camp stays Calm through a suppressed kill at range; body discovery still escalates |
| Ragdoll | Deaths tumble; takedown ragdolls after the 0.8s anim window |

Full per-phase QA checklists: `enemy_code_plan.md` (Phases 1–7) and `enemy_gameplay_as_built.md` §10 for the intended end-to-end feel.
