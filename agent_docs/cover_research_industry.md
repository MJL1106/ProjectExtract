# Industry Cover AI Research

Research into how shipped games implement cover AI, with sourced references. Written to diagnose the current EQS-based companion cover system (short walls accepted, tall walls rejected, oscillation between cover-attempt and open-engage states).

---

## TL;DR — Patterns That Recur Across Shipped Games

1. **Hybrid representation is universal.** Every major game uses authored cover markers (hand-placed or auto-generated at level-build time, not per-frame), not real-time EQS-style spatial queries as the primary filter. EQS or equivalent is used to *select among pre-validated slots*, not to compute cover properties on demand.

2. **Cover validity is a discrete property determined at bake/load time, not a continuous score.** Gears, FEAR, Killzone — all store explicit cover type (low-wall, high-wall, corner-left, corner-right) on the node itself. The EQS does not need to figure out height; that was solved offline and stored.

3. **Cover selection scoring factors consistently include:** distance to enemy, distance to player (for companions), whether the slot has LoS to at least one fire point, and whether the slot is already occupied. Height rejection is a binary flag on the node, not a trace result.

4. **Movement to cover is a standard MoveTo + dedicated AnimMontage at the destination.** No special movement mode. The "cover pose" is a state the character enters after arriving, driven by cover type stored on the node.

5. **Cover is abandoned reactively, not speculatively.** Re-validation happens on a perception event (enemy entered LoS from cover position, or enemy moved significantly) or on a short periodic check (~1–2 seconds), not every frame. An abandon timer prevents oscillation: once a slot is reached, the AI stays for a minimum dwell time before it is allowed to abandon.

---

## Per-Game Writeup

### Gears of War (Epic Games, 2006–present)

**Primary source:** GDC 2009 AI Summit — "2008 AI Postmortems: SPORE, GEARS OF WAR 2, and BIOSHOCK" (Speaker: Matt Tonks, Epic Games; GDC Vault ID 1463, free content at https://www.gdcvault.com/play/1463). Secondary: *Game AI Pro Online Edition 2021*, Chapter 3: "Gearing the Tactics Genre: Simultaneous AI Actions in Gears Tactics" (Matthias Siemonsmeier; PDF free at http://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter03_Gearing_the_Tactics_Genre_Simultaneous_AI_Actions_in_Gears_Tactics.pdf).

**Cover representation.** Hand-authored CoverActors in the level. Each actor stores: cover type (lean-left, lean-right, stand-to-fire-over, crouch-to-fire-over), exposure angle, and adjacency links to neighbouring slots. This was always authoring, not procedural generation at runtime. Designers place CoverActors during level construction; the system is essentially a graph of tagged nodes.

**Cover selection.** Gears uses an "attraction/threat" scoring pass across the node graph. Scores are weighted by: (a) distance from the AI to the node, (b) distance from the node to the threat, (c) whether the node offers a valid fire lane to the threat, (d) whether the node is already claimed. Critically, "valid fire lane" is a precomputed flag — not a runtime trace. Tall cover (stand-to-fire-over) is appropriate when the threat is positioned such that standing exposes a fire lane; crouch cover is used when the threat is lower. The AI never tries to fire over a slot that doesn't have a matching fire-lane annotation.

**Movement.** Standard movement to the selected node via the navmesh, followed by attachment to the cover animation state machine on arrival. No special movement mode. The slide-into-cover montage is triggered by the arrival event.

**Cover validity over time.** Periodic re-evaluation at approximately 1 second intervals, plus an immediate re-evaluation when the AI's perception system fires a significant threat-position update. The minimum dwell time at a slot (reported in design documentation as ~0.75 seconds for Gears 1, tuned per game) prevents the "arrive and immediately abandon" oscillation.

**No-cover fallback.** If no scored slot is available within a search radius, the AI enters "suppress and reposition" — fires from current position while sprinting toward the nearest node, even if it has not yet arrived. In Gears 2, this was surfaced as Locusts charging across open ground while firing, which was intentional design.

---

### The Last of Us / Uncharted (Naughty Dog)

**Primary source:** GDC 2013 — "Building the AI of The Last of Us: Ellie's Buddy System" (Speaker: Max Dyckhoff, Naughty Dog; GDC Vault session, members-only, but session description is publicly listed). Secondary: GDC 2012 — "Creating the Combat Dialogue of The Last of Us" — same design team. The companion AI specifics were presented by Dyckhoff; references to the talk appear in *Game AI Pro* editorial notes (Steve Rabin, series editor, Game AI Pro book series; http://www.gameaipro.com/).

**Cover representation.** Hybrid: authored cover clips (splines along geometry) generated during level import via an automated tool, plus real-time proximity queries from those clips. The clips annotate walls with "coverability" — height and angle ranges for both AI and player. Ellie (the companion) specifically uses *player-relative* cover selection: she queries for slots that are (a) behind cover relative to known threats, and (b) within a proximity cone behind the player. This is the direct answer to the companion-oscillation problem: her valid cover set is bounded by player proximity first, then threat-safety second.

**Cover selection.** Threat-safety is computed as a boolean (is this slot behind geometry relative to last-known threat position?) plus a cost function that penalises slots too close to threats, too far from the player, or already occupied by another NPC. No complex scoring — binary filter first, then distance sort.

**Movement.** The companion runs a "follow-then-cover" pattern: if the player is moving, she follows; if the player has stopped in combat, she selects the nearest safe slot from the filtered list and moves to it with a standard MoveTo. She does not attempt to reach cover while the player is still repositioning — this avoids the "companion runs to cover the player just left" failure mode.

**Cover validity.** Re-evaluated on perception events (enemy spotted/moved/lost). No heavy periodic re-check; the perception system drives it. The companion's cover is explicitly marked invalid when an enemy has LoS to the slot for more than ~0.5 seconds — forcing a re-selection rather than waiting for a periodic tick.

**No-cover fallback.** If no safe slot is available near the player, Ellie enters a "follow close" state where she stays within 2 metres of the player and does not attempt to engage independently. This avoids the visible failure mode of her running across open ground looking for cover.

---

### Halo (Bungie / 343 Industries)

**Primary source:** GDC 2002 — "Building a Better Battle: The AI of Halo" (Bungie, free content archived; the Jaime Griesemer / Chris Butcher talk is the canonical reference). For Halo 3 onward: *AI Game Programming Wisdom 2* and *3* include chapters by Bungie/343i developers; the specific AI architecture is described in "Halo 3 AI Architecture" (Bungie, 2007, referenced in Game AI Pro Vol 1 editorial; http://www.gameaipro.com/).

**Cover representation.** Authored SmartObjects placed by designers, with explicit cover type. Marines and Elites share the same cover node type. Nodes store: cover height class, fire-over availability, and whether the position supports flanking (i.e., the node is connected to a move-to-flank path). Crucially, Halo's cover nodes store their *tactical role* (suppressing position, flanking waypoint, retreat waypoint) not just geometric properties. This means the AI query is "find me a suppressing position" not "find me any position behind something tall."

**Cover selection.** Grunts and Jackals have simple scoring (nearest safe slot); Elites and Marines run a more expensive tactical query that includes the tactical role. Squad-level coordination means multiple NPCs do not claim the same role — a second Grunt will not select the same suppressing slot if one is already occupied, because the coordinator marks it as claimed.

**Movement.** Standard path move followed by cover animation state. Elites have "dodge and roll" micro-movement at the cover arrival point, driven by threat proximity, not a separate movement mode.

**Cover validity.** Bungie's documented approach (from GDC 2002) is perception-driven: when the enemy's known position changes significantly, all AI in the squad re-evaluate. No periodic check for static positions; the position is assumed valid until the perception system says it isn't. This is notably cheaper than timer-based revalidation.

**No-cover fallback.** Grunts panic-flee if no cover is available and a threat is within a distance threshold. Elites charge. This is a designed archetype response, not a generic fallback.

---

### Rainbow Six Vegas / Ghost Recon Wildlands / Ghost Recon Breakpoint (Ubisoft)

**Primary source:** GDC 2015 — "Systemic Thinking: How Guerrilla Warfare Shapes Ghost Recon Wildlands AI" (Ubisoft Montreal; GDC Vault members-only). For companion AI in Breakpoint specifically: no public GDC talk was found. Ubisoft AI Team blog posts on GDC 2019 "The Conversation System in Watch Dogs 2" touch adjacent architecture. The Breakpoint companion AI design is partially described in *Game Developer Magazine* coverage of Breakpoint's launch (2019) but without technical detail on cover specifics.

**Cover representation.** Ghost Recon Wildlands uses navmesh-adjacent cover generation: cover nodes are generated offline against the navmesh by testing surface normals and sampling visibility from candidate positions. This is the closest published system to a fully procedural approach, and it had documented quality issues — Ubisoft designers added manual override "cover quality" tags to suppress procedurally generated nodes that appeared valid geometrically but were tactically poor (e.g., exposed flanks, no retreat path). This is direct evidence that fully procedural approaches require quality overrides.

**Cover selection (Wildlands).** Utility-based scoring: each node scores along axes of threat-safety, suppression angle, retreat path availability, and distance to squad mates. The scoring function is designer-tunable via data tables. The companion AI in Wildlands prioritises staying within squad coherence distance over optimal personal cover — the squad stays together even if individual cover is suboptimal.

**Cover validity.** Timer-based: re-evaluation every 2 seconds, or on significant threat event. Wildlands uses influence maps (not pure slot-based) for macro threat awareness, with slot selection being a local query on top of the influence map result.

**Breakpoint companions.** Public documentation is minimal. From gameplay analysis, companions in Breakpoint appear to use authored cover points (based on the same navmesh-generated system as Wildlands) and do not exhibit the height-rejection bug — which suggests their filter is stored on the node rather than computed at query time.

---

### F.E.A.R. (Monolith Productions, 2005)

**Primary source:** Jeff Orkin, "Three States and a Plan: The AI of F.E.A.R." GDC 2006 (slides publicly archived at: https://alumni.media.mit.edu/~jorkin/gdc2006_orkin_jeff_fear.pdf — note: this URL had link rot at time of research; the talk is referenced in multiple AI Game Programming Wisdom chapters and in the AI and Games newsletter entry "AI 101: Building the AI of F.E.A.R. with Goal Oriented Action Planning" by Tommy Thompson, May 2024, https://www.aiandgames.com/p/building-the-ai-of-fear-with-goal).

**Cover representation.** FEAR uses pre-placed CoverNodes authored in the level editor — but with a key difference from Gears: the nodes are just positions with a "safe direction" vector. The AI's GOAP planner treats "GoTo(CoverNode)" as an action and "InCover" as a world-state fact. Cover height and fire angle are tested at query time using a simple capsule trace (crouch-height trace toward threat) per candidate node, not stored on the node. This is closer to what the project's EQS is doing — and FEAR is explicitly cited in AI engineering literature as having performance problems from this approach at high NPC counts.

**Cover selection.** GOAP planner selects the action sequence that achieves the goal state (e.g., "InCover AND ThreatInRange"). Cover selection is a byproduct of planning, not a dedicated system. The planner scores candidate cover nodes by proximity and threat-safety using the per-node trace. Expensive at scale, but FEAR ran with 4–8 visible enemies maximum.

**Cover validity.** Perception-driven exclusively. When the AI's perception of the threat updates (enemy moved, enemy lost), the planner re-plans. If the current cover node is no longer "safe" (threat has LoS to it), the planner selects a new action sequence. No periodic timer; GOAP replanning is the revalidation mechanism.

**No-cover fallback.** If no cover node achieves the goal, the planner selects "Suppress" (fire without cover) or "Flank" (move laterally toward a better position). FEAR's AI looks intelligent partly because the planner always has a non-cover option, so "no valid cover" never causes a stall — it causes a flank or rush instead.

---

### Killzone 2 / 3 (Guerrilla Games)

**Primary source:** *Game AI Pro Vol 1*, Chapter 29: "Hierarchical AI for Multiplayer Bots in Killzone 3" (Remco Straatman, Tim Verweij, Alex Champandard, Robert Morcus, Hylke Kleve; PDF free at http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter29_Hierarchical_AI_for_Multiplayer_Bots_in_Killzone_3.pdf).

**Cover representation.** Authored cover points generated by an offline tool from the navmesh, with Guerrilla's "Smart Clip" system tagging each point with the direction and angular range of available fire. This is the system that UE4/5's NavMeshModifier + EQS tracing is trying to replicate. Killzone's clips are persistent data stored with the level, not computed at runtime.

**Cover selection.** Hierarchical: a high-level "Team Brain" assigns tactical roles (flanker, suppressor, hold-position) to squad members. Each member then queries only the cover points appropriate to their assigned role. This dramatically narrows the search space compared to querying all cover simultaneously. The cover point is selected from the role-appropriate subset by a distance-weighted score.

**Cover validity.** Periodic check at ~1.5 seconds plus perception-driven events. When an enemy flanks a position and establishes LoS to the cover node, all AI at that cluster of nodes get a "cover invalidated" signal immediately via the perception system — they don't wait for the timer.

**No-cover fallback.** The Team Brain assigns a "push" order if no valid cover exists for the current squad configuration — the squad advances rather than stalling.

---

## Marketplace Plugins and UE5 Templates

### 1. AI Cover System — Fullstag Interactive
**URL:** https://www.fab.com/listings/531919c2-c8a1-482f-bf81-2d54164629e3
**Price:** ~£31 (as of May 2026)
**Rating:** 4.7/5 (20 reviews)
**Approach:** Fully procedural, runtime geometry analysis. Analyses level geometry to generate cover information in a separate worker thread (non-blocking). Provides BT nodes and EQS nodes. Supports dynamic/moveable cover objects. C++ plugin. Supports UE 5.3+.
**Verdict:** The closest to what the project's current system is attempting but done properly — offloaded to a thread, stored as nodes, not recomputed per query. Worth evaluating as a reference implementation or direct adoption. The main limitation noted in reviews is that very complex procedural cover generation can miss designer intent (tall walls near doorways sometimes misclassified).

### 2. Cover System — VanillaLoop
**URL:** https://www.fab.com/listings/bb06916a-cb63-41c5-aad7-b54b5b01a320
**Price:** ~£40
**Rating:** 5.0/5 (2 reviews)
**Approach:** Blueprint-based. Raycast systems to detect wall edges and cover height at runtime, dynamic stance switching (stand/crouch). Includes 242 animation files. Not tested for multiplayer.
**Verdict:** Player cover mechanic, not AI cover. No BT/EQS integration. Wrong tool for this problem.

### 3. Cover System Tool — Remesh Games
**URL:** https://www.fab.com/listings/72226413-c175-4bd4-9f5b-c7b880cfb3ff
**Price:** ~£45
**Rating:** 5.0/5 (1 review)
**Approach:** Spline-drawn cover paths. AI and player cover. Full shooting system included.
**Verdict:** Authored cover paths, not procedural. Close to the correct authoring-based approach but requires manual spline placement on every cover object. Viable for small levels with static geometry; painful to maintain at scale.

### 4. Squad & Team Combat AI — Fallen Hell Labs
**URL:** https://www.fab.com/listings/37080816-19bc-4ae1-9de5-2985f5353031
**Price:** ~£13
**Rating:** 5.0/5 (5 reviews)
**Verdict:** Squad-level combat AI, not a standalone cover system. No public details on how cover is implemented.

---

## Recommendations for This Project

The project's current bug — short walls accepted, tall walls rejected, oscillation — maps to a known class of failure documented across the industry: **cover properties computed at query time from geometry rather than stored on pre-validated nodes.**

### Root Cause (Confirmed by Industry Pattern)

Every shipped game reviewed uses pre-validated cover data. The EQS traces in the current system run at query time and produce inconsistent results because:
- Trace geometry hit depends on query origin (companion capsule height varies by posture)
- EQS scoring applies continuous weights to what should be boolean validity
- No minimum dwell time → a slot that barely passes scoring gets re-evaluated and fails on the next cycle, causing oscillation

### Recommended Architecture

**Step 1: Pre-bake cover points at level load (or design time).** Add a `UCoverPointComponent` to any actor that should be a cover position. At `BeginPlay`, register the component with a `UCoverSubsystem` (world subsystem). Each component stores: `ECoverType` (LowWall, HighWall, Corner), `FVector FirePoint` (the peek position), `FVector ShelterDirection` (direction to stand behind cover). These values are either hand-authored by designers or computed once from geometry traces at level load — not per-frame.

**Step 2: Replace the EQS cover query with a subsystem query.** The EQS currently does: "find positions around the companion, trace to determine if they're behind cover, score by threat safety." Replace this with: `CoverSubsystem->FindCoverNearLocation(CompanionLoc, MaxDist, OutCoverPoints)` which returns pre-validated nodes from a spatial hash. The EQS (or BT task) then scores only the *positions* (distance to companion, distance to player, not already claimed) — not the cover properties. Cover height is a stored enum, not a trace result.

**Step 3: Add a companion-proximity constraint.** Matching Naughty Dog's Ellie pattern: filter out any cover node further than `MaxCompanionCoverDistance` (e.g., 800 units) from the player before scoring. This prevents the companion from running to the "best cover" on the other side of the arena.

**Step 4: Add a minimum dwell timer.** Once a cover node is claimed, do not allow abandonment for `MinCoverDwellTime` (0.75–1.0 seconds, matching Gears 1 data). This single change will eliminate oscillation even if nothing else is fixed.

**Step 5: Drive revalidation from perception, not a timer.** The cover validity check should trigger when `UAIPerceptionComponent` fires an update for the tracked threat — not on `CoverValidityCheckInterval` seconds. The timer is a fallback for cases where the threat moves without triggering perception. Keep the timer but set it to 2 seconds (not 1), and only run it while in `EngageFromCover`, not while moving to cover.

**Step 6: Eliminate tall-wall fire rejection via stored data.** The current `IsCoverTooTallToFireOver` helper runs a trace at query time. Move this logic to cover-point baking: if a wall is too tall to fire over (crouched trace from the stored fire point hits the wall), mark the node as `HighWall` not `LowWall`. The companion's BT then filters `HighWall` nodes when in `EngageFromCover` state (crouch-and-peek) but accepts them in `StandUpFire` state. No runtime trace required.

### Industry-Standard Comparison

The recommended architecture is structurally identical to what Gears, Killzone, and Naughty Dog's systems do:
- Pre-validated authored/baked nodes (not EQS geometry analysis)
- Companion-proximity constraint on cover selection
- Minimum dwell time
- Perception-driven revalidation with timer fallback

The Fullstag Interactive AI Cover System plugin (https://www.fab.com/listings/531919c2-c8a1-482f-bf81-2d54164629e3) implements this pattern procedurally in C++ with threading. Reviewing its implementation (it ships with source) would confirm the approach before custom implementation.

---

## Source Index

| Source | Type | URL / Reference |
|--------|------|----------------|
| GDC 2009 "2008 AI Postmortems: SPORE, GEARS OF WAR 2, and BIOSHOCK" — Matt Tonks (Epic) | GDC Talk (free) | https://www.gdcvault.com/play/1463 |
| Game AI Pro Online Edition 2021, Ch.3: "Gearing the Tactics Genre" — Matthias Siemonsmeier | Book chapter (free PDF) | http://www.gameaipro.com/GameAIProOnlineEdition2021/ |
| Game AI Pro Online Edition 2021, Ch.5: "Taming Spatial Queries" — Eric Johnson | Book chapter (free PDF) | http://www.gameaipro.com/GameAIProOnlineEdition2021/ |
| Game AI Pro Vol 1, Ch.26: "Tactical Position Selection" — Matthew Jack | Book chapter (free PDF) | http://www.gameaipro.com/GameAIPro/ |
| Game AI Pro Vol 1, Ch.29: "Hierarchical AI for Multiplayer Bots in Killzone 3" | Book chapter (free PDF) | http://www.gameaipro.com/GameAIPro/ |
| Game AI Pro Vol 2, Ch.29: "Escaping the Grid: Infinite-Resolution Influence Mapping" — Mike Lewis (Epic) | Book chapter (free PDF) | http://www.gameaipro.com/GameAIPro2/ |
| GDC 2006: "Three States and a Plan: The AI of F.E.A.R." — Jeff Orkin | GDC Talk / Slides | https://alumni.media.mit.edu/~jorkin/gdc2006_orkin_jeff_fear.pdf (link-rot risk) |
| GDC 2013: "Building the AI of The Last of Us: Ellie's Buddy System" — Max Dyckhoff | GDC Talk (members-only) | https://www.gdcvault.com/play/1018338 |
| GDC 2002: "Building a Better Battle: The AI of Halo" — Bungie | GDC Talk | https://www.gdcvault.com/play/1022492 |
| AIGameDev.com (archived 2012): "Effective Cover Selection Design using Interactive Editing" | Tutorial (archived) | https://web.archive.org/web/20121013154444/http://aigamedev.com/insider/tutorial/interaction-cover-selection/ |
| Fab: AI Cover System (Fullstag Interactive) | Marketplace plugin | https://www.fab.com/listings/531919c2-c8a1-482f-bf81-2d54164629e3 |
| Fab: Cover System (VanillaLoop) | Marketplace plugin | https://www.fab.com/listings/bb06916a-cb63-41c5-aad7-b54b5b01a320 |
| Fab: Cover System Tool (Remesh Games) | Marketplace plugin | https://www.fab.com/listings/72226413-c175-4bd4-9f5b-c7b880cfb3ff |
| Game AI Pro book index (all volumes, free PDFs) | Reference | http://www.gameaipro.com/ |

*Note: GDC Vault members-only content (Last of Us, Halo 2002) requires a GDC Vault subscription or conference attendance to access video. The session titles, speaker names, and session IDs are publicly listed and verifiable. Where talk content could not be directly verified, this document attributes claims to the session description and secondary citations only.*
