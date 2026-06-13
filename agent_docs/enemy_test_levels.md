# EXTRACTION — Enemy AI Test Level Guidance

**Status:** Living guidance. Created 2026-06-09 on `AI-Companion-Prototype`.
**Companion to:** `enemy_design.md` (vision), `enemy_code_plan.md` (phase contracts + QA lists), `companion_testing.md` (QA style).
**Purpose:** What to build in-editor to test each phase, and how those test spaces grow toward the real game's level structure instead of being throwaway boxes.

All distances in metres (1m = 100uu). Numbers reference the `DA_Enemy_*` defaults — if you retune the DA, retune the spaces.

---

## The two maps

Build and keep **two** maps. Don't make seven.

**`L_EnemyGym`** — a flat modular test map of labelled zones, one zone per system. Fast iteration, no art, text-render labels on the floor. Every phase adds a zone; old zones stay (regression). This is where you isolate a system and stare at it.

**`L_ExtractionSlice`** — a compressed three-act extraction level that grows a wing per phase. This is where you verify the *game* works, not just the system. Shape (mini High-Rise):

```
ACT A — Approach/Perimeter (stealth):  open yard + fence line + 2 entries
ACT B — Interior objective (combat):   2 floors + connecting stair + small atrium
ACT C — Extraction (crescendo):        route back OUT through A/B, exfil pad in the yard
```

The slice is the dissertation demo level in embryo. Authoring effort here transfers; gym zones are sacrificial.

### Why both
The gym answers "does suspicion decay correctly?" in 30 seconds. The slice answers "does sneaking the perimeter, going loud at the objective, and fighting back out *feel* like an extraction shooter?" — which is the design's actual test (design §9: knowable placed force → director-owned pressure → extraction repopulation).

---

## Authoring rules that the code enforces (read before placing anything)

**AICoverSlot placement** (shared pool — companion and enemies claim from the same registry):
- One slot = one occupant. Place **more slots than expected combatants** per fight space (rule of thumb: 2× the bodies you expect fighting there, split across both facings).
- Slots are directional: `FireArcDegrees` (default 120°) must face the expected threat or the registry rejects them for that fight. **Place slots facing both ways** in any space where enemies and the companion both fight — a pillar gets two slots, opposite sides.
- Crouch-height slots = fire-over-the-top; they work anywhere. Stand-height slots are **rejected as hide-only unless** `bIsPeekableCornerStart/End` is set — only mark corners that really are step-out-able.
- Enemy cover search radius is **12m** (`CoverSearchRadius`) from where the enemy stands when combat starts. If no slot is within 12m of the engagement line, enemies fight in the open (by design — but know you're authoring that).
- Companion search radius is also 12m — keep slot chains between fight spaces so cover-switching has somewhere to go.

**Patrol routes:** `APatrolRoute` points must sit on navmesh (drag the widgets, then `P` to check). `bLoop` for circuits, off for ping-pong sentry lines. Default 2s wait per point. No route assigned = static guard post.

**Navmesh:** every patrol point, cover slot, investigate-able noise spot, and spawn zone must be on connected navmesh. The single most common "AI does nothing" cause.

**Perception distances (DA defaults)** — mark these bands on the gym floor with coloured decals/text:
| Band | Default | Means |
|---|---|---|
| Sight radius | 25m | outside this you are invisible |
| Lose-sight | 30m | once seen, tracked out to here |
| Hearing | 20m | max range any noise can matter |
| Sprint footsteps | 12m | sprint is loud |
| Walk footsteps | 4m | walking is quiet |
| Crouch footsteps | 1.5m | effectively silent |
| Unsuppressed shot | 30m | wakes a whole area |
| Suppressed shot | 7m | kills stay local |
| Auto-confirm | 3.5m | point-blank = instant Combat |
| Engage band | 6–18m | where enemies want to fight |
| Takedown | 1.6m, rear 120° | from behind only, Unaware only |

**Debug workflow:** Gameplay Debugger (`'` key) → Perception + Behavior Tree categories on a selected enemy; `stat AI` for budget; `LogEnemyAI` in Output Log (state transitions + global alert print there); eject (`F8`) and fly to watch squads from above; `slomo 0.25` for reading peek/burst timing.

---

## Phase 1 — Skeleton (Grunt). Gym Zone A: "Combat Yard"

**Zone A layout (~30×30m):**
- Open yard, waist-high crouch-cover scattered at 4–8m spacing on **both** halves (enemy side + player/companion side), 8–10 slots total.
- Two stand-height walls with marked peekable corners.
- One 3-point patrol loop along one edge; one static guard post behind cover.
- A long 25m+ approach lane so you can stand outside sight radius and step in.

**What this zone proves:** the full Phase-1 QA list (plan doc) — patrol, detect, cover claim, peek-burst rhythm, headshots, search-and-give-up, FF, claim exclusivity (kill a grunt near its slot, watch the second grunt claim it… or not, if you didn't place spares).

**Slice work this phase:** block out ACT A's yard only — fence, two entries, the patrol loop becomes a *perimeter* patrol. Same actors, real context.

**Companion check:** fight one grunt with the companion present — companion and grunt must never contest the same slot (different facings), and the grunt should switch target to the companion when it's nearer.

---

## Phase 2 — Awareness ladder. Gym Zone B: "Stealth Lane" + Zone B2: "Body Alley"

**Zone B layout (a 50×12m lane):**
- Distance decals at 5m intervals from a guard post at one end (you need to *see* the 25m sight edge and 20m hearing edge while testing).
- Cover stones at 10m intervals along one side (break-LOS-and-decay testing).
- One dormant 3-guard cluster mid-lane facing away from a flanking path (takedown approach).
- A lit/unlit half is **not** needed — lighting modifiers are deferred (design §13).

**Zone B2 (an L-corridor):** lone patroller on a short ping-pong route; a second guard whose patrol crosses the first one's route 30s later — kill patroller one, watch guard two find the body. Add a third distant guard to verify the *second* body (or body-while-alerted) goes Loud.

**What these zones prove:** the Phase-2 QA list — meter fill vs stance/speed/distance/angle, decay, sound investigate points (never sound→Combat), suppressed vs loud kills, takedown gating, body discovery escalation, Loud waking the dormant cluster, bark subtitles + dedup.

**Slice work this phase:** ACT A becomes playable stealth — perimeter patrols you can learn (design: "knowable"), a dormant pair at each entry, one interior-visible body-discovery setup. Verify a full ghost-run of ACT A is possible with crouch movement + suppressed kills + takedowns, and that one mistake escalates believably.

**Verticality check (do it now, cheap):** put one guard on a 4–5m platform/floor above the lane. Hearing is a 3D sphere — sprint under him and confirm he investigates downstairs (this is the High-Rise's whole gameplay; catch nav problems early).

**Companion check:** companion follow during your stealth approach — its footsteps/vault noise correctly *do* alert enemies (it's a combatant); confirm that reads fair, not buggy. If it ghosts worse than you, that's Phase-4+ tuning (posture), note it, don't fix it here.

---

## Phase 3 — Roster. Gym Zone C: "Archetype Lanes"

Seven short lanes off a central spawn hub, each shaped as its archetype's counter-lesson (design §5 table):

| Lane | Shape | What it proves |
|---|---|---|
| Grunt | reuse Zone A | the yardstick |
| Rusher | 20m straight, no cover | he closes and melees if you camp; dies easily if you don't |
| Heavy | 4m-wide choke + side flank door | frontal armour shrugs, slow turn punished via the flank, rear/head weakpoints |
| Sniper | 40m+ sightline + 3 elevated perches + cover chain underneath | laser telegraph always precedes the shot; relocates when pressured |
| Officer | squad cluster with officer behind a second cover row | aura buffs visible; killing him degrades; *reaching* him is the puzzle |
| Grenadier | one strong cover stone facing him (your camp spot) | lobs at your cover only when you camp; limited supply; arc telegraph |
| Shield | 10m corridor + two side gaps | walk-down forces flank; brittle once flanked; nade pops it |

**Slice work this phase:** populate ACT B with role-cast placements — grunts in the lobby, heavy on the stair choke, sniper overlooking the atrium, officer + squad in the objective room, grenadier covering the camp-able doorway. The slice should now play as "each room teaches a counter."

**Companion check (thesis hooks, design §11):** grenadier flush forces the companion's cover re-evaluation; officer fight shows target-prioritisation value.

---

## Phase 4 — Morale & suppression. Gym Zone D: "Pressure Ring"

**Zone D layout:** concentric cover rings — a forward shallow ring and a rear "deep" ring 10–12m further back (fallback needs *deeper cover to exist*, and within the 12m search radius of the front ring). Officer + 4 grunts in the front ring. An ammo crate / unlimited-reserve test weapon for you to hose suppression with.

**What it proves:** suppression ducks heads per-archetype profile (grunt cowers, heavy shrugs, sniper bolts, rusher ignores), morale fallback to the deep ring when losing, officer-death turtle, hit reacts + ragdoll, suppressed-companion guardrails (cautious, still revives).

**Slice work this phase:** the ACT B objective room becomes the morale showcase — defenders fall back from the door ring to the deep room ring as you win. Verify the arc reads through behaviour + barks alone (no meters — design §7).

**Companion check:** the money loop — go down in the open, enemy pressure on your body, companion revives under covering fire. Stage it in Zone D deliberately.

---

## Phase 5 — Squad baseline. Gym Zone E: "Flank Field"

**Zone E layout (~40×40m):** central player strongpoint with cover; enemy approach side with a cover field (slots spaced > squad min-spacing so spreading is visible); **two clear flank routes** (left/right hedges) with their own cover chains and off-facing approach geometry — the flank EQS needs reachable points outside your facing. Two 4-man squads with distinct `SquadId`s, one officer-led.

**What it proves:** spacing (never two in one cover, no clustering), shared sightings (spot one → squad converges), exactly one flanker at a time, flanker aborts when wounded, officer roles + focus-fire + rally, squads do NOT share info across `SquadId` (engage squad 1, squad 2 stays cold until it perceives you — stagger them far apart to verify).

**Slice work this phase:** ACT B floors each get a tagged squad. Fighting floor 1 must not pre-alert floor 2's squad beyond the global-alert effects — waves read as staggered, not hive-mind (design §6).

**Companion check:** lighting up an enemy pulls its squad's attention (threat scoring) — the companion drawing aggro off you should be visible.

---

## Phase 6 — Director. Slice-first phase (gym gets a meter, not a zone)

**Gym Zone F (small):** a "spawn closet ring" — 4 `AEnemySpawnZone`s around a glass observation box; god-mode inside, trip the alarm, watch cadence/composition/cap with `stat AI` + alert logs. This zone is for measuring, not playing.

**Slice work this phase (the real test):**
- Place spawn zones **out of sightlines**: behind ACT A's fence corners, ACT B's stairwell backs, one "off-map ingress" at the yard edge. Walk every zone → navmesh → player-space path.
- Mission-phase trigger volumes: ACT A entry = Infiltration, objective room = Objective, objective-complete trigger = Extraction.
- Extraction route must pass back through cleared ACT B/A space — verify repopulation makes "fight back down through cleared floors" land (design §9, the High-Rise beat).
- Verify the sawtooth: after a big fight, pressure visibly eases (relief beat) before rebuilding; alive count never exceeds the cap (~20), reinforcements trickle as the front thins.
- Full loop test: ghost ACT A → objective → go loud → extract under crescendo. This is the first end-to-end extraction-shooter run. Time it; note where pacing sags for DA tuning.

**Companion check:** relief beats are when revive/rearm/reposition happen — confirm the companion's down-time behaviours get room to breathe.

---

## Phase 7 — Bounding overwatch. Gym Zone G: "Bounding Street"

**Zone G layout:** a 35m parallel street — one long suppression sightline down the middle, and an advance lane along one side with a chain of cover slots every 5–6m (the flanker bounds slot to slot). Player strongpoint at the far end. Officer-led squad at the near end.

**What it proves:** suppressor pins (sustained fire at your cover without LOS), flanker advances **only while suppression is live** — shoot the suppressor mid-bound (or wait out his mag) and the flanker freezes/aborts; kill the officer and bounding never happens again (degrades to Phase-5 baseline, diegetically).

**Slice work this phase:** the extraction yard's final defence runs overwatch against your exfil position. Last act of the demo loop.

**Companion check:** companion suppression *against* the bounding squad pins the suppressor — the counter to the maneuver is the thesis beat (suppression relief enabling your push).

---

## Per-phase test session protocol

1. Gym zone first — isolate, use Gameplay Debugger, run the phase QA list from `enemy_code_plan.md`.
2. Slice second — play the act(s) the phase touches, companion always present.
3. Regression sweep — re-run the previous phase's slice act once (10 min).
4. Log tuning deltas into the DA instances, not into notes — the DA is the record.
5. Anything broken → reproduce in the gym zone before reporting (smallest repro).
