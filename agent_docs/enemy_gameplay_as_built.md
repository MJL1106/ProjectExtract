# EXTRACTION — Enemy AI: As-Built Gameplay Doc

**Status:** As-built reference + gap analysis. Written 2026-06-10 on `AI-Companion-Prototype`, before first playtest.
**Sources:** Live editor read-back of every asset in `/Game/Enemy/AI` (7 archetype DAs, DA_DirectorConfig, DA_Barks_Grunt, BB_Enemy, all 8 behaviour trees, BP wiring) + full read of the enemy C++ (`Source/Extraction/{Public,Private}/Enemy/`, enemy BT nodes, suppression, weapon noise/near-miss paths).
**Companions:** `enemy_design.md` (intent — the *why*), `enemy_code_plan.md` (implementation record — the *how it landed*). This doc is the *what actually runs*: every number below is the live asset value or the named C++ default, not the design aspiration.
**Gap analysis** against the design doc is §11.

---

## 1. One brain, seven bodies

Every enemy is the same `AEnemyCharacter` possessed by the same `AEnemyAIController` running the same `BT_EnemyBase`. All seven personalities come from data:

- **`DA_Enemy_<Type>`** — 111 tunables: stats, perception, accuracy, suspicion, morale, suppression, and per-archetype bolt-on config. Verified wiring: each `BP_Enemy_*` points at its own DA, all use `AIC_Enemy`, `AIC_Enemy` runs `BT_EnemyBase`.
- **Combat subtree injection** — at possess, the DA's `CombatSubtree` is injected into the base tree's `RunBehaviorDynamic` node (tag `BT.EnemyCombat`). The base tree owns patrol/suspicion/search; the subtree owns how that archetype fights.
- **Bolt-on components** — created at possess only if the DA flags them: armour (Heavy), shield (Shield), grenadier kit (Grenadier), command aura (Officer), aim telegraph (Sniper). Every enemy also gets suppression + morale components unconditionally.
- **Teams** — enemies are team 1, player + companion team 0. Corpses drop to NoTeam so live enemies can *see* dead allies (body discovery).

One consequence worth knowing: **a placed enemy with no `SquadId` has no squad** — it still patrols, fights, and panics, but never shares sightings, never flanks, never focus-fires, never bounds. Squad behaviours are squad-gated in code, not radius-based.

---

## 2. Perception & the awareness ladder

### Senses (per-DA; identical across roster except Sniper)

| | Sight radius / lose | Cone | Hearing | Stimulus memory |
|---|---|---|---|---|
| All except Sniper | 2500 / 3000 | 110° | 2000 | sight 5s, hearing 3s |
| Sniper | **4000 / 4500** | 110° | 2000 | sight 5s, hearing 3s |

### What makes noise (actual values)

All seven enemies carry **BP_Rifle → DA_AssaultRifle**: 25 dmg/round, 10 rounds/s, 30-mag, auto-reload forced on.

| Event | Loudness | Range | Suspicion gained by a hearer |
|---|---|---|---|
| Unsuppressed shot | 1.0 | 3000 | +30 per shot |
| Reload | 0.3 | 600 | +9 |
| Sprint footstep (player, per 350cm) | 1.0 | 1200 | +30 |
| Walk footstep | 0.3 | 400 | +9 |
| Crouch/prone footstep | 0.12 | 150 | +3.6 |
| Grenade detonation | — | — | **silent to AI hearing** (see §11) |
| Silent takedown | — | — | no noise event at all |

Suspicion from noise = `loudness × NoiseSuspicionGain (30)`, hard-capped at 99 — **noise alone can never confirm Combat** (the design rule survived implementation).

### The suspicion meter

Per-target 0–100, ticked at 6.7Hz. Sight fill per second = `40 × distance × angle × speed × stance`:

- **Distance:** 1.0 point-blank → 0.15 floor at the edge of sight radius (linear).
- **Angle:** 1.0 centred → 0.35 at the cone edge.
- **Speed:** sprinting (≥500cm/s) ×1.6, near-still (≤25) ×0.5, else ×1.
- **Stance:** crouched ×0.5, prone ×0.35 (prone is inert on the kit player — interface returns false), standing ×1.
- **Decay:** −15/s the moment the stimulus stops.

Worked examples (grunt, walking player):
- Standing walk at 1250cm, centred: 20/s → Suspicious in 1.5s, Searching at ~3.3s, Combat at 5s.
- Sprint at the same spot: 32/s → Combat in ~3.1s.
- Crouched and still at 2000cm: 2/s → ~50s to confirm; break LOS for 7s and the meter is back to zero.
- **Inside 350cm (AutoCombatRange), a clearly-sighted hostile is instant Combat** regardless of meter.
- **Taking damage is instant Combat** toward the shooter, even if never seen or heard (suppressed kill-shots that miss = you are now hunted).

### The ladder

| State | Threshold | What the enemy does | Bark |
|---|---|---|---|
| **Unaware** | <30 | Walks `PatrolRoute` at 200 (loop or ping-pong, 2s waits) or idles at post. **Takedown-able.** | — |
| **Suspicious** | ≥30 | Stops, turns, faces the stimulus point (`BTTask_EnemyFaceSuspicion`). Decays back to Unaware. | "Did you hear that?" |
| **Searching** | ≥65 | Moves to the investigate point at combat speed, sweep-looks. 8s without re-acquire → Unaware. New stimuli refresh the point and the clock. | "Search the area!" |
| **Combat** | 100 / instant triggers | Subtree takes over. LOS lost → holds 4s (`LostContactGrace`) on last-known → Searching. | "Contact!" / "Lost him!" |

Entering Combat zeroes all suspicion tracks and (if visually confirmed) broadcasts the sighting to the squad. Leaving Combat releases cover and un-crouches.

**Body discovery:** a live enemy that *sees* a corpse (corpses go team-neutral so sight perceives them) → Searching at the body, "Man down! We've got a body!", once per body per enemy. The first discoverer reports to the director.

### Global alert (the director's seam)

One-way ladder, level-wide: **Calm → Searching → Loud.**

- Any enemy reaching Searching → global **Searching**.
- Any enemy reaching Combat → global **Loud**.
- 1st body discovered → Searching; 2nd body (or a body while already Searching) → **Loud**.
- `TripAlarm()` (BlueprintCallable) → Loud.

Going Loud: every Unaware enemy wakes and sweeps its own post ("Search the area!"), and the director's spawn machinery starts. Enemies spawned *after* Loud catch up at init (they start Searching, never obliviously Unaware).

---

## 3. Combat behaviour shared by the whole roster

### BT_EnemyBase (verified node-for-node)

```
Selector
├─ [AwarenessState == Combat, abort Both]
│    Sequence  + Service: BTService_EnemyCombat (0.25s ± 0.05)
│    └─ RunBehaviorDynamic [tag BT.EnemyCombat]  ← archetype subtree
├─ [AwarenessState == Searching, abort Both]  → BTTask_EnemySearchLastKnown
├─ [AwarenessState == Suspicious, abort Both] → Sequence → BTTask_EnemyFaceSuspicion
└─ BTTask_EnemyPatrol (default)
```

The service validates the target and writes `HasLineOfSight` (eye→target visibility trace) and `TargetInRange` (≤ DA `EngageRangeMax`) every quarter second. It does not pick targets — awareness owns that.

### Accuracy — the "not an aimbot" pipeline

Spread in degrees, evaluated per shot:

```
spread = lerp(SpreadStartDeg → SpreadSettledDeg over SpreadSettleTime since aim began)
       + SpreadWidenMovingTarget   (target faster than 300cm/s)
       + SuppressionSpreadPenalty × suppression01   (up to +4°)
       × CommandSpreadMultiplier   (×0.75 inside officer aura)
       + ExtraSpreadDegrees        (shield sidearm +6°)
```

| | Reaction delay | First-burst spread | Settled | Notes |
|---|---|---|---|---|
| Grunt | 0.5s | 7° | 1.5° | the yardstick |
| Rusher | 0.3s | 9° | 5° | permanently sloppy — fires on the move |
| Heavy | 0.5s | 8° | 3° | volume over precision |
| Sniper | 0.2s | 2° | **0.5°** | deadly accurate — fairness lives in the telegraph |
| Officer | 0.5s | 7° | 1.5° | grunt accuracy |
| Grenadier | 0.5s | 7° | 1.5° | grunt accuracy |
| Shield | 0.5s | 7°+6° | 1.5°+6° | sidearm always wide |

Reaction delay applies only on a **new** target acquisition; the settle clock restarts every time aim switches. These six numbers per archetype are the thesis difficulty knobs.

In addition the Heavy (only) has `MaxAimYawDeg 60`: if the target is more than 60° off his facing, the weapon refuses to aim at all while the body turns at its clamped 90°/s — circling a heavy buys real seconds of silence.

### The peek-fire loop (`BTTask_EnemyCombatFire` — every archetype's fallback)

`Acquire (reaction delay, first pass only) → Expose (0.2s; un-crouch, or side-step 60cm toward the cover's peekable corner) → Fire (burst, DA duration) → Recover (0.15s; re-crouch) → Pause (DA pause) → repeat`.

Suppression hooks into this loop directly: a suppressed enemy **will not enter Expose** (it crouches and waits out a pause instead, even in open ground), and being suppressed mid-Expose/Fire cuts the burst and ducks immediately. This is the "covering fire visibly works" beat.

### Cover

Enemies use the same `AICoverSlot` registry as the companion — one claimant per slot, both factions score slots against *their own* threat. Squad members refuse slots within 300cm of a squadmate (score ×0.2). Cover is released on combat exit, death, controller teardown, and task aborts.

### Threat-scored targeting

Re-evaluated every 0.15s in Combat:

```
score = 1.0 × (1 − dist/SightRadius)  +  0.75 × sighted  +  1.5 × damaged-me-recently (4s window)
```

- Squad **focus target wins outright** — if the officer (or, leaderless, the first damaged member) called a target this enemy can currently see.
- A challenger must beat the incumbent by **×1.25** (hysteresis) — no target ping-pong between player and companion.
- Shooting an enemy in the back force-registers you as a candidate even if it never saw you.

The companion is a fully valid target: light an enemy up and its weighted score drags aggro off the player.

### Damage in, damage out

Incoming order: **shield absorb → hit-region multiplier → armour reduction → health.**

- Hit regions from the mannequin bone map: **head ×2.0, torso ×1.0, limbs ×0.75** — universal, every archetype.
- Melee (Rusher): 35 dmg, 180cm, 1.5s cooldown, bypasses hit regions.
- Enemy rifle damage out: 25/round, 10 rounds/s — burst length is the per-archetype lethality lever.

### Death, corpses, takedowns

- Death: weapon stops, capsule off, bolt-ons swept (telegraphs cancelled, aura off, shield collision off), suppression/morale deactivated, **ragdoll** (montage-stopped, Ragdoll profile).
- Corpses persist as discoverable bodies in a capped registry (10, oldest destroyed). Controllers self-destroy on death — corpses cost nothing and can't perceive.
- **Silent takedown:** Unaware target only, within 160cm, inside a 120° rear arc → instant kill (1e6 dmg through any shield), zero noise, ragdoll delayed 0.8s for the BP anim window (`OnTakedownExecuted`).
- No enemy DBNO anywhere — they die outright, by design.

---

## 4. Suppression

Shared component (enemies + companion, never the player). Pure near-miss model:

- Every hitscan round passing within **150cm** of an AI pawn adds `0.25 ÷ SuppressionResistance`.
- ≥0.5 = suppressed; decays 0.6/s; clears below 0.3 (hysteresis).

| | Resistance | Near rounds to suppress | Time to recover |
|---|---|---|---|
| Sniper | 0.5 | **1** | ~0.3s+ |
| Grunt / Officer / Grenadier / Rusher / Shield | 1.0 | 2 | ~0.3–1.2s |
| Heavy | 3.0 | **6** | fast |

What suppression actually does, per archetype: blocks peeking and cuts bursts (everyone in the CombatFire loop), +4° spread at full, aborts flanks and bounding advances, forces the sniper to relocate, denies the sniper telegraph. The Heavy's `BTTask_HeavySuppress` has **no suppression interrupt at all** — he keeps hosing while rounds crack past (that, plus resistance 3, is "shrugs it off"). Note the Rusher *can* be suppressed (resistance 1) — but nothing in `BTTask_RusherAdvance` reads it, so suppression only widens his spray (+4°) without slowing the charge. Functionally fearless in motion.

Player suppression: doesn't exist, by design. The player suppresses *them*.

---

## 5. Morale

Per-enemy 0–100, **Confident → Shaken (≤60) → Broken (≤30)**, never rout. 1s tick.

**Losses ÷ MoraleEventResistance; gains unscaled:**

| Event | Base delta | Route |
|---|---|---|
| Squad ally died | −15 | exactly-once via squad (or 2500cm radius if squadless/cross-squad) |
| **Officer died** | **−75** (non-fearless DAs) | squad, or 5000cm radius |
| Sustained suppression | −10 per second suppressed | own component |
| Low health (≤30%, once) | −20 | own component |
| Flanked (target >~101° off facing) | −10 **per second** | own component |
| Hit the player/companion | +5 | shooter-side weapon hook |
| Downed the player/companion | +15 | shooter-side weapon hook |
| Officer rally | +30, floor +20 for 20s, guaranteed un-Broken | squad |
| Recovery | +2/s after 5s without losses | tick |

The officer-death math is the roster's keystone working as intended: a Confident grunt (100) takes 75 → **25 = instantly Broken**; sniper takes 150 → Broken; heavy takes 37.5 → still Confident at 62.5; rusher/shield are `bFearless` and ignore morale entirely (gains included). Kill the officer and the grunts/grenadier/sniper turtle on the spot while the heavy anchors and the rusher keeps coming — five distinct reactions from one event, all data.

**Broken = `BTTask_EnemyFallback`** (first branch of every non-fearless subtree, abort Both): release cover, pick the *deepest* slot (search radius ×2, scored heavily toward distance-from-threat), crouch, and peek-fire only every 5–10s for 0.6s — and never while suppressed. A Broken enemy under covering fire goes fully dark.

The flanked drain is sneaky-strong: park behind a grunt's facing and he loses 10/s — alone it breaks him in ~7s. It mostly bites enemies that can't turn fast (Heavy: −5/s after resistance, while his 90°/s turn drags him around).

Fearless roster: **Rusher** (charges through anything), **Shield** (no Broken state; "brittle once broken" is expressed through the BT instead — see §7).

---

## 6. Barks — the legibility layer

`UBarkSubsystem` → subtitle feed widget (`WBP_BarkFeed`), optional SFX per line. Dedup: 1.5s global per-type window, per-speaker cooldown (default 6s; Contact 4s, BodyFound 8s), squad-level 3s window for squad calls. Random line per bark.

**Authored today (all seven archetypes share `DA_Barks_Grunt`):** HeardSomething ×3, SearchArea ×3, Contact ×3, LostTarget ×3, BodyFound ×2, ManDown ×2, Pinned ×2, FallingBack ×2, Flanking ×2, FocusTarget ×2, CoveringGo ×2.

**Missing from the asset: `GrenadeOut` and `Suppressing`.** The code requests both (grenadier on telegraph, overwatch suppressor on opening fire); the subsystem silently drops a type with no lines. Until two lines are added, the grenade's *audio* telegraph and the suppressor's call do not exist — see §11.1.

Also: the bark speaker name is the DA `DisplayName`, and **DA_Enemy_Grunt's DisplayName is empty** — grunt barks (most barks in the game) render with a blank speaker label.

---

## 7. The roster

Stats shared unless stated: sight 2500/3000 @110°, hearing 2000, suspicion block identical across all seven (fill 40, decay 15, thresholds 30/65, auto-combat 350), takedown 160cm/120°, rifle 25dmg×10rps.

---

### 7.1 GRUNT — the yardstick

**100 HP · 200 patrol / 400 combat · engage 600–1800 · burst 0.4–1.2s, pause 0.8–2s · react 0.5s · 7°→1.5°**

The baseline everything else reads against, and the only archetype with the full squad toolkit:

```
Selector
├─ [Broken]                      Fallback (deep cover, turtle)
├─ [ManeuverRole == Suppressor]  SuppressFire   (3–5s area bursts, 0.3–0.8s gaps)
├─ [ManeuverRole == Flanker
    + SuppressionLive]           BoundingAdvance (750cm hops while covered)
├─ Flank                          (opportunistic, squad token)
├─ MoveToCover → CombatFire
└─ CombatFire                     (open-ground fallback)
```

In a leaderless squad he covers, peeks, bursts, and occasionally takes the flank token. Under an officer he becomes half of the pin-and-bound machine. Isolated and Broken, he turtles at the farthest cover he can find.

**How you beat him:** standard play — flank his slot, suppress him (2 near rounds) and push, or headshot the peek (×2.0 = 2 head rounds from a 50dmg weapon).

**Wiring gaps:** mesh/ABP manual; DisplayName empty (blank bark label).

---

### 7.2 RUSHER — punishes camping

**60 HP · 250 / 600 combat · engage 0–1200 · react 0.3s · 9°→5° · melee 35dmg/180cm/1.5s · FEARLESS**

Subtree: `RusherAdvance → CombatFire`. No fallback branch, no flank, no cover — ever.

`RusherAdvance`: repath to the target every 0.5s at 600cm/s, **continuous fire** the whole way in (with 5° settled spread and a +3° moving-target penalty, it's pressure not precision), weapon down at 180cm and melee every 1.5s until something dies. Morale-immune, suppression only widens his spray.

**How you beat him:** he's a 60HP straight line — burst him before he arrives (about 2.4s of sprint from max engage range), let the companion peel him, or simply don't be where he's going. Letting him arrive is the failure state: 35dmg per 1.5s, no hit-region mercy.

**Watch in playtest:** Combat speed 600 with no sprint animation budget yet (mesh/anims manual) — expect skating until anims land.

---

### 7.3 HEAVY — area denial

**250 HP · 150 / 250 combat · engage 600–2200 · burst 2.0–3.5s, pause 0.6–1.2s · 8°→3° · armour 140° front ×0.3, rear ×1.5, 3 plates · turn 90°/s, aim gate 60° · suppression ÷3, morale ÷2**

Subtree: `[Broken] Fallback → HeavySuppress → CombatFire`. **No cover branch** — the heavy anchors in the open and turns the firefight into a geometry problem.

`HeavySuppress`: stop moving, hose the target for 2–3.5s bursts; if LOS breaks he keeps firing **at your last-known position** (aim-location override) — your cover stays loud, and your companion nearby eats the near-misses. The 30-round mag bounds a burst at 3s; auto-reload punctuates his sustain (a real, exploitable rhythm).

Armour: frontal hits ×0.3 *after* hit-region scaling, **head bypasses armour entirely**; each ~83 raw frontal damage breaks a plate (`OnPlateBroken` for the visual — unbound yet); all 3 plates gone = frontal protection over. Rear/side ×1.5. Effective frontal-only kill ≈ 470+ rifle rounds of raw damage vs ~167 from behind — flanking isn't a suggestion.

**How you beat him:** circle him. 90°/s turn + 60° aim gate means a strafing player at close range can stay permanently in his deaf zone; the flanked morale drain (−5/s after resistance) and ×1.5 rear damage do the rest. Or grenade him (no armour vs radial from behind logic — radial resolves by attacker direction). He shrugs suppression (6 near rounds) and anchors when the squad breaks (morale ÷2).

**Watch in playtest:** he carries the same rifle as a grunt — his "LMG" identity is burst length only; if he doesn't read as a weapons platform, he needs a dedicated weapon DA (§11.3).

---

### 7.4 SNIPER — controls sightlines

**70 HP · sight 4000/4500 · engage 1200–4000 · react 0.2s · 2°→0.5° · telegraph 2.0s, shot cooldown 4s, relocate after 2 shots / when damaged (2s window) / when suppressed · perch search 3000 · suppression ÷0.5, morale ÷0.5 — the brittle one**

Subtree: `[Broken] Fallback → SniperNest → CombatFire`.

`SniperNest` loop: pick the **farthest** unclaimed stand-height cover slot within 3000 whose fire arc covers you → move → laser on (`OnLaserChanged(true)`) → 2.0s telegraph → single shot → laser off, 4s cooldown → after 2 shots (or any damage, or one suppressing near-miss) abandon the perch and repeat. Telegraph cancels instantly if you break LOS or crack a round past his head — **one near round spoils the shot**, the cleanest companion-suppression showcase in the roster.

**How you beat him:** the laser is the contract — duck when it paints you, suppress to cancel, or rush the 70HP body between relocations. Up close he's helpless (engage floor 1200 means once you're inside, his subtree fails into ordinary cover-fire with a rifle).

**Two blockers before he's testable (§11.1):** the laser delegate has **no Niagara bound** — today the telegraph is invisible, which violates the design's one hard rule for this archetype ("never a sniper without it"); and he fires the standard 25dmg rifle — a fully-telegraphed shot that takes a quarter of a health bar isn't a sniper, it's a nuisance. Needs his dedicated high-damage weapon DA.

---

### 7.5 OFFICER — the force multiplier

**100 HP · engage 600–2000 · grunt accuracy · aura 1500cm ×0.75 spread · focus call 10s cd · rally +30/floor+20×20s, 25s cd · bounding trigger 20s cd, min squad 3**

Subtree: `[Broken] Fallback → OfficerCommand → MoveToCover+Fire → Fire`.

`OfficerCommand` (the 3-second command beat): hold a navmesh point **600cm behind the living squad centroid, away from you** (repositioned every 3s — he hides behind the line by construction), fire only opportunistically when LOS happens to exist, and on each beat:
1. **Focus call** — sets the squad focus target (officer override; members who can see it drop everything else). "Focus that one!"
2. **Rally** — if any squadmate is Broken: +30 morale, +20 floor for 20s, guaranteed un-Broken. Un-pins turtled grunts.
3. **Bounding overwatch** — every 20s, try to start the pin-and-bound (§8.3). "Covering — go!"

Passive: the aura buffs **any living enemy within 1500cm** (not squad-filtered — a world-scan every 1s) to ×0.75 spread. Kill him: aura off same second, focus target cleared, any bounding maneuver collapses, and the −75 morale event breaks every non-fearless squadmate in radius outright (§5).

**How you beat him:** he's a 100HP grunt who deliberately stands behind everyone — *reaching* him is the puzzle. Flank wide, punch a hole with the companion's suppression, or out-range the line (his hold point hugs the squad, not cover). Killing him first is the single highest-value trigger pull in the game, exactly as designed.

---

### 7.6 GRENADIER — the flush

**100 HP · grunt stats · 3 grenades · 12s cooldown · trigger: LOS blocked ≥4s · range 500–2000 · 1.0s wind-up telegraph → arc flight → 2.5s fuse · 80 dmg, 350 radius, linear falloff, team-blind**

Subtree: `[Broken] Fallback → [no-LOS, abort Both] GrenadierLob → MoveToCover+Fire → Fire`. While he can see you he is exactly a grunt. The moment your cover blocks LOS, a 4-second timer starts.

The lob: arc-solve to your last-known position (`SuggestProjectileVelocity` arc 0.5), wind up 1.0s (delegate `OnGrenadeTelegraph(landing, ETA)` + bark request), spawn `BP_EnemyGrenade`, 12s cooldown, supply −1. The grenade ignores pawns in flight (bounces only on world geometry — it lands *at your cover*, not in your face) and detonates after 2.5s for 80 radial with falloff, **damaging anyone including his own squad**. Total flee window ≈ 1s wind-up + flight + 2.5s fuse — generous, *if you can perceive it*. Dies mid-wind-up → no grenade. Three throws and he's a grunt for the rest of the fight.

**How you beat him:** never camp one slot >4s against him, kill him at range (grunt body), or stand off >2000 / inside 500 where he can't lob at all. Nading his cluster works — the blast is team-blind.

**Blockers (§11.1):** the "Grenade out!" bark has **no lines authored** and the landing indicator has **no visual bound** — today the flush arrives with zero telegraph beyond the 1s body animation (which is also unbound). All three legibility channels for the roster's anti-camp mechanic are currently dark.

---

### 7.7 SHIELD — walks you down

**120 HP · 200 / 300 combat (220 while advancing) · shield 400 HP, blocks by physical interception · sidearm +6° spread, 0.6s burst every 2.5s · FEARLESS**

Subtree: `ShieldAdvance → MoveToCover+Fire → Fire`. No Broken branch (fearless) — "brittle once broken" is structural: `ShieldAdvance` returns Failed the instant the shield breaks, dropping him permanently into ordinary grunt-with-a-rifle behaviour with 120HP and 7° spread.

`ShieldAdvance`: walk at you at 220cm/s; every 2.5s stop, peek the sidearm (13°+ effective spread — suppressive, not lethal), 0.6s burst, resume. The shield is a real `UStaticMeshComponent` that **blocks shots by being physically in the way** (trace hits the shield component → 400 shield HP absorbs, zero through-damage). Grenades hit it for ×2 (200/blast) *and* full radial damage passes to the carrier — a single 80dmg grenade halves the shield and hurts him.

**How you beat him:** flank (the block is purely geometric — anything that hits the body, hurts the body), grenade (double-dips), pincer with the companion, or focus 400 damage through the plate and then delete the 120HP man holding it.

**The big blocker (§11.1):** the assigned mesh is the engine cube **attached at the mesh origin, unscaled/unpositioned** — until it's replaced and offset it physically wraps the carrier and blocks from *every* direction, deleting the archetype's entire counter. `ShieldBlockArcDeg 150` is informational only (geometry decides), so the fix is purely the mesh transform.

---

## 8. Squads

A squad = same `SquadId` (designer-set per placed instance) or a director spawn-group (`DirSquad_N`, auto-assigned). Squads never coordinate with each other. The coordinator (`UEnemySquad`) relays and arbitrates; it never puppets — **survival pre-empts sit inside every member's own BT**, above squad orders.

### 8.1 Baseline (always on, no officer)

- **Shared sightings:** any member with confirmed visual broadcasts target + last-known (squad relays at most 1/s). Members below Combat go **Searching** at the spot — never free Combat; they confirm with their own eyes. Members already fighting the same target get last-known refreshes. A fully-Unaware squad relays nothing — clean takedowns stay clean (morale relay is also gated on any member ≥ Searching).
- **Spacing:** cover slots within 300cm of a squadmate score ×0.2 — they spread without being told to.
- **One flanker at a time:** squad token + per-member 8s cooldown. The flank task ring-samples 12 navmesh points at ~900cm around *you*, scored `2×behind-your-facing + lateral-displacement-from-current-bearing`, then moves there firing on the way ("Flanking!"). Aborts (release token, back to cover): suppressed, below 35% HP, target lost.
- **Leaderless focus-fire:** first member damaged sets the squad focus if none exists; focus clears when the holder dies.

### 8.2 Officer tier

Focus-fire override on the 10s command beat, rally, aura, and the bounding unlock — all in §7.5. Officer death degrades the squad to baseline in the same tick (roles dropped, focus cleared, morale shock applied).

### 8.3 Bounding overwatch (officer-gated, grunt-only roles)

Start conditions (checked on the officer's 20s beat): officer alive with aura flag, squad has a live shared target, **≥2 grunt-archetype members in Combat with valid targets** (specialists never get drafted — rushers rush, snipers snipe). Suppressor pick prefers LOS + proximity; flanker pick prefers health.

- **Suppressor** (`ManeuverRole=Suppressor`): 3–5s area bursts at the target/last-known, 0.3–0.8s gaps, no LOS needed.
- **Flanker** (`ManeuverRole=Flanker`): advances in 750cm navmesh hops toward you, **only while the decorator `SuppressionLive` holds** — suppressor actively firing, alive, not reloading.
- Suppressor reloads → flanker **pauses in place** (maneuver holds, resumes with the mag); suppressor dies → advance stops dead.
- Flanker arrives → roles **swap** ("Covering — go!"), rate-limited to one swap per 2s — the pair leapfrogs toward you.
- Teardown: officer dies, either role dies, flanker suppressed or under 35% HP, target stale. Squad reverts cleanly to baseline (cut-safe verified in P7).

**How you fight it:** shoot the suppressor (the loud one) — the advance stops instantly; or suppress the flanker mid-hop; or kill the officer and end the behaviour for the rest of the fight.

---

## 9. The director

Wakes when the world goes **Loud**. 1s cadence, zero work while Calm.

### Tension (0–100)

```
+ 0.8 × player health lost      + 8 × enemy killed
+ 3 × engaged enemy per second  (Combat-state enemies within 3000 of the player)
− 4 / second decay
```

### The sawtooth

**Build** → (tension ≥75) → **Peak** → (tension <40) → **Relief, 25s** → Build. **Spawning happens only during Build** and only while tension is below the phase's intensity ceiling — pressure arrives when you're coasting, never stacks onto a peak fight, and after a big engagement you get a guaranteed ~25s+ breather. Mission-phase escalation forces Build (no dead air at extraction start).

### Spawning (per mission phase — live DA_DirectorConfig values)

| Phase | Cadence | Max alive | Ceiling | Compositions (weight) |
|---|---|---|---|---|
| **Infiltration** | 45s | **8** | 40 | GruntPair: 2 grunts (1.0) |
| **Objective** | 25s | 12 | 60 | 3 grunts (1.0) · 2 grunts+rusher (0.8) · 2 grunts+officer (0.6) · 2 grunts+grenadier (0.5) |
| **Extraction** | 15s | 20 | 85 | heavy+2 grunts+officer (1.0) · 3 rushers (0.8) · shield+2 grunts (0.7) · sniper+2 grunts (0.5) · 2 grunts+officer (0.6) |

- Compositions that don't fit the alive-cap headroom are filtered out before the weighted roll — squads are never trimmed. **The placed force counts toward the cap**: a stealth level with 8+ placed enemies gets *zero* Infiltration reinforcements until you thin it (per-level tuning trap, recorded in P6).
- Zone pick: registered `AEnemySpawnZone` actors active for the phase, 1500–4500cm from the player, zone origin *and* 3 sampled spawn points off the player's sightline (view-cone + visibility trace), nav-projectable; random among the first 5 candidates. Spawn points spread deterministically inside the box per squad member.
- Every spawned group registers as a squad and is **seeded with the player's position** — waves arrive Searching toward the fight, not wandering.
- Mission phase is set by level scripting (`SetMissionPhase` BlueprintCallable). Extraction "repopulation" = continued high-intensity spawning anywhere off-sightline including behind you — there is no cleared-zone memory (recorded deviation).

---

## 10. An encounter, beat by beat

**Calm.** The placed force walks routes at 200cm/s. You crouch-move (150cm hearing radius, 0.5× sight fill); a guard clips your silhouette at 1500 — +~10 suspicion, "Did you hear that?", he faces you. You freeze behind cover; 15/s decay, he shrugs in two seconds and walks on. You take down the trailing guard from his 120° rear arc — silent. His patrol partner *sees* the body a minute later: "Man down!", Searching, global alert → Searching. A second body found anywhere now tips the world Loud.

**Loud.** Confirmed contact: "Contact!", his squad learns your position within a second, everyone below Combat converges Searching. The director wakes at Infiltration settings — a grunt pair every 45s from off-sightline zones while the alive count allows. The squad you engaged spreads across cover (300cm spacing), one grunt takes the flank token and swings for your blind side ("Flanking!"); your companion's burst cracks past him — 2 near rounds, suppressed, flank aborted, back to cover with his head down.

**Objective phase** (level trigger): every 25s under cap 12 — rusher-tipped pairs to crack your camp, an officer squad that tightens everyone's spread and starts calling focus, a grenadier who counts to four the moment your cover blocks his eyes. You learn the rhythm: kill officers first (the squad audibly breaks — "Falling back!"), never sit through the fourth second.

**Peak/Relief.** Tension spikes through the fight (engaged enemies + your lost health + their deaths); at 75 the spawner stops; you clear the wave, tension bleeds at 4/s; under 40 you get 25 guaranteed quiet seconds to loot, reposition, revive.

**Extraction.** Crescendo: 15s cadence, cap 20, ceiling 85. Heavy-push squads with officers, triple-rusher waves, shieldwalls, sniper overwatch teams — spawning continues behind you on the way out. The officer squads now bound: one grunt pins you with 3–5s bursts while another leapfrogs 750cm at a time, swapping on arrival, collapsing the moment you delete the suppressor.

---

## 11. Gap analysis — design vs implementation

Severity: **BLOCKER** (breaks a design pillar / QA scenario until fixed) · **DIFFERS** (works, different shape than designed) · **MISSING** (designed, not present) · **EXTRA** (beyond design) · **CUT** (agreed §13 deferral).

### 11.1 Blockers before the first real playtest

| # | Gap | Evidence | Why it matters |
|---|---|---|---|
| 1 | **Sniper laser has no visual** — `OnLaserChanged` unbound (Niagara manual) | design §5 "Never a sniper without it"; P3 manual remainder | The one-shot archetype's entire fairness contract is invisible. Highest-priority wire. |
| 2 | **`GrenadeOut` and `Suppressing` bark lines don't exist** in DA_Barks_Grunt (11 of 13 types authored) | live dump; code requests both (`UEnemyGrenadierComponent::TryThrowAt`, `BTTask_EnemySuppressFire`) | Grenade flush and overwatch pin both fire silently. Two lines of text each — trivial fix, large legibility win. *New find — not recorded in the code plan.* |
| 3 | **Grenade landing indicator unbound** — `OnGrenadeTelegraph(landing, ETA)` delegate exposed, no BP visual | design §8 "arc indicator + bark + fuse window"; P3 manual remainder | Combined with #2, the anti-camp mechanic currently has zero telegraph channels. |
| 4 | **Shield is an engine cube at component origin** — wraps the carrier, blocks 360° | P3 notes; DA `ShieldMesh=/Engine/BasicShapes/Cube` | "Forces flanking" archetype currently *prevents* flanking. Mesh + relative offset fix. |
| 5 | **Sniper fires the standard 25dmg rifle** (all 7 share BP_Rifle/DA_AssaultRifle) | live dump: `WeaponClass=BP_Rifle_C` ×7 | A 2s-telegraphed quarter-bar poke isn't a sightline threat. Needs the dedicated sniper weapon DA (P3 listed it as "ideally"; in practice it's load-bearing for the archetype's role). |
| 6 | **Meshes/ABPs absent on the 6 new BP children**; flinch/hit-react/takedown montages + suppression flinch unbound; ragdoll physics asset unverified | P3/P4 manual remainders | No hit feedback = design §8's "the player must see shots land" fails; rusher at 600cm/s without anims will read as a glitch. |
| 7 | **No suppressed weapon DA variant** (player-side) | P2 manual remainder; `bSuppressed` field exists, unused | The stealth counter-loop (suppressed kills at distance) and P5 QA #2 are untestable. |
| 8 | **Spawn zones, DirectorConfig hookup, mission-phase triggers, SquadId on placed groups — all pending level work** | P5/P6 manual remainders | Without zones the director never spawns; without SquadIds placed enemies have *no squad behaviours at all* (no relay/flank/focus/bounding — easy to misread as "AI broken" in playtest). |

### 11.2 Designed, not implemented

| Gap | Status |
|---|---|
| Lighting factor in sight fill (design §4 "dark slower") | MISSING — recorded as deferred in P2 notes. Fill = distance×angle×speed×stance only. |
| Grenade blasts produce **no suppression, no AI-hearing noise, no dedicated morale event** (design §7 "his nades dent enemy morale too") | MISSING — suppression/morale deferral recorded P4; the hearing silence is a *new find*. Partial mitigation: blasts are team-blind, so real damage (and its low-HP morale hit) does land on his own cluster. |
| Morale events "outnumbered" (loss) and "reinforcements arrive" (gain) from design §7's event list | MISSING — *new find, unrecorded*. Implemented set: ally/officer death, suppression, low-HP, flanked, hit/downed target, rally. |
| Officer aura raising allies' **morale floor** passively (design §7 officer row) | MISSING (partial) — *new find*. The aura is accuracy-only; the floor raise exists only as the temporary rally (+20 for 20s). `MoraleFloor` sits at 0 in every DA. |
| "Stays edgy for a while" after a failed search (design §4) | MISSING (minor) — search timeout returns to clean Unaware; the only residue is combat-speed search movement. |
| Side-aware "Flanking left!" | MISSING (cosmetic) — generic "Flanking!", recorded P5. |
| Heavy plates visibly breaking off (design §5) | Delegate (`OnPlateBroken`) exists, visual unbound — part of 11.1 #6. |

### 11.3 Implemented differently than designed

| Deviation | Assessment |
|---|---|
| Shield "brittle once broken" morale arc → static `bFearless`, brittleness expressed structurally (ShieldAdvance fails on break → permanent grunt behaviour) | Recorded P4. Reads fine on paper; revisit only if the post-break shield carrier feels too composed. |
| Flank slot selection: EQS asset → C++ ring solver (12 samples, behind-facing ×2 + displacement scoring, nav-projected) | Recorded P5. Same semantics, no editor dependency — arguably better. |
| Bounding roles restricted to **Grunt archetype** members | Recorded P7. Design didn't restrict; keeps specialists on signature behaviours. Side-effect: an officer leading rushers/shields/snipers never bounds — diegetically fine. |
| Officer alive-but-Broken keeps a running maneuver alive (only death cancels) | Recorded P7 as accepted. Rare state (officer needs −75-class events ÷1.0 resistance). |
| Extraction "repopulates cleared areas" → continuous crescendo spawning, no cleared-zone tracking; behind-player zones eligible | Recorded P6. Functionally close for the High-Rise fight-back-down; revisit if "cleared floor stays clear" ever becomes a promise to the player. |
| Threat-score "exposure" term dropped (no cheap signal) | Recorded P5. Proximity+LOS+recent-damage with ×1.25 hysteresis covers the design intent. |
| Officer aura buffs **any enemy within 1500cm**, not just his squad (1s world-scan) | *New find.* Multi-squad fights: one officer tightens everyone nearby, including squadless strays. Cheap to squad-filter if it muddies the "kill the officer" read. |
| Rusher suppression: component accumulates (resistance 1) but `RusherAdvance` never checks it — suppression only adds spread, never stops the charge | Matches "barely suppresses" intent through omission rather than tuning; flagging because the *companion's* suppression-the-rusher thesis beat won't visibly land. |
| Prone stance factor inert on the kit player (interface returns false) | Recorded P2. Becomes real when prone is exposed on `AExtractionPlayer`. |
| `AEnemyBase` placeholder not deleted (permission-blocked); self-referenced only | Recorded P3. Manual delete of `Public/Enemy/EnemyBase.h` + `Private/Enemy/EnemyBase.cpp`. |

### 11.4 Implemented beyond design (hardening worth keeping)

`MaxAimYawDeg` facing-gated fire (heavy can't shoot where he isn't looking) · aim-location override (suppressing last-known cover without a target actor) · target-switch hysteresis ×1.25 · post-Loud spawn catch-up (late spawns wake Searching) + director seeding squads with the player's position · squad-persisted flank/bounding cooldowns (survive BT restarts) · identity-gated suppressor flags (swap-safe) · rally floor-before-boost with guaranteed un-Broken · corpse registry cap/recycle + controllers self-destroying on death · takedown ragdoll delay for the anim window · async shield-mesh load with broken-mid-load handling · damage-instigator force-tracking (back-shots always become target candidates, without granting wallhack LOS).

### 11.5 Agreed cuts (design §13 — listed so nobody re-finds them)

Surrender/rout · player-side suppression · co-op · smoke/flash · throwback · full VO · body-dragging. All confirmed absent, all intentional.

### 11.6 Value-level audit (live DA values vs intent)

- Every per-archetype differentiator the design implies **is actually set** in the assets: rusher `bFearless+bCanMelee`, heavy armour/turn/aim-gate/resistances (3.0/2.0), sniper sight 4000 + brittleness (0.5/0.5), officer `bHasCommandAura`, grenadier kit + projectile class, shield kit + fearless. No bolt-on flag was left at default. ✔
- `MoraleLossOfficerDied=75` on all five non-fearless DAs → officer death instantly **Breaks** Confident grunts/grenadiers (100−75=25 ≤ 30) and snipers (÷0.5 = −150), heavy shrugs (÷2 → 62.5). Reads as designed ("squad turtles"); just know the turtle is *instant*, not a drift.
- `DA_Enemy_Grunt.DisplayName` is **empty** (all six others set) → blank bark speaker labels. One-field fix.
- All seven `BarkSet` → `DA_Barks_Grunt` (per-archetype sets deferred, P3) — fine interim, but it means officer rally calls and rusher contact barks share one voice.
- `ShieldBlockArcDeg=150` is informational only — geometry decides blocking (P3 recorded).
- `MoraleFloor=0` everywhere — no archetype has a passive floor (see 11.2 officer-floor note).
- `MaxShield=0` everywhere — no enemy regenerating shields, consistent with design.

---

## Appendix A — archetype value matrix (live values, 2026-06-10)

| | Grunt | Rusher | Heavy | Sniper | Officer | Grenadier | Shield |
|---|---|---|---|---|---|---|---|
| MaxHealth | 100 | 60 | 250 | 70 | 100 | 100 | 120 |
| Patrol / Combat speed | 200/400 | 250/600 | 150/250 | 200/400 | 200/400 | 200/400 | 200/300 |
| Sight / lose | 2500/3000 | 2500/3000 | 2500/3000 | **4000/4500** | 2500/3000 | 2500/3000 | 2500/3000 |
| Engage min–max | 600–1800 | 0–1200 | 600–2200 | 1200–4000 | 600–2000 | 600–1800 | 600–1800 |
| Burst / pause (s) | .4–1.2 / .8–2 | .4–1.2 / .8–2 | **2–3.5 / .6–1.2** | .4–1.2 / .8–2 | .4–1.2 / .8–2 | .4–1.2 / .8–2 | .4–1.2 / .8–2 |
| Reaction delay | 0.5 | 0.3 | 0.5 | **0.2** | 0.5 | 0.5 | 0.5 |
| Spread start→settled | 7→1.5 | 9→5 | 8→3 | **2→0.5** | 7→1.5 | 7→1.5 | 7→1.5 |
| Cover search | 1200 | 1200 | 1200 | **2500** | 1200 | 1200 | 1200 |
| MaxAimYaw / TurnRate | — | — | **60° / 90°/s** | — | — | — | — |
| Suppression resistance | 1 | 1 | **3** | **0.5** | 1 | 1 | 1 |
| Morale resistance / fearless | 1 / no | 1 / **YES** | **2** / no | **0.5** / no | 1 / no | 1 / no | 1 / **YES** |
| MoraleLossOfficerDied | 75 | 30* | 75 | 75 | 75 | 75 | 30* |
| Bolt-ons | — | melee | armour | sniper | aura | grenadier | shield |
| Weapon | BP_Rifle | BP_Rifle | BP_Rifle | BP_Rifle | BP_Rifle | BP_Rifle | BP_Rifle |

\* irrelevant — fearless ignores morale. Shared suspicion block (all seven): fill 40/s · decay 15/s · Suspicious 30 · Searching 65 · auto-combat 350cm · noise gain 30 · edge 0.35 · still 0.5 · sprint 1.6 @500 · crouch 0.5 · prone 0.35. Shared misc: search 8s · lost-contact 4s · takedown 160cm/120° · spread penalty @full suppression +4° · threat weights 1/0.75/1.5 hysteresis 1.25 · spacing 300 · flank cd 8s abort ≤35% HP · rally 30/+20×20s cd 25s · focus cd 10s · bounding cd 20s min squad 3.

Bolt-on numbers — Armour: 140° arc, front ×0.3, rear ×1.5, 3 plates (~83 raw dmg each), head bypasses. Shield: 400 HP, radial ×2, sidearm +6°, advance 220. Grenadier: 3 × (80 dmg, r350, fuse 2.5s), cd 12s, telegraph 1.0s, range 500–2000, trigger 4s LOS-blocked. Aura: r1500, spread ×0.75. Sniper: telegraph 2.0s, cd 4s, relocate @2 shots / damaged(2s) / suppressed, perch r3000. Melee: 35 dmg, 180cm, 1.5s.

## Appendix B — node-level tunables (live in the BT assets, not DAs)

- `BTService_EnemyCombat`: 0.25s ± 0.05.
- `BTTask_EnemyCombatFire`: peek lateral offset 60cm; expose 0.2s / recover 0.15s (C++ constants).
- `BTTask_EnemyFallback`: search radius ×2, distance-from-threat weight 2.0, peek every 5–10s, burst 0.6s, recover 0.2s, cover retry 5s.
- `BTTask_EnemyFlank`: 12 ring samples, outer pad +600, arrival 120.
- `BTTask_EnemySuppressFire`: burst 3–5s, pause 0.3–0.8s.
- `BTTask_EnemyBoundingAdvance`: hop 750, arrival 120 (+200 slack), survival HP 0.35.
- `BTTask_OfficerCommand`: ally scan 2000, hold offset 600, beat 3s.
- `BTTask_ShieldAdvance`: burst cycle 2.5s, burst 0.6s.
- Suppression component (C++ defaults): +0.25/near-miss, decay 0.6/s, on ≥0.5, off <0.3, near-miss radius 150 (weapon-side).
- Morale component (C++ constants): tick 1s, recovery grace 5s, ally-death radius 2500 (officer ×2), low-HP 30%, flanked dot −0.2.
- Director (C++ fallbacks mirror the DA): engage-count radius 3000, tick 1s, ≤5 zone candidates, corpse cap 10.

## Appendix C — Blackboard (BB_Enemy, 12 keys, verified)

`SelfActor` · `CombatTarget` (Actor) · `LastKnownLocation` · `InvestigateLocation` (Vectors) · `AwarenessState` (EEnemyAwarenessState) · `HasLineOfSight` · `TargetInRange` · `HasCover` (Bools) · `CoverSlot` · `PatrolRoute` (Actors) · `MoraleState` (EMoraleState) · `ManeuverRole` (EEnemyManeuverRole). Decorator wiring verified: Combat=3 / Searching=2 / Suspicious=1, Broken=2, Suppressor=1 / Flanker=2, all abort Both; grenadier branch gated on inverted `HasLineOfSight`.
