# EXTRACTION — Enemy AI Design

**Status:** Design vision (high-level gameplay doc). Drafted 2026-06-09 on `AI-Companion-Prototype`.
**Scope:** The complete, ideal enemy specification — single-player. Implementation is sequenced separately.
**Supersedes:** The current `AEnemyBase` placeholder (static-mesh target dummy: ping-pong patrol, Tick-firing, no controller/BT/perception). This document describes its full replacement.
**Companion to:** `agent_docs/companion_testing.md`.

> Numbers in this doc (ranges, rates, healths) are *intent*, not final values. Tuning lives in data assets and is set during implementation/balance, not here.

---

## 1. Purpose & Scope

The enemy exists to create the pressure that **shows the AI companion off**. The companion is the thesis; the enemy is the stage it performs on. Every enemy mechanic is judged by one test: *does it create a moment where the companion's competence becomes visible and valued* — cover-seeking under fire, suppression relief, target prioritisation, and revive-under-pressure.

This is the **full ideal spec** — the complete vision, not a trimmed milestone. It is designed for **single-player** (player + AI companion vs enemies). Co-op multiplayer has been cut from the design target; the architecture stays authority-clean so it *could* return, but nothing here assumes more than one human player. Cutting co-op sharpens the thesis: "the companion makes solo play feel cooperative" becomes the whole point rather than a side-benefit.

---

## 2. Design Pillars

Every decision in this doc obeys these:

1. **Legibility over lethality.** Anything that can kill or out-manoeuvre the player is *telegraphed* — the sniper's aim-laser, the grenade arc, the "Flanking left!" bark. An enemy the player can't read how to beat is a damage sponge, not a threat.
2. **The enemy serves the companion.** The firefight is a stage. If a mechanic doesn't create challenge the companion can visibly answer, it's cut.
3. **Variety is data, not code.** One enemy class; behaviour driven by per-archetype data assets. A designer can tune or add a type without new C++.
4. **Reactive, not scripted.** Gradual awareness, morale, and a dynamic director make encounters feel alive and replayable rather than trigger-tripped.
5. **Fight to the death, but smart about it.** Enemies never rout. Losing makes them cautious and defensive, not gone — pressure stays relentless.

---

## 3. Architecture (overview)

Kept light — this is a gameplay doc. The shape that makes the rest possible:

- **`AEnemyCharacter`** — one class for all archetypes (skeletal mesh, replaces the placeholder).
- **`AEnemyAIController`** — perception, blackboard, behaviour tree. Faction = **Team 1** (player/companion = Team 0) so perception sorts friend/foe automatically.
- **`UEnemyArchetypeData`** — one data asset per archetype. Holds stats (health, speed, accuracy, engagement range), behaviour knobs, and a reference to the archetype's combat sub-tree. *All* behavioural difference between archetypes lives here.
- **Conditional components** — the officer's command/aura and the shield's frontal hitbox bolt on only for those archetypes.
- **Base behaviour tree** — a `Combat → Alert/Search → Patrol` selector shared by all archetypes; the archetype-specific combat sub-tree is injected at runtime.

**Reuse surface (already shipped, no new work):**

| System | Reused for |
|---|---|
| `UHealthComponent` (health + shield + death, replicated) | Enemy damage and death |
| `AWeaponBase` / `UWeaponComponent` | Enemy firing, ammo, reload, accuracy |
| `AICoverSlot` + `CoverRegistrySubsystem` (slot claim/release) | Enemy cover *and* the squad-coordination primitive (§6) |
| `EHitRegion` (head/torso/arms/legs) | Headshot and weakpoint multipliers |
| AI Perception pattern (sight + hearing) | Enemy senses (§4) |

All AI decisions run on the authority. Clients (if MP ever returns) only see replicated results.

---

## 4. Perception & the Awareness Ladder

Detection is a **gradual awareness model**, not binary spotting. This is what makes the infiltration phase a real phase rather than a walk to the first trigger.

### Senses

- **Sight** — a view cone (~110°) requiring line of sight. Fill speed scales with distance, angle off-centre, player stance (prone/crouch slower), movement (sprinting fast, still-in-cover very slow), and lighting (dark slower).
- **Hearing** — discrete sound events that bump suspicion and drop an **investigate point** (a location to check), never instant detection.

| Sound | Loudness | Effect |
|---|---|---|
| Unsuppressed shot | Very high | Wide alert, investigate point, fast-tracks toward Combat |
| Suppressed shot | Low, short range | Only very close enemies hear — kill distant targets unalerted |
| Sprint footsteps | Medium | Nearby enemies turn Suspicious, move to investigate |
| Walk / crouch-move | Low | Audible only at close range |
| Reload / vault / mantle | Low | Small local tell |
| Discovered body | (visual) | Nearby enemies → Searching, raises the global alert |

### Suspicion meter & state ladder

Each enemy holds a per-target suspicion value (0→100) that decays when the stimulus stops. Thresholds drive the state:

- **Unaware / Dormant** — patrol route or guard post. The placed stealth-phase force lives here; **silent-takedown-able** from behind.
- **Suspicious** — low threshold. Stops, faces the stimulus, questioning bark ("Did you hear that?"). Holds post. Decays if contact breaks.
- **Searching** — mid threshold. Moves to the investigate point, sweeps, checks behind cover, pulls in nearby allies (shared awareness). Times out → patrol, but stays edgy for a while.
- **Combat** — meter maxed, confirmed sight, took damage, or a nearby ally engaged. Threat known; squad coordination and the director engage.
- **Lost contact** (within Combat) — LOS lost → holds/searches last-known position → no reacquire in ~N seconds → back to Searching.

### Global alert level — the seam

Above individual enemies sits a level-wide alert state: **Calm → Searching → Loud**. Going **Loud** is the handoff: the stealth phase ends, the director wakes, reinforcements begin. Triggered by a confirmed combat sighting, an escalated body discovery, or an alarm/objective trip.

### Stealth counters (what the gradual model unlocks)

- **Silent takedown** on Unaware/Dormant enemies (melee from behind).
- **Suppressed weapons** — small, short-range hearing bump; kill without alerting distant enemies (nearby ones in LOS still see it / find the body).
- **Body discovery** bumps nearby enemies to Searching and raises the global alert.
- **Breaking LOS and repositioning** decays suspicion if you were never confirmed.

---

## 5. Archetypes

Seven distinct tactical problems. Each has a signature mechanic and a legible counter.

| Archetype | Tactical role | Signature mechanic | How you beat it |
|---|---|---|---|
| **Grunt** | Baseline threat, fills squads | Competent cover use, burst fire — the yardstick others read against | Standard: flank, suppress, headshot |
| **Rusher** | Punishes camping, collapses distance | Sprints in, fires on the move, melee point-blank, ignores cover, **fearless** | Low HP — burst him down; peel with companion; don't let him arrive |
| **Heavy** | Area denial, holds chokes | Armoured front, sustained LMG suppression, **slow turn** | Flank the turn, hit rear/weakpoints, grenade |
| **Sniper** | Controls long sightlines | **Telegraphed aim-laser** before the shot, relocates nests when found | Break LOS, suppress to spoil the shot, flank, rush (low HP) |
| **Officer** | Force-multiplier, priority target | Role assignment + focus-fire calls + morale aura + rally; unlocks overwatch | Kill him → squad degrades. *Reaching* him (hides behind the line) is the puzzle |
| **Grenadier** | Anti-camp flush | **Telegraphed lob** at your cover to force you out — throws to displace, not kill | Keep moving, kill at range, throwback; nading his cluster dents squad morale |
| **Shield** | Forces flanking, walks you down | **Bullet-proof frontal arc**, advances behind it, peeks sidearm | Flank, grenade, or pincer with the companion; brittle once flanked |

### Mechanic notes

- **Sniper telegraph is load-bearing.** The visible aim-laser is what makes a long-range one-shotter fair — you get warning to duck, suppress, or break LOS. Never a sniper without it.
- **Grenadier supply is limited** (cooldown or count) so he flushes rather than spams, and he arcs at your *cover*, not your body — pure displacement pressure.
- **Officer aura/rally is the roster's keystone** — the entire reason target prioritisation is the smart, rewarded play.
- **Headshots are universal** (`EHitRegion`) — every archetype takes a headshot multiplier; heavy and shield only gate the *easy* shots behind armour.

### Armour model

- **Heavy** — directional damage-reduction: frontal plates soften incoming fire; head and rear are weakpoints taking full/bonus damage. Rewards flanking his slow turn and aiming for weakpoints. (Plates may visibly break off for feedback.)
- **Shield** — a breakable frontal hitbox with its own HP. Blocks all frontal fire until it pops under sustained damage or a grenade, then he's exposed. Rewards focus-fire *or* a flank *or* a nade — never "invincible until you walk around it."

---

## 6. Squad Coordination

### What a squad is

A **designer-tagged group** (the placed force) or a **director spawn-group** (reinforcements). The squad is the coordination boundary — squads do **not** coordinate with each other. This keeps the system tractable and makes multiple squads in one fight read as staggered waves rather than one hive mind.

### The coordinator

One **thin coordinator per squad**. It does not puppeteer enemies; it hands out a few **tactical-slot claims** and orders, and each enemy executes through its own behaviour tree. Built directly on the existing cover-claim primitive: *"claim a cover slot"* generalises to *"claim the suppressor role / claim the left-flank slot."*

### Three tiers

- **Baseline (always on, no officer needed):** shared sightings (one spots you → the whole squad knows your last-known position), spacing (never two in the same cover, no clustering), one opportunistic flanker at a time, focus-fire only when someone calls a target.
- **Officer alive:** assigns explicit roles (suppressor + flanker), calls focus-fire targets, raises allies' morale floor.
- **Bounding overwatch (officer-gated):** the suppressor pins the target with sustained fire while the flanker moves to a flank-EQS slot, then they swap. The flanker advances **only while suppression is live** — if the suppressor reloads or dies, the flanker holds or aborts. This is the highest-risk maneuver and is gated behind the officer, so its absence in leaderless squads is diegetic.

### The hard rule

**Local survival always overrides squad orders.** Low HP → retreat; grenade incoming → dive. Both pre-empt any assigned role — orders sit *below* self-preservation in the behaviour tree. A squad never marches a man to his death because the coordinator told him to.

### Shared cover pool

Enemies and the companion draw from the **same** `AICoverSlot` registry, each scoring slots against *their own* threat (cover is directional). Emergent payoff: the companion can deny a key pillar to enemies by claiming it first, and vice versa. No separate enemy-cover markup to author.

---

## 7. Morale & Suppression

Two linked systems at different speeds. **Suppression is an AI-reaction mechanic — it applies to enemies and to the companion, but never to the player.** (The player self-regulates through damage: peeking into accurate fire already hurts; a second control-stealing layer adds nothing.)

### Suppression (fast, twitch)

Rises from rounds cracking *near* an AI (not just hits); decays in a second or two. High suppression → flinch, duck behind cover, can't aim, won't peek. This is the mechanic that makes covering fire matter — and it is a clean thesis beat: the companion lays down fire, the enemy's head goes down, the player repositions or revives.

### Morale (slow, the arc)

A confidence value (per-enemy and per-squad) that drifts **down** from events (ally dies nearby, officer dies, flanked, outnumbered, sustained suppression, low HP) and **up** from winning signs (hits on the player, the player retreats, reinforcements arrive, officer rally). It gates *behaviour*, not aim:

- **Confident** — aggressive: push, flank, hold exposed angles.
- **Low** — defensive: hug cover, fall back to deeper cover.
- **Floor = fall-back.** Enemies never flee or surrender (per design call). "Broken" means *turtle hard / retreat to the deepest cover*, not gone. Fanatical-tagged enemies and the extraction wave ignore morale entirely for relentless pressure.

The player never sees a meter — morale is read through **behaviour and barks**.

### Per-archetype profiles

| Archetype | Morale profile | What you see |
|---|---|---|
| **Grunt** | Baseline | Suppresses, falls back when losing, turtles if isolated or the officer dies |
| **Rusher** | Fearless | Barely suppresses, ignores morale, keeps pushing after the squad breaks |
| **Heavy** | High floor, slow to suppress | Shrugs off suppression, anchors the line, holds even when others fall back |
| **Sniper** | Brittle | Suppressed by one near-miss, relocates nests under pressure. Brave only while hidden |
| **Officer** | Morale *source* | Raises nearby allies' floor, can rally/un-pin. Cautious himself. Kill him → squad turtles |
| **Grenadier** | Normal | His nades dent *enemy* morale too — nading his cluster makes the squad turtle |
| **Shield** | Fearless up, brittle broken | Advances fearlessly behind the shield; collapses to defensive once flanked or broken |

The rusher who won't stop and the sniper who bolts at the first crack are characterised *entirely* through morale parameters — one system, seven personalities.

### Companion suppression guardrails

The companion can be suppressed by enemy fire (it ducks, holds, repositions to safer cover) — pure upside, it reads as a real soldier rather than a fearless robot. Two guardrails: a suppressed companion stays **cautious, not cowering** (keeps contributing, just smarter about exposure), and suppression **never overrides revive** — a pinned companion still comes for a downed player.

---

## 8. Combat Feel

### Accuracy — enemies are not aimbots

They reuse the companion's accuracy-settle model in reverse:

- A deliberate **first-burst grace** — sloppy aim on first acquiring the player (the player's reaction window).
- Aim **tightens** while they hold on target, **widens** when the player moves, breaks LOS, or the enemy is suppressed.
- A short **reaction delay** on acquisition so a fight never opens with an instant headshot.

Reaction time / base spread / settle rate / peek frequency are the **difficulty knobs** — and the thesis's "tune the AI and measure it" lever.

### Grenades

Telegraphed and fair: an arc indicator + "Grenade out!" bark + a fuse window to move (or throw back, if throwback ships). Limited supply so they displace rather than spam. **Frag only to start**; smoke and flash are stretch — smoke is a strong squad-synergy piece (officer calls it to cover an advance).

### Hit reactions & death

The player must *see* shots land: flinch/stagger scaling with hit, suppression duck, a special headshot reaction, heavy's plates visibly breaking off. Ragdoll on death. **No enemy DBNO** — enemies die outright; the down-and-revive loop is player/companion only, which keeps the thesis centrepiece protected. **Damage numbers off** (grittier, tactical feel).

### Barks — the legibility layer

Every invisible system above (squad coordination, morale) is *wasted* if the player can't perceive it. Barks are not polish; they are how tactics become counterable.

- **Detection:** "Did you hear that?" / "Search the area!" / "Contact!"
- **Tactics:** "Flanking left!" / "Suppressing — move up!" / "Grenade out!" / officer designations ("Focus the one on the left!")
- **Morale:** "Man down!" / "He's got us pinned!" / "Falling back!"

**Scope note:** barks ship as **on-screen subtitle text + simple SFX first**, full VO later. The *information* is load-bearing, not the voice acting.

---

## 9. The Director & Encounter Flow

Two regimes joined at the global alert seam (§4).

### Stealth phase (Calm) — placed & dormant

Designers hand-place the defending force: patrols, guard posts, dormant clusters. It is *knowable* — the player can watch, learn the patterns, and plan. The director is asleep; no spawning. This is where silent takedowns and suppressed weapons earn their keep. **Calm → Loud** trips on a confirmed combat sighting, an escalated body find, or an alarm/objective trigger.

### Combat phase (Loud) — the director owns pressure

Once Loud, the director monitors player state (position, health, recent intensity, objective progress) and spawns reinforcements to hold a **tension sawtooth**: build → peak → **relief** → build. The relief beat matters most — after a big fight it backs off so the player can breathe, reposition, revive, and rearm, then it ramps again.

- **Spawning:** out of sightlines, from nav-valid designer spawn zones (barracks, stairwells, off-map ingress), at a sane distance band. Reinforcements arrive as **pre-formed squads** so coordination works on contact.
- **Composition, not just count:** rushers to crack a turtle, grenadier to flush a camp, sniper to punish open movement, heavy/officer to escalate.
- **Phase escalation:** infiltration light → objective moderate → **extraction = crescendo** (highest intensity, heavies unlocked, *repopulates cleared areas*). This is what makes the High-Rise Tower's "fight back down through cleared floors" land.
- **Perf cap as a pacing tool:** a hard max-concurrent-AI ceiling (~20 active) means reinforcements trickle in as the front thins — natural pacing instead of a deathball. The director queues rather than exceeding it.

### Director feel — adaptive pacing, honest composition

The director adapts **when** pressure comes (relief after a big fight, ramp when the player is cruising) but does **not** conjure the perfect counter every time. Composition is weighted-random by phase with only light adaptation. The result is a living tension curve without the "it's reading my mind / cheating" frustration.

All director behaviour is **data-driven** (tension curve, spawn cadence, intensity ceiling per phase, composition weights, max-alive cap) — tunable without code, and measurable for the thesis.

---

## 10. Single-Player Framing

Co-op is cut from the design target. What this means concretely:

- The **companion is the only ally.** No co-op scaling, no multi-player threat distribution, no replication-testing burden.
- **Targeting is threat-scored** between the two allies: enemies weigh player vs companion by proximity + LOS + recent damage-to-me/squad + exposure. Lighting them up draws aggro; the companion is a valid target, which pulls fire off the player and makes the companion feel like a real teammate. Officer focus-fire overrides individual picks within a squad.
- **The downed-and-revive money shot** is now purely player ↔ companion: enemies push a downed player to deny the revive; the companion's revive-priority exists to counter exactly that.
- Architecture stays **authority-clean** so MP could return, but it is not a design target.

---

## 11. Thesis Hooks

Where the enemy is explicitly engineered to generate measurable companion moments:

- **Suppression relief** — the companion's covering fire visibly pins enemies, enabling the player's repositioning. Measurable: enemy time-suppressed, player advances enabled.
- **Cover re-evaluation under flush** — the grenadier (and flanking squads) forces the companion to abandon flanked cover, exercising its EQS re-evaluation. Measurable: cover-switch frequency and quality under pressure.
- **Revive under fire** — the director's downed-push creates the highest-stakes revive scenario. Measurable: revive success rate under varying enemy pressure.
- **Target prioritisation payoff** — the officer makes "shoot the right enemy first" a rewarded read for both player and companion. Measurable: does the companion prioritise high-value targets?
- **The difficulty knobs** (reaction time, spread, settle, peek rate, director intensity) are the controlled variables for the thesis's AI-tuning and comparative analysis.

---

## 12. Recommended Build Sequence

> **The implementation chat owns the final sequencing.** This is a recommendation, not a mandate — offered because the spec is large and a testable, de-risked order matters. Each milestone leaves a *working* (if simpler) enemy, and the riskiest maneuver is isolated last where it can be cut without leaving holes.

1. **Skeleton** — replace the `AEnemyBase` dummy with `AEnemyCharacter` + controller + archetype-data + faction + base BT (patrol/search/combat) + perception. Grunt only. → *Grunt patrols, spots you, takes cover, fires, dies.*
2. **Awareness** — suspicion meter, states, sound events, global alert, stealth counters. → *Sneak a dormant patrol, get made, watch it search and give up.*
3. **Roster** — the 7 archetype data-assets + combat sub-trees + bolt-on components (armour, shield, lob, telegraph, aura). → *Each type fights distinctly with its own counter.*
4. **Morale & suppression** — two layers, per-archetype profiles, hit reactions, fall-back, companion suppression. → *Suppress an enemy and it ducks; kill an officer and the squad turtles.*
5. **Squad baseline** — the coordinator on the cover-claim primitive: shared sightings, spacing, opportunistic flanks, focus-fire, officer roles. → *A squad spreads and flanks — minus bounding overwatch.*
6. **Director** — dormant force, Calm→Loud handoff, reinforcement waves, escalation, perf cap. → *Go loud, get adaptive pressure, extraction repopulates.*
7. **Bounding overwatch** — the officer-gated suppress-and-advance, isolated at the end. → *Officer-led squad pins-and-bounds; degrades cleanly when sync breaks or the officer dies.*

**Barks** thread through milestones 2–7 as each system needing a telegraph comes online (subtitle text first, VO later).

---

## 13. Deferred / Out of Scope

- **Surrender / rout** — cut. Morale floors at fall-back.
- **Player-side suppression** — cut. Suppression is AI-only.
- **Co-op multiplayer** — cut from the design target (architecture stays MP-capable).
- **Smoke / flash grenades** — stretch. Frag first.
- **Grenade throwback** — stretch.
- **Full VO barks** — subtitle text first.
- **Body-dragging / hiding bodies** — not in scope unless revisited.
- **Exact tuning values** — deferred to implementation and balance passes.
