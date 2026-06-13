# EXTRACTION — Enemy AI: In-Engine Setup & Testing Manual

**Per-phase, step-by-step.** Written 2026-06-10 on `AI-Companion-Prototype`.
**What this is:** everything that still happens *in the editor* to take Phases 1–7 from "code landed" to "playtested", phase by phase: the assets you need, the exact setup clicks, the test levels to build, and a test script for every QA scenario with expected results and the most likely cause when it fails.
**Sources:** live editor read-back of all `/Game/Enemy/AI` assets (this morning — wiring states below marked **DONE** were verified byte-level), `enemy_code_plan.md` (QA lists), `enemy_test_levels.md` (zone layouts), `enemy_gameplay_as_built.md` (every number).

**Status legend used throughout:** **DONE** = verified wired this morning · **VERIFY** = probably done in the Phase 1–2 handoff but never read back — check it · **MISSING** = must be created · **PLACEHOLDER** = exists but wrong (blocks its test).

---

## Part 0 — One-time prerequisites (do before any phase testing)

### 0.1 The two maps

Build **two** maps and keep both forever — don't make seven throwaways.

1. `L_EnemyGym` — flat modular test map. One labelled zone per system (Zones A–G, added phase by phase, old zones kept for regression). No art: BSP/cube floors, TextRender labels on the ground.
2. `L_ExtractionSlice` — a compressed three-act mini level that grows a wing per phase: ACT A approach/perimeter (stealth) = open yard + fence + 2 entries; ACT B interior objective = 2 floors + connecting stair + small atrium; ACT C extraction = the route back OUT through B/A to an exfil pad in the yard.

The gym answers "does the system work?" in 30 seconds. The slice answers "does it feel like an extraction shooter?" The slice is the dissertation demo level in embryo — effort there transfers.

### 0.2 Authoring rules the code enforces (read before placing anything)

1. **NavMesh everywhere.** Every patrol point, cover slot, noise spot, and spawn zone on *connected* navmesh (place NavMeshBoundsVolume, press `P` to view green). This is the #1 "AI does nothing" cause.
2. **Cover slots (`AICoverSlot`)** are one-occupant, directional actors. Place ~2× more slots than expected combatants per fight space, split across **both facings** (a pillar gets two slots, opposite sides). `FireArcDegrees` (120 default) must face the expected threat or the slot is rejected for that fight.
3. Crouch-height slots work anywhere (fire over the top). **Stand-height slots are hide-only unless** `bIsPeekableCornerStart`/`End` is ticked — only mark corners a person could really step out of. Snipers exclusively use stand-height slots.
4. **Enemy cover search radius is 12m** from where the enemy is standing when combat starts. No slot within 12m of the engagement line = they fight in the open. Author deliberately.
5. **Patrol routes** (`APatrolRoute`): drag the 3D point widgets onto navmesh; `bLoop` on for circuits, off for ping-pong; default 2s wait per point. No route assigned = static guard post (that's valid).

### 0.3 Distance bands — paint these on the gym floor

Put rings/decals/TextRenders at these distances from a guard post in Zone B. You cannot eyeball-test perception without seeing the bands.

| Band | Distance | Meaning |
|---|---|---|
| Sight radius | 25m | outside = invisible (sniper: 40m) |
| Lose-sight | 30m | once seen, tracked to here |
| Hearing | 20m | max range any noise matters |
| Unsuppressed shot heard | 30m | wakes an area |
| Suppressed shot heard | ~4–7m | kills stay local (after 0.5 §11 fix) |
| Sprint footsteps | 12m | sprint is loud |
| Walk footsteps | 4m | quiet |
| Crouch footsteps | 1.5m | effectively silent |
| Auto-confirm | 3.5m | point-blank = instant Combat |
| Engage band (grunt) | 6–18m | where they want to fight |
| Takedown | 1.6m | rear 120° arc, Unaware only |

### 0.4 Verify the core wiring (10 minutes, once)

The Phase 3–7 wiring was read back and confirmed this morning. The Phase 1–2 handoff items were never re-checked. Open and confirm:

1. **DONE** — `BB_Enemy` (12 keys incl. enum MoraleState/ManeuverRole), `BT_EnemyBase` (Combat/Search/Suspicious/Patrol + `BT.EnemyCombat` injection tag), all 7 combat subtrees, all 7 `DA_Enemy_*`, `DA_DirectorConfig`, `DA_Barks_Grunt`, `AIC_Enemy` → BT_EnemyBase, every `BP_Enemy_*` → its DA + AIC_Enemy. Skip — verified.
2. **VERIFY** — `BP_Enemy_Grunt`: open it → Mesh component has a skeletal mesh + Anim Class set, and the skeleton has a `WeaponSocket` (right hand). Without the socket the rifle attaches to the capsule root and floats at his feet.
3. **VERIFY** — `WBP_BarkFeed` exists at exactly `/Game/Core/UI/WBP_BarkFeed` (the player controller loads that hard path). If missing: Right-click → User Interface → Widget Blueprint → parent class `BarkFeedWidget` → name it `WBP_BarkFeed` in `/Game/Core/UI/`. In the BP: add a VerticalBox (top-centre of screen), implement event `OnBarkReceived(SpeakerName, Line)` → add a TextBlock child "SpeakerName: Line", fade/remove after ~4s. No BindWidget names are required — visuals are entirely yours.
4. **VERIFY** — Takedown input: an `IA_Takedown` InputAction asset (Digital/bool) exists, is mapped in your InputMappingContext (suggest `F`), and is assigned to the `TakedownAction` property on the player BP(s) (both `BP_ExtractionCharacter`-type and the kit `BP_ExtractionPlayer`-type if you use both).
5. **VERIFY** — enemy mesh's Physics Asset exists (kill a grunt in PIE — he should ragdoll, not T-pose-freeze).

### 0.5 Fix-first list (30 minutes, before Phase 3 testing — these break tests, full detail in `enemy_gaps_and_setup.pdf`)

1. `DA_Barks_Grunt` → add map entries `Grenade Out` (lines: "Grenade out!", "Frag going in!") and `Suppressing` (lines: "Suppressing — move up!", "Covering fire!"), CooldownSeconds 6 each.
2. `DA_Enemy_Grunt` → Identity → `DisplayName` = "Grunt" (bark labels are blank without it).
3. Sniper weapon: duplicate `DA_AssaultRifle` → `DA_SniperRifle` (BaseDamage 90, MagazineSize 5, NoiseRange 3500); duplicate `BP_Rifle` → `BP_EnemySniperRifle` (WeaponData = DA_SniperRifle); `DA_Enemy_Sniper` → `WeaponClass` = BP_EnemySniperRifle.
4. Shield mesh: assign a riot-shield static mesh in `DA_Enemy_Shield` → `ShieldMesh`; in `BP_Enemy_Shield` add event `OnBoltOnComponentsReady` → `GetShieldComponent` → SetRelativeLocation (X=45, Z=40) + rotation so the face points forward. (The placeholder cube currently blocks shots from ALL directions.)
5. Sniper laser: create a red Niagara beam `NS_SniperLaser`; in `BP_Enemy_Sniper`: `GetSniperTelegraphComponent` → Bind Event to `OnLaserChanged` → on true spawn the beam (muzzle → target, update End on a 0.05s timer), on false destroy it.
6. Grenade ring: red decal/Niagara ring ~3.5m radius; in `BP_Enemy_Grenadier`: `GetGrenadierComponent` → Bind `OnGrenadeTelegraph(Landing, TimeToImpact)` → spawn ring at Landing with lifetime = TimeToImpact; bind `OnGrenadeCancelled` → destroy it.
7. Suppressed player weapon: duplicate your player rifle/pistol DA → `bSuppressed` true, `NoiseLoudness` 0.15, `NoiseRange` 400; BP child; add to your test loadout.

### 0.6 Debug toolkit (memorise these five)

1. **Gameplay Debugger:** `'` (apostrophe) in PIE with an enemy under the crosshair → toggle **Perception** and **Behavior Tree** categories. You can watch his blackboard live: `AwarenessState`, `MoraleState`, `ManeuverRole`, `HasLineOfSight`, `HasCover`, `CombatTarget`.
2. **Output Log filters:** console `Log LogEnemyAI Verbose` and `Log LogEnemySquad Verbose`. State transitions, global alert, morale changes, director Build/Peak/Relief, squad role claims all print there.
3. **`slomo 0.25`** — for reading peek/burst/telegraph timing. `slomo 1` to restore.
4. **`F8` (eject) + free-fly** — watch a squad spread/flank/bound from above. `god` while observing.
5. **`stat AI`** — alive count + budget while director testing; `P` for navmesh view.

### 0.7 Master asset checklist (everything across all phases)

| Asset | Status | Needed by phase |
|---|---|---|
| Skeletal mesh + ABP on BP_Enemy_Grunt + WeaponSocket | VERIFY | 1 |
| `WBP_BarkFeed` at /Game/Core/UI/ | VERIFY | 2 |
| `IA_Takedown` + IMC mapping + TakedownAction on player BPs | VERIFY | 2 |
| Physics asset (ragdoll) on enemy mesh | VERIFY | 2 (corpses), 4 (ragdoll) |
| Suppressed player weapon DA + BP | MISSING | 2 |
| Meshes/ABP/tint MIs on the 6 other BP_Enemy_* | MISSING | 3 |
| Riot shield static mesh + DA ref + BP offset | PLACEHOLDER (engine cube) | 3 |
| `NS_SniperLaser` + OnLaserChanged binding | MISSING | 3 |
| Grenade warning ring + OnGrenadeTelegraph binding | MISSING | 3 |
| `DA_SniperRifle` + `BP_EnemySniperRifle` | MISSING | 3 |
| Grenade body mesh / trail / explosion FX+SFX on BP_EnemyGrenade | basic | 3 (polish) |
| `GrenadeOut` + `Suppressing` bark lines | MISSING | 3, 7 |
| Grunt DisplayName | MISSING | 2 |
| Flinch/hit-react montages → `OnHitReact(Region)` | MISSING (P1 polish) | 4 |
| Suppression duck → `OnSuppressedStateChanged` | MISSING (P1 polish) | 4 |
| Takedown reaction montage → `OnTakedownExecuted` (0.8s window) | MISSING (P1 polish) | 4 |
| Melee swing → `OnMeleePerformed` | MISSING (P1 polish) | 4 |
| Plate-break FX → `OnPlateBroken` (heavy) | MISSING (P1 polish) | 4 |
| High-capacity player test weapon (suppression hose) | MISSING (test-only) | 4 |
| `L_EnemyGym` + `L_ExtractionSlice` maps | MISSING | all |
| `AEnemySpawnZone` ×4+ placed, one carrying DA_DirectorConfig | MISSING | 6 |
| Mission-phase triggers + debug keys in level BP | MISSING | 6 |
| `SquadId`s on placed groups | MISSING | 5 |

---

## Part 1 — Phase 1: Skeleton (Grunt)

**Goal:** *Grunt patrols, spots you, takes cover, fires, dies.*
**Code + asset wiring:** DONE (BB/BT/AIC/BP/DA all verified). Your work is the test space + the VERIFY items in 0.4.

### Build Gym Zone A — "Combat Yard" (~30×30m)

1. Flat floor, TextRender label "ZONE A — COMBAT YARD".
2. Scatter waist-high blocks at 4–8m spacing on **both** halves (enemy side + your side); add an `AICoverSlot` per block face that matters — 8–10 slots total, Height=Crouch, arcs facing the opposite half.
3. Two stand-height walls; on each, mark one corner: slot Height=Stand with `bIsPeekableCornerStart` or `End` ticked.
4. One `APatrolRoute` along an edge: 3+ points (drag widgets), `bLoop` true.
5. Place 2× `BP_Enemy_Grunt`. Grunt #1: Details → Enemy|Patrol → `PatrolRoute` = the route. Grunt #2: no route (static post behind cover). Leave `SquadId` empty this phase.
6. NavMeshBoundsVolume over everything; press `P` — all green between patrol, covers, and your approach lane.
7. A 25m+ open approach lane so you can stand outside sight radius and step in.

### Test script (PIE, companion enabled for #7–8 only)

| # | Do | Expect | If it fails |
|---|---|---|---|
| 1 | Watch from outside 25m | Grunt walks the route, waits ~2s per point, loops | Off-navmesh points; route not assigned |
| 2 | Step into the lane at ~15m, stand still | ~1.5s: stops & faces you ("Did you hear that?") → ~3s: walks at you → ~5s: "Contact!", moves to cover, crouch-peeks, bursts | Never fires: check DA/AIC on the BP. Idles in Combat: subtree injection (verified DONE — then check BB asset on BT) |
| 3 | Watch his first burst vs later bursts (slomo 0.25) | First burst visibly sloppy (7° spread), later bursts tight (1.5° after ~2s on-target) | — |
| 4 | Shoot him: 2 head vs 4+ torso (50dmg weapon) | Head kills in 2 (2.0×), limbs weakest (0.75×) | BoneToHitRegionMap vs your skeleton's bone names |
| 5 | Break LOS mid-fight, stay hidden 15s | 4s holding fire on your last spot → moves there, sweep-looks ~8s ("Lost him!") → gives up, returns to patrol | — |
| 6 | Let him fully reset, shoot him from behind | Instant Combat toward you (no meter) | — |
| 7 | Companion present, one grunt | Companion engages it automatically; never blocks your shots from the same slot | — |
| 8 | Stand far, companion near the grunt | Grunt prefers the companion (closer + damaging him) | — |
| 9 | Two grunts, one cover block | They never share a slot — second one takes another or fights open | Not enough slots placed (that's authoring) |
| 10 | Let both grunts fire across each other | Zero friendly damage ever | — |
| 11 | Kill one | Firing stops, capsule off, ragdolls, companion drops it as a target; body stays (corpse registry) | T-pose = physics asset missing |

**Slice work:** block out ACT A's yard only — fence, two entries, the patrol becomes a perimeter patrol.

**Sign-off:** all 11 pass → mark Phase 1 playtest ✅ in `enemy_code_plan.md`.

---

## Part 2 — Phase 2: Awareness ladder

**Goal:** *Sneak a dormant patrol, get made, watch it search and give up.*
**Needs from 0.4/0.5:** WBP_BarkFeed, IA_Takedown wiring, suppressed player weapon. Bark DA content is DONE (11 types verified) apart from the grunt DisplayName fix.

### Build Gym Zone B — "Stealth Lane" (50×12m) + Zone B2 — "Body Alley"

1. Zone B: guard post (static grunt) at one end; distance decals every 5m to 30m (use 0.3's table).
2. Cover stones at 10m intervals along one side (for break-LOS decay tests).
3. A dormant 3-guard cluster mid-lane, all facing away from a flanking path (takedown approaches).
4. Zone B2 (L-corridor): patroller #1 on a short ping-pong; patroller #2 whose route crosses #1's ~30s later; a third guard 25m+ away (out of sight of both routes).
5. **Verticality check:** one guard on a 4–5m platform above the lane (hearing is a 3D sphere — High-Rise insurance).

### Test script

| # | Do | Expect | If it fails |
|---|---|---|---|
| 1 | Crouch-walk the lane at 20m+ from the guard | Never spotted (fill ~2/s at that range crouched — takes ~50s of continuous exposure) | — |
| 2 | Same spot, stand and sprint laterally | Spotted in ~3s (sprint 1.6× + stand) | — |
| 3 | Peek 2s, hide behind a stone, wait 7s, peek again | Meter visibly reset — his reaction starts over (decay 15/s) | Watch suspicion via his bark cadence; no re-"Huh?" = decay broken |
| 4 | Sprint within 12m behind him | He turns ("Did you hear that?"), then walks to where the noise was — investigates the *point*, not you | Sound→instant Combat = wrong (cap rule) |
| 5 | Fire ONE unsuppressed shot 25m from the cluster | All three rouse and converge on the shot's location ("Search the area!") | Hearing config / noise range |
| 6 | Same with the suppressed weapon at 10m+ | Nobody reacts (suppressed range ~4m) | DA values on the suppressed weapon |
| 7 | Approach a dormant guard from behind, inside 1.6m, press Takedown | Instant silent kill, anim hook fires, ragdoll after 0.8s; nobody else reacts | Input unbound (0.4 #4); not Unaware; front arc |
| 8 | Try the takedown from his front | Refused (rear 120° arc only) | — |
| 9 | Zone B2: kill patroller #1, hide; wait for #2 | #2 *sees* the body: "Man down! We've got a body!", searches; global mood → Searching (log line) | Body must be in his sight cone; corpse persisted? |
| 10 | Let #2 calm down, then kill him too; wait for guard #3 | Second body found → **Loud** (log "Global alert: 1 -> 2"); ALL Unaware enemies map-wide wake and sweep their posts | — |
| 11 | After Loud: watch the dormant cluster | They never return to true Unaware patrolling calm (one-way ladder) | — |
| 12 | Sprint under the platform guard | He investigates downstairs (3D hearing + nav link down) | Nav connection between floors |
| 13 | Throughout: bark feed | Lines appear with "Grunt:" speaker, no spam (per-type cooldowns, 1.5s global dedup) | WBP_BarkFeed path; DisplayName fix |

**Slice work:** ACT A becomes playable stealth — learnable perimeter patrols, a dormant pair at each entry, one body-discovery setup visible from a route. Verify a full ghost-run of ACT A is possible (crouch + suppressed kills + takedowns), and that one mistake escalates believably. Companion follows: its noise legitimately alerts enemies — confirm it reads fair.

**Sign-off:** 13/13 → Phase 2 playtest ✅.

---

## Part 3 — Phase 3: The roster

**Goal:** *Each of the 7 types fights distinctly with its own legible counter.*
**Do 0.5 fix-first list COMPLETELY before this phase** — sniper, shield, and grenadier tests are invalid without items 1–6. Then assign meshes/ABP/tints to the 6 BP children (0.7).

### Build Gym Zone C — "Archetype Lanes" (7 short lanes off a central hub)

| Lane | Build | Teaches |
|---|---|---|
| Grunt | reuse Zone A | yardstick / regression |
| Rusher | 20m dead-straight, zero cover | kill him before he arrives |
| Heavy | 4m-wide choke + a side flank door | armour front, flank the slow turn |
| Sniper | 40m+ sightline, 3 elevated stand-slot perches, crouch-cover chain underneath | laser → duck → push between relocations |
| Officer | grunt trio in a front cover row, officer behind a second row | aura visible, reaching him is the puzzle |
| Grenadier | ONE strong cover stone facing him (an obvious camp spot) | camping is punished at 4s |
| Shield | 10m corridor + two side gaps | flank or grenade the wall |

### Test script (one lane at a time; `slomo 0.25` for timings)

| # | Archetype | Do | Expect |
|---|---|---|---|
| 1 | Grunt | re-run Part 1 #2–5 | unchanged (regression) |
| 2 | Rusher | let him spot you, stand your ground | sprints straight in (~6 m/s) spraying wildly; at arm's reach swaps to melee, 35/hit every 1.5s. He ignores your suppression and never panics |
| 3 | Rusher | burst him during the charge | 60 HP — dies to one committed burst; "don't let him arrive" reads |
| 4 | Heavy | duel him frontally in the choke | bullets visibly do almost nothing (×0.3 + plates); he answers with 2–3.5s hoses; when you duck he keeps firing at your cover |
| 5 | Heavy | circle through the flank door at close range | his body pivots slowly (90°/s) and his gun goes silent beyond 60° off-facing; rear shots melt him (×1.5, head ×2 ignores armour) |
| 6 | Heavy | grenade or sustained frontal fire | after ~3 plate breaks the front stops protecting |
| 7 | Sniper | cross his sightline | laser paints you a full 2.0s before any shot; the shot HURTS now (90) |
| 8 | Sniper | when lasered: break LOS / have companion fire near him | telegraph cancels instantly — no shot. One near round = he abandons the perch |
| 9 | Sniper | track him over 2 shots | relocates to a different stand-height perch after the 2nd, and immediately if you wing him |
| 10 | Officer | fight the squad without killing him | their shots are noticeably tighter (×0.75 inside 15m); he repositions to stay behind the line; "Focus that one!" every ~10s and they converge on you |
| 11 | Officer | kill him | spread visibly loosens; squad-wide "Falling back!" panic (Phase 4 deepens this) |
| 12 | Grenadier | fight him in the open | he's a grunt |
| 13 | Grenadier | camp the stone ~4s | "Grenade out!" + ring on your cover + ~4s total to move (1s windup + flight + 2.5s fuse); blast 3.5m hurts EVERYONE incl. his friends |
| 14 | Grenadier | count throws | max 3, ≥12s apart, never inside 5m or beyond 20m; kill him mid-windup = no grenade |
| 15 | Shield | shoot him frontally | rounds hit the shield, zero damage to him; he keeps walking (2.2 m/s) with sloppy sidearm bursts every 2.5s |
| 16 | Shield | step through a side gap and shoot his body | normal damage — the block is geometric. THE 360° TEST: if side shots are still blocked, the mesh offset (0.5 #4) isn't done |
| 17 | Shield | grenade him | shield takes double (one frag ≈ half the plate) AND he takes blast damage; at 400 cumulative the shield breaks and he drops to ordinary cover-fighting |
| 18 | All | kill each mid-signature (officer mid-call, grenadier mid-windup, sniper mid-laser, shield mid-advance) | clean cancels: aura gone, no grenade, laser off, corpse doesn't block bullets |

**Slice work:** populate ACT B role-cast — grunts in the lobby, heavy on the stair choke, sniper over the atrium, officer+squad in the objective room, grenadier covering the campable doorway. Each room should teach its counter.

**Sign-off:** 18/18 → Phase 3 playtest ✅.

---

## Part 4 — Phase 4: Morale & suppression

**Goal:** *Suppress an enemy and its head goes down; kill an officer and the squad turtles.*
**Assets:** flinch/hit-react montages (`OnHitReact`), suppression duck (`OnSuppressedStateChanged`), takedown reaction (`OnTakedownExecuted`) — wire what you have; the systems test fine without them, they just read better with. Make a high-capacity test weapon for yourself (duplicate rifle DA, MagazineSize 200) — suppression testing eats ammo.

### Build Gym Zone D — "Pressure Ring"

1. Two concentric cover rings: a forward ring, and a "deep" ring 10–12m further back (fallback needs deeper cover to exist *within ~24m* — double search radius).
2. Officer + 4 grunts in the forward ring (give them all one `SquadId` — early Phase 5 spillover is fine and makes officer-death cleaner).
3. Your strongpoint with ammo, facing the rings.

### Test script

| # | Do | Expect | If it fails |
|---|---|---|---|
| 1 | Hose rounds 0.5–1m OVER a covered grunt's head (don't hit him) | Within 2 near rounds: head down, stops peeking, any return fire is wild (+4° at full); "He's got us pinned!" | NearMissRadius is 1.5m — aim close |
| 2 | Stop firing, stopwatch | ~1–2s later he resumes the peek rhythm (decay 0.6/s) | — |
| 3 | Same hose on the heavy | Barely reacts — needs ~6 near rounds, and even suppressed he keeps firing (his task has no suppression interrupt — intended) | — |
| 4 | One near round past the sniper | Instantly suppressed (resistance 0.5) → telegraph cancels → relocates | — |
| 5 | Same on the rusher mid-charge | Charge never stops; only his spray gets wider (fearless by intent) | — |
| 6 | Kill grunts one by one, watch survivors | Each nearby death: "Man down!", morale dropping (log: `Morale Confident -> Shaken`) | — |
| 7 | Kill the OFFICER | Every non-fearless squadmate instantly Broken (−75 ≥ their whole bar): "Falling back!", they abandon the forward ring for the DEEP ring, crouch, peek only every 5–10s | Deep ring too far (>24m) or no free slots |
| 8 | Suppress a Broken enemy | Zero peeks at all while rounds fly — totally dark | — |
| 9 | Stop all pressure 30s+ | Morale recovers (+2/s after 5s grace) — Shaken enemies come back to confident peek timing. Broken usually stays broken without a rally (recovery from ≤30 takes minutes — intended turtle) | — |
| 10 | Officer alive variant: break two grunts, *don't* kill the officer | Within ~25s he rallies — broken grunts un-pin and rejoin ("morale floor" raise, 20s) | — |
| 11 | Deaths generally | Every kill ragdolls; takedowns ragdoll 0.8s late (anim window); corpses never bark/react and still trigger body-discovery | — |
| 12 | Suppress the COMPANION (fire near it) | It ducks/repositions more cautiously but keeps fighting — and still completes a revive if you go down under fire (stage it: go down in the open in Zone D) | — |

**Slice work:** ACT B objective room becomes the morale showcase — defenders fall back from the door ring to the deep room ring as you win. The arc must read through behaviour + barks alone (no meters anywhere).

**Sign-off:** 12/12 → Phase 4 playtest ✅.

---

## Part 5 — Phase 5: Squad baseline

**Goal:** *A squad spreads, shares sightings, and flanks — minus bounding overwatch.*
**Setup is one property:** `SquadId`. Placed enemies without it have **no squad behaviour at all** — if a test below "fails", check the id first.

### Build Gym Zone E — "Flank Field" (~40×40m)

1. Central player strongpoint with cover.
2. Enemy approach side: a cover field with slots spaced >3m apart (spacing must be *visible*).
3. **Two clear flank routes** (left/right hedge lines) with their own cover chains, curving toward your sides/back — the flank solver samples a ~9m ring around YOU, scored toward "behind your facing"; give those ring points navmesh and approach geometry.
4. Two 4-man squads, far apart: select each group → Details → Enemy|Squad → `SquadId` = "Alpha" / "Bravo". Make Alpha officer-led (officer + 3 grunts), Bravo leaderless.

### Test script

| # | Do | Expect | If it fails |
|---|---|---|---|
| 1 | Engage ONE Alpha member, eject (F8) and watch | Within ~1s the rest of Alpha converges on where you were seen (Searching, weapons up — they confirm with their own eyes before firing) | SquadIds |
| 2 | While fighting Alpha, watch Bravo | Bravo keeps patrolling, cold — squads never share across ids (only the global Loud wake touches them) | Squads too close (Bravo perceives you directly) |
| 3 | Hold a fight, watch the squad's shape | Spread firing line, nobody within ~3m of a squadmate, never two on one slot | Cover field too sparse |
| 4 | Wait ~10s into the fight | Exactly ONE member breaks off on a hedge route toward your blind side — "Flanking!" — while the rest hold | Flank ring off navmesh |
| 5 | Wound or suppress the flanker mid-run | He aborts to cover; no new flank attempt for ~8s | — |
| 6 | Suppressed-pistol one Bravo member from full stealth (rest Unaware) | NO squad convergence — a clean takedown/suppressed kill on a fully unaware squad stays silent until someone reaches Searching (body find / noise) | This is the stealth-preservation rule — if they converge, something leaked |
| 7 | Shoot Alpha's officer repeatedly (don't kill) | Squad focus does NOT thrash to you on every hit — his focus call re-targets at most every 10s | — |
| 8 | Kill the squad's current focus target's... i.e. you downing their focus: kill whoever they focus (a companion test) — simpler: damage Bravo (leaderless) first from one position, then have companion light them up | Bravo initially piles on YOU (first damager sets focus when empty); they re-pick when the picture changes — no squad-wide freeze when a target dies | — |
| 9 | Companion drawing fire | Light up an enemy with the companion: its squad's attention visibly shifts (threat = recent damage weighs heaviest) | — |

**Slice work:** each ACT B floor gets its own tagged squad. Fighting floor 1 must not pre-alert floor 2 beyond the global-alert wake — staggered waves, not a hive mind.

**Sign-off:** 9/9 → Phase 5 playtest ✅.

---

## Part 6 — Phase 6: The director

**Goal:** *Go loud, get adaptive pressure with relief beats; extraction repopulates.*

### Setup — zones, config, triggers

1. **Gym Zone F (measurement, not play):** a ring of 4 `AEnemySpawnZone`s around a glass/god observation box. Box extents on navmesh. Leave `ActivePhases` empty (= all).
2. On exactly ONE zone: `DirectorConfig` = `/Game/Enemy/AI/DA_DirectorConfig`. (Alternative: Level BP BeginPlay → `GetWorldSubsystem(EnemyDirectorSubsystem)` → `SetDirectorConfig`.)
3. **Debug keys in the level BP** (temporary, testing gold): key `8` → `SetMissionPhase` Infiltration, `9` → Objective, `0` → Extraction, `K` → `TripAlarm`. All via `GetWorldSubsystem(EnemyDirectorSubsystem)`.
4. **Slice:** place real zones out of sightlines — behind ACT A's fence corners, ACT B stairwell backs, one off-map ingress at the yard edge. Walk each: zone → navmesh → a path to player space. Trigger volumes: ACT A entry = Infiltration, objective room = Objective, objective-complete = Extraction.
5. **Know the math before testing:** spawns happen only when ALL of: alert is Loud, director state is Build (not Peak/Relief), cadence elapsed (45/25/15s by phase), alive count below cap (8/12/20 — **placed enemies count**), tension below ceiling (40/60/85), an eligible zone is 15–45m from you and off your sightline.

### Test script (Zone F: god, `stat AI`, Output Log on `LogEnemyAI`)

| # | Do | Expect | If it fails |
|---|---|---|---|
| 1 | Stay Calm 3+ minutes | Zero spawns, zero director log lines (it doesn't even tick while Calm) | — |
| 2 | Press K (TripAlarm) | Log "Director woke"; within one Infiltration cadence (45s) a grunt PAIR spawns at an off-sight zone and moves toward your position (they arrive Searching — seeded) | "no eligible spawn zone" log: distance band (15–45m) or sightline or navmesh |
| 3 | Fight the wave, watch the log | Tension climbs with engagement/kills; at 75: "Build -> Peak" and spawning STOPS mid-fight | — |
| 4 | Disengage, time it | Tension decays (4/s); under 40: "Peak -> Relief"; ~25s of guaranteed quiet; then "Relief -> Build" and waves resume | — |
| 5 | Press 9 (Objective) | Cadence 25s, cap 12; mixed squads appear over time: grunt trios, grunt+rusher, officer-led, grunt+grenadier | — |
| 6 | Count alive enemies (stat AI) with placed force present | Placed + spawned never exceeds the phase cap; kills open headroom → trickle resumes | Remember: a comp only spawns if it FITS — cap 8 with 7 alive spawns nothing (pair won't fit) |
| 7 | Press 0 (Extraction) | Immediate Build (no relief dead-air); 15s cadence, cap 20; heavy/officer pushes, triple rushers, shieldwall, sniper teams; spawning continues BEHIND you | — |
| 8 | Delete all zones in-PIE | One warning log, no spam, no crash | — |

**Slice (the real test):** full loop — ghost ACT A (Calm: confirm zero spawns the whole time) → go loud at the objective (Infiltration trickle) → trigger Objective (real fights + relief beats) → Extraction (crescendo, fight back out through repopulating space). Time the run; note where pacing sags — tune `DA_DirectorConfig` (cadence/ceiling/weights), not code. Companion check: relief windows are where revive/rearm happen — confirm they get room.

**Sign-off:** 8/8 + the full loop feels like build/peak/breathe → Phase 6 playtest ✅.

---

## Part 7 — Phase 7: Bounding overwatch

**Goal:** *Officer-led squad pins-and-bounds; degrades cleanly when sync breaks.*
**Prereq:** the `Suppressing` bark lines (0.5 #1) — the maneuver is silent without them. Squad needs: officer + **at least 2 grunt-archetype members** (others welcome but only grunts take the roles; use officer + 3 grunts so one death doesn't end eligibility), all with the same `SquadId`, in Combat with a shared target.

### Build Gym Zone G — "Bounding Street" (35m)

1. A long, straight suppression sightline down the middle (your strongpoint at the far end, with cover).
2. An advance lane along one side with a chain of cover slots every 5–6m (the flanker bounds ~7.5m hop to hop).
3. Officer + 3 grunts at the near end, one `SquadId`.

### Test script (watch ManeuverRole in the Gameplay Debugger)

| # | Do | Expect | If it fails |
|---|---|---|---|
| 1 | Engage and hold your strongpoint | Within ~20s of combat: one grunt opens SUSTAINED fire on your position (3–5s bursts — "Suppressing — move up!") while a second sprints the side lane in ~7.5m hops, moving ONLY while the fire is live; on arrival they SWAP ("Covering — go!") and leapfrog | Squad comp (officer? 2+ grunts? same id? all in Combat?) — also a 20s cooldown between attempts |
| 2 | Kill the suppressor mid-bound | The runner freezes/aborts immediately; maneuver ends; squad reverts to normal Phase-5 behaviour | — |
| 3 | Don't kill anyone; wait for the suppressor's reload (his 30-mag empties inside one long burst) | The runner PAUSES in place during the reload and resumes when fire resumes — no teardown | — |
| 4 | Kill the officer | Current maneuver stops; no bounding ever starts again this fight | — |
| 5 | Suppress or wound the RUNNER mid-bound | He aborts to cover; maneuver ends cleanly | — |
| 6 | Replace the grunts with rushers/shields/snipers (officer intact) | Never bounds — roles are grunt-only; specialists keep their signature moves. Leaderless squad: never bounds, ever | — |
| 7 | Companion counter (the thesis beat) | Have the companion suppress the suppressor: the pin breaks, the runner freezes — *your* covering fire defeats *their* covering fire | — |

**Slice work:** the extraction yard's final defence runs overwatch against your exfil position — the last fight of the demo loop.

**Sign-off:** 7/7 → Phase 7 playtest ✅ — and the roster is fully playtested.

---

## Part 8 — Session protocol & the full-loop test

For every phase session, in order:

1. **Gym zone first** — isolate, Gameplay Debugger on, run that phase's script above.
2. **Slice second** — play the act(s) the phase touches, companion always present.
3. **Regression sweep** — re-run the *previous* phase's slice act once (10 min).
4. **Tuning goes into the DAs, not into notes** — the DA is the record. Retune a number → re-check the gym zone that proves it.
5. **Anything broken → reproduce in the gym zone first** (smallest repro), then fix.

**The graduation test** (after Phase 7 signs off): one continuous L_ExtractionSlice run — ghost the perimeter, get made at the objective, survive the sawtooth, extract through the crescendo with the companion. If that run produces: a takedown, a body-discovery escalation, a suppression save by the companion, an officer-kill squad collapse, a grenade flush you dodged, and a bounding maneuver you broke by shooting the suppressor — every system in the stack just demonstrated itself in one sitting. That's the dissertation demo.

---

## Appendix — quick console card

```
'            gameplay debugger (Perception + Behavior Tree categories)
Log LogEnemyAI Verbose       state ladder, global alert, morale, director
Log LogEnemySquad Verbose    squad roles, focus, bounding claims/swaps
stat AI      alive count / budget        P    navmesh view
slomo 0.25   read telegraphs/bursts      F8   eject and observe
god          observe without dying
```

Key log lines worth recognising: `Global alert: 0 -> 1 -> 2` (Calm→Searching→Loud) · `Director woke` · `Director: Build -> Peak / Peak -> Relief / Relief -> Build` · `Director spawned squad (N/N members) at zone ...` · `Morale Confident -> Shaken -> Broken` · `[Alpha] Bounding started — Suppressor=..., Flanker=...` · `[Alpha] Bounding swapped` · `released cover slot ... on teardown`.
