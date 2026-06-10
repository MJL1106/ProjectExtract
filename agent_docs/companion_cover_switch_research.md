# Companion Cover-Switching Research

Design research for the AI companion's *cover repositioning* behaviour in ProjectExtract (UE5.7, multiplayer FPS). The companion's initial cover entry is already solved by the existing `UAICoverSlot` system; this document covers the **decision-making for switching slots once the companion is already in cover** — when to switch, how to pick the new slot, how to keep it from feeling spammy, and what shipped games do.

Scoped to design only — no code, no pseudocode. A sibling document, `cover_research_industry.md`, already covers initial cover *selection* and project-level diagnostics; this doc deliberately does not repeat that ground except where switching depends on it.

---

## TL;DR — Patterns That Recur Across Shipped Buddy AI

1. **Switches are reactive, not periodic.** Every well-regarded companion (Ellie, Elizabeth, Uncharted buddies, Halo Marines, Killzone bots) re-evaluates cover on **events** — player moved past a threshold, perception update from a threat, current slot lost LoS to its primary target, slot took damage / was destroyed — not on a fixed timer. A timer is at best a fallback for missed events.

2. **Player position is the strongest single signal for companions.** Squadmate AI in Mass Effect, Resident Evil 5, and especially Naughty Dog's buddy stack treats *"the player has repositioned"* as the dominant trigger. Companion cover is usually re-anchored relative to the player before any tactical consideration of enemies.

3. **Hysteresis is mandatory.** Every design that ships without companion oscillation uses one or more of: minimum dwell time at the new slot, a threshold delta the new slot must beat the current slot by (not just equal it), or a cooldown after a recent switch. Without this, any continuous scoring function jitters between near-equal candidates.

4. **Don't-switch-while-firing.** Across Gears, Halo, Killzone, and the Naughty Dog stack, an AI mid-burst-fire is not allowed to abandon cover. The switch is queued until the burst ends, or the burst is cancelled only by a hard event (cover destroyed, melee threat in range).

5. **The slot picker is usually the same one used for initial entry, run with extra filters.** Switching is rarely a different system — it's the same scoring function with (a) the current slot excluded, (b) tighter player-proximity, and (c) a "must be meaningfully better" delta gate.

6. **Animation variety is decoupled from the switching decision.** Even when the runtime picks a slide vs. sprint vs. crouch-walk for the transition, the *decision* to switch is made first; the transition style is chosen from the (start posture, end posture, distance, urgency) tuple after the fact. This matters for your future expansion — don't bake animation choice into the switch trigger.

---

## Per-Game Notes

### The Last of Us (Naughty Dog, 2013) — Ellie

Primary source: Max Dyckhoff, **"Ellie: Buddy AI in The Last of Us"**, GDC 2014. Talk page on GDC Vault: https://www.gdcvault.com/play/1020364/Ellie-Buddy-AI-in-The. Coverage at https://www.gamedeveloper.com/design/ellie-s-buddy-ai-in-i-the-last-of-us-i-explained-at-gdc-2014. Sibling Naughty Dog talk: **"The Last of Us: Human Enemy AI"** (Travis McIntosh and Mark Botta), https://gdcvault.com/play/1020338/The-Last-of-Us-Human.

**Switch triggers.** Ellie's cover decisions are dominated by *Joel's* position. She is anchored to him with a follow-utility, and her cover candidates are filtered to a region behind him relative to threats. When Joel moves out of that region, her current slot is invalidated and she re-picks — not because her own slot is tactically bad, but because the player has redefined what "behind cover near the player" means. Secondary triggers are perception-driven: when the human-enemy AI's last-known threat position updates, all buddies (Ellie, Tess, Bill, Sam) re-evaluate. Tertiary: the slot has direct LoS to a threat for longer than ~0.5 s.

**Slot picker.** Filter first, score second. Filter: behind cover relative to known threats, within player-proximity radius, not occupied by another NPC. Score among survivors: distance to player ascending, distance to threats descending, slight tie-break for "doesn't break the player's fire lane." The team specifically wanted Ellie to *not* run for the geometrically best cover on the other side of the room — proximity to Joel is the dominant term.

**Transition handling.** Standard MoveTo, with the "don't move while Joel is mid-reposition" interlock. Ellie waits until Joel stops or reaches his own cover before she commits to her own switch, to avoid the visible failure mode of "she dashes to where I just was." This single rule fixes a huge class of bad-feeling switches.

**Annoyance prevention.** Dyckhoff highlighted three explicit failure modes the team tracked: (a) Ellie crossing the player's fire lane, addressed by penalising candidate slots that lie within Joel's view cone, (b) Ellie running across open ground looking for cover, addressed by the "follow close" no-cover fallback within ~2 m of the player, and (c) Ellie repeatedly switching between two near-equal slots, addressed by a minimum dwell time at the chosen slot before re-evaluation is allowed. The talk also flags that Ellie was deliberately given limited combat utility — she calls out threats, throws bricks, occasionally fires — because every additional combat behaviour adds another opportunity for her to look stupid.

### The Last of Us Part II (Naughty Dog, 2020) — Dina, Jesse, Lev

No public GDC talk on Part II's buddy stack was given as comprehensively as Dyckhoff 2014, but the system is widely understood to be an evolution. The observable shift is that Part II buddies more actively re-flank when the player has been pinned, suggesting a player-distress signal feeding into the cover picker. The fundamentals — player-anchored filtering, perception-driven re-evaluation, dwell hysteresis — are unchanged.

### Uncharted 4 (Naughty Dog, 2016) — Sully, Sam, Elena

Primary source: 80.lv, **"Behind the Scenes: AI of Uncharted 4"**, https://80.lv/articles/behind-the-scenes-ai-of-uncharted-4. This article describes the **"Buddy Cover Share"** system explicitly.

**Switch trigger.** Velocity-projected player position. The system extrapolates where the player will be in N frames based on current velocity; if that projected position lands inside a rectangular footprint around a buddy's current cover slot, the buddy aborts current in-cover behaviour and hunkers, freeing the cover wall for the player. This is a *predictive* trigger — the buddy moves before the collision actually happens.

**Slot picker.** Once forced off a slot, buddies re-query the standard slot set around the player, with the just-vacated slot excluded for a short cooldown so the buddy doesn't snap back as soon as the player passes.

**Transition handling.** Hunker against the wall is the default cheap transition (no full slot re-acquisition); only when the player commits to a stretch of cover does the buddy actually relocate to a different slot. This two-stage response — yield first, relocate later — is what makes the system feel polite rather than panicked.

**Annoyance prevention.** The article emphasises that an earlier prototype tried "Nate reaches out and pulls the buddy aside" but it broke movement flow. The velocity-projection switch is the *fluid* alternative.

### Bioshock Infinite (Irrational, 2013) — Elizabeth

Primary source: John Abercrombie, **"Bringing BioShock Infinite's Elizabeth to Life: An AI Development Postmortem"**, GDC 2014. https://www.gdcvault.com/play/1020831/Bringing-BioShock-Infinite-s-Elizabeth. Companion piece: Gamasutra video coverage, https://www.gamedeveloper.com/design/video-how-irrational-gave-life-to-i-bioshock-infinite-i-s-elizabeth. Long-form designer essay: **"Thinking About Elizabeth: Part 1"** by Drew Holmes, https://www.gamedeveloper.com/design/thinking-about-elizabeth-part-1.

**Switch trigger.** Elizabeth doesn't take cover in the conventional sense — she takes *vantage*. Irrational's "soccer-defender" framing has her position herself such that Booker is always visible on screen between her and his apparent destination. Her switching is driven by: Booker's facing direction changing, Booker firing a sustained burst at a new target, or her current vantage losing LoS to either Booker or his current target. The throwable-item mechanic (Booker-Catch) layers on top — she switches to be in throwing range of Booker when his ammo or salts drop below threshold.

**Slot picker.** Vantage points are level-authored, with explicit roles (high ground, throw position, hide-here-during-Songbird). The picker is a utility scorer over the authored set, biased toward staying on-screen for the player. The "theatre blocking" framing in the talk is design-speak for: positions are picked for *visibility to the player*, not tactical optimum.

**Annoyance prevention.** Two big ones. First, Elizabeth is invulnerable to enemy fire — this removes the entire class of "she switched into a worse spot and died" problems. Second, the team added a spotlight on her when she's about to throw something, because internal playtests showed players didn't notice her calling out enemies. The lesson for cover switching: a successful switch should be *legible* to the player, not silent.

### F.E.A.R. (Monolith, 2005) — Squad AI

Primary source: Jeff Orkin, **"Three States and a Plan: The AI of F.E.A.R."**, GDC 2006. https://gdcvault.com/play/1013282/Three-States-and-a-Plan. Free archive: https://archive.org/details/GDC2006Orkin. Modern walkthrough: Tommy Thompson, **"Building the AI of F.E.A.R. with Goal Oriented Action Planning"**, https://www.gamedeveloper.com/design/building-the-ai-of-f-e-a-r-with-goal.

**Switch trigger.** Replanning in the GOAP planner is the switch. When the current cover node fails the "safe relative to threat" predicate — because the threat moved, or LoS opened — the goal state "InCover AND ThreatInRange" becomes unsatisfied and the planner picks a new action sequence, which may include `GoToCover(node)` for a different node. There is no separate "cover-switching subsystem"; switching is an emergent consequence of replanning.

**Slot picker.** The planner enumerates available cover nodes, scores by distance and per-node trace against the threat, and picks the cheapest plan. Expensive at scale, which is why F.E.A.R. ran with small encounters.

**Annoyance prevention.** F.E.A.R.'s squad AI famously used **combat dialogue as an excuse for delay** — the "covering!" / "flanking right!" callouts are not just flavour, they buy the planner time to actually move. The illusion of intent is created by audio at the moment of decision, masking the latency of locomotion. Source: Jeff Orkin, **"Combat Dialogue in F.E.A.R.: The Illusion of Communication"**, Game AI Pro 2, free PDF at https://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter02_Combat_Dialogue_in_FEAR_The_Illusion_of_Communication.pdf.

### Halo (Bungie / 343i)

Primary source: Damian Isla, **"Managing Complexity in the Halo 2 AI System"**, GDC 2005. https://www.gdcvault.com/play/1020270/Managing-Complexity-in-the-Halo. Free archive: https://archive.org/details/GDC2005Isla. Public summary: https://www.gamedeveloper.com/programming/gdc-2005-proceeding-handling-complexity-in-the-i-halo-2-i-ai. Encounter-design follow-up: **"Combat Evolved: The Encounter Design of Halo 3"**, https://www.gamedeveloper.com/design/combat-evolved-the-encounter-design-of-halo-3.

**Switch trigger.** Halo organises space into **areas** (designer-tagged groupings of firing positions) and assigns **squads** to areas via behaviour-tree orders. A squad member switches firing position within its current area when: an enemy enters the area's exclusion zone, the current position has been suppressed for N seconds, the squad's overall objective changes (push, fall back, flank), or another squad member is more usefully placed at the current position.

**Slot picker.** Within an area, picking is a cheap distance + LoS + claim check. Critically the *area* is chosen by the squad leader / encounter logic — individual cover-switching is local. Marines (your friendlies) inherit the same picker but with a strong follow-leader bias.

**Annoyance prevention.** The "area" abstraction is itself the anti-annoyance trick: a Marine never picks a slot outside its assigned area, so it can't run halfway across the map looking for cover. Squad-claim arbitration prevents two Marines fighting over the same slot.

### Gears of War (Epic Games) — Dom, Baird, Cole

See `cover_research_industry.md` for the slot-authoring details. Notable for *switching* specifically: Gears 2's AI postmortem (Matt Tonks, GDC 2009, https://www.gdcvault.com/play/1463) describes a **~0.75 s minimum dwell time** at any newly entered slot and an explicit "switch on threat-update only" policy for stationary engagements. Dom specifically has a tight player-proximity tether — if Marcus moves more than the tether allows, Dom abandons cover unconditionally and re-pathfinds, which is the same pattern as Ellie.

### Killzone 2/3 (Guerrilla Games)

Primary source: Straatman et al., **"Hierarchical AI for Multiplayer Bots in Killzone 3"**, Game AI Pro Vol 1 Ch 29, free PDF at http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter29_Hierarchical_AI_for_Multiplayer_Bots_in_Killzone_3.pdf. Tools-side: **"Out of Sight, Out of Mind: Improving Visualization of AI Info"**, https://www.guerrilla-games.com/read/out-of-sight-out-of-mind-improving-visualization-of-ai-info.

**Switch trigger.** Two tiers: the **Team Brain** can reassign tactical roles (flanker, suppressor, hold) at the squad level, which forces affected bots to re-query for new cover appropriate to their new role; locally, a bot switches within its role when the current point is invalidated by perception or has been continuously fired upon. Periodic safety check at ~1.5 s.

**Slot picker.** Role-restricted set. A bot assigned "suppressor" only queries suppressor-tagged points. This dramatically narrows the candidate set and removes the temptation to switch into a tactically wrong slot just because it scores well on distance.

**Annoyance prevention.** Role-restriction + perception-driven invalidation + 1.5 s timer fallback. The Team Brain only re-issues role assignments when the macro situation changes (squad reduced below threshold, objective contested), so role-driven switches are infrequent and feel deliberate.

### Resident Evil 5 (Capcom) — Sheva

Primary source: community/wiki documentation and Capcom's "Partner Action" design (https://residentevil.fandom.com/wiki/Partner_Action_Button). No public dev talk on cover specifically.

**Switch trigger.** Player command. Sheva has two macro modes — Attack and Cover — toggled by the player via the D-pad. In Cover mode she stays near Chris, uses her weakest weapon, and is less reckless; in Attack mode she pushes forward to scavenge and engage. There is no autonomous tactical cover-switching at the Halo / Killzone level; switching of *position* is a follow-anchor relative to Chris.

**Annoyance prevention.** Putting macro mode under explicit player control sidesteps the entire autonomous-switching problem — but at the cost of asking the player to micromanage. For ProjectExtract this is interesting as an emergency-off switch (a "hold position" command) more than as a design model.

### Resident Evil 4 Remake (Capcom, 2023) — Ashley

Sources: https://gamerant.com/resident-evil-4-remake-ashley-graham-ai-usefulness-last-of-us-ellie-god-war-atreus/, https://stevivor.com/news/resident-evil-4-remake-changes-ashleys-escort-missions/. Ashley is escort-style, not combat-cover-style. She hides on command, downs and is revived (like a teammate DBNO state) rather than using cover slots tactically. Notable design choice: removing her health bar made her *feel* less like a babysit-meter even though she still takes damage — a UX lesson about legibility of companion state.

### Mass Effect 2/3 (BioWare)

No public talk on cover specifically. Community consensus (https://steamcommunity.com/app/17460/discussions/0/1870623436621105068/, https://fextralife.com/forums/t242000/on-squad-ai-and-mass-effect-2) reports squadmates take cover autonomously in ME2 but "forget" assigned cover between waves and when Shepard moves too far. The leave-cover-when-player-moves-too-far rule confirms the player-tether pattern is universal; the "forget between waves" bug is a useful negative datapoint — switching state must persist across combat lulls, not reset.

### The Division 2 (Ubisoft) — Recruitable NPC Companions

Sources: https://www.ubisoft.com/en-gb/game/the-division/the-division-2/news-updates/5fLsSWyLeFsYWiBxlByY4K/the-division-2-mutiny, https://www.mmorpg.com/news/the-division-2-brings-recruitable-npc-companions-into-the-fray-2000136748.

**Switch trigger.** Companions have archetypes — Assault, Medic, Engineer — and switching behaviour follows the archetype. Assault is aggressive cover-pushing, Medic stays near the player, Engineer holds back near deployables. Player can issue Command Link orders that override (reposition, focus target, support teammate).

**Slot picker.** Archetype-restricted, similar in spirit to Killzone's role-tagged points.

**Annoyance prevention.** Explicit archetype means the player builds an expectation for the companion's behaviour, and switching that violates the archetype reads as a bug. For ProjectExtract, this argues for the companion having a single legible "style" rather than context-shifting between styles mid-encounter.

### Watch Dogs Legion (Ubisoft Toronto, 2020)

Primary source: **"Branching Out: Watch Dogs Legion's Architecture for Group AI Behaviours"**, GDC 2020 AI Summit, https://gdcvault.com/play/1027239. Sibling earlier talk: **"Hacking into the Combat AI of Watch Dogs 2"** (Chae Dickie-Clark), https://www.gdcvault.com/play/1024209/Hacking-into-the-Combat-AI.

Legion's relevance to companion cover-switching is limited — operatives aren't traditional buddies, and combat is rarely cover-centric. But the **aLiVE Group Behaviour System** is the documented descendant of Watch Dogs 2's cover picker, with cover scoring done via per-NPC utility considerations. Worth knowing if you ever generalise the companion's cover stack into something multi-NPC.

---

## Common Pitfalls — What Makes Companion Cover Switching Feel Bad

These are recurring failure modes called out in talks, postmortems, and community feedback. Listed in rough order of how often they sink a buddy AI prototype.

- **Oscillating between two near-equal slots.** Two candidates score within a hair of each other, and on every re-evaluation the winner flips. Fix: minimum dwell time at the chosen slot, plus a "must beat current by margin X" gate on the picker.
- **Switching mid-burst.** The companion fires three rounds then sprints to a different slot — looks like the AI gave up. Fix: lock cover during active firing windows; queue the switch for after the burst ends.
- **Running across the player's fire lane.** Companion picks an excellent slot on the opposite side of the player's current view cone and crosses the line of fire to get there. Fix: penalise candidates that lie in the player's view cone for the next N frames (use the player's facing, not just position).
- **Picking cover on the wrong side of the room.** The best-scoring slot tactically is 30 m away from the player; companion runs over there and is now useless. Fix: hard cap on player distance; rank within that cap.
- **Crossing the threat's LoS during the move.** Switch trajectory passes through open ground exposed to the threat that motivated the switch in the first place. Fix: pathfinding penalty for path segments with threat LoS, or a "stay-low" transition style when the threat is active.
- **Switching the moment the player leaves.** Player walks past, companion abandons cover to follow, then immediately picks a new slot two metres away when the player stops. Fix: short post-player-move cooldown before the picker is allowed to commit to a new slot (Uncharted-style yield-first-relocate-later).
- **Switching to cover that's about to be invalid.** Companion runs to a slot the player is about to walk past or fire over. Fix: factor projected player position into the picker (Uncharted's velocity projection).
- **Snapping back into the just-vacated slot.** Companion yields to the player, then the player moves on, and the companion teleport-snaps back. Fix: cooldown on the just-vacated slot, force a path move not a hunker-back.
- **Switching when the player is themselves switching.** Both reposition simultaneously, creating chaos. Fix: Ellie's interlock — wait until the player is settled before committing.
- **No legibility.** The switch happens silently and the player doesn't notice the companion has moved, then the player walks into a position the companion is occupying. Fix: a quiet audio cue or animation tell at the start of a switch (the F.E.A.R. callouts and the Elizabeth spotlight do this work).
- **Switching that breaks the player's mental model of where the companion is.** Player's tracking the companion peripherally, companion switches, player turns and panics because the companion has "disappeared." Fix: keep switches inside the player's general spatial awareness — proximity-capped picker handles most of this.
- **Stuck in cover after combat ends.** The "forget between waves" issue from ME2 — cover state doesn't clear when there are no threats. Fix: exit-cover transition driven by perception going clear, not by an explicit timer.
- **Switching too often even with hysteresis tuned.** Often a symptom of the underlying validity check being too noisy (intermittent LoS due to particle effects, swaying foliage, brief enemy occlusion). Fix: smooth the validity signal with a 0.3–0.5 s rolling window before letting it flip the switch decision.
- **Animation choice driven by switch decision instead of separately.** Locks the team into picking a slide animation at decision-time, then committing to it even when the situation has changed by the time the move starts. Fix: pick animation at the moment movement begins, from `(start posture, end posture, distance, urgency, threat exposure on path)`.

---

## Applied Recommendations for ProjectExtract

The companion already enters cover via the existing `UAICoverSlot` system. For switching specifically:

1. **Player position is the primary switch trigger.** When the player moves outside an anchor radius (or projected position will leave it within N frames), the companion's current slot is invalidated and the picker runs. Ellie's pattern; matches the existing follow-anchored design.
2. **Threat-perception event is the secondary trigger.** When the AI perception component fires a threat-position update for the slot's relevant target, re-evaluate. No periodic re-check while the threat hasn't moved (Halo / Killzone pattern).
3. **Fallback timer at ~1.5–2 s, only while engaged.** Catches missed events. Do not run the timer while moving to cover or while in non-combat.
4. **Re-use the existing slot picker with switch-specific filters.** Exclude the current slot; tighten the player-proximity cap by ~25% relative to initial-entry; require the new slot to beat the current slot's score by a margin (not just equal it). This is the hysteresis gate.
5. **Minimum dwell of ~1.0 s at every slot.** Including the initial entry. No re-evaluation allowed inside the dwell window.
6. **Don't switch while firing.** Interlock against the firing state machine; queue the switch for end-of-burst. Hard cancel only on cover-destroyed or melee-threat-in-range.
7. **Player-fire-lane penalty.** Penalise candidate slots that lie within the player's view cone for the next half-second of projected facing — keeps the companion out of the player's shots without making it cling to bad slots.
8. **Yield-first, relocate-later when the player approaches the companion's slot.** Uncharted pattern. Cheap hunker-against-wall first; full slot re-acquisition only if the player commits to the slot.
9. **Quiet legibility tell at switch start.** A single VO line or a footstep audio cue at decision-time, before the move begins. Cheap, prevents the "where did the companion go" disorientation.
10. **Animation selection deferred to move-start, picked from the (start posture, end posture, distance, urgency) tuple.** Don't bake it into the switch decision; this is the path the user described for future expansion (stand→crouch = slide, crouch→stand = crouch-sprint), and decoupling it now means the switching system doesn't need to change later.

---

## Source Index

| Source | Type | URL |
|--------|------|-----|
| Max Dyckhoff, "Ellie: Buddy AI in The Last of Us", GDC 2014 | GDC talk | https://www.gdcvault.com/play/1020364/Ellie-Buddy-AI-in-The |
| Coverage of Dyckhoff GDC 2014 | Press summary | https://www.gamedeveloper.com/design/ellie-s-buddy-ai-in-i-the-last-of-us-i-explained-at-gdc-2014 |
| Travis McIntosh & Mark Botta, "The Last of Us: Human Enemy AI", GDC 2014 | GDC talk | https://gdcvault.com/play/1020338/The-Last-of-Us-Human |
| 80.lv, "Behind the Scenes: AI of Uncharted 4" (Buddy Cover Share) | Article | https://80.lv/articles/behind-the-scenes-ai-of-uncharted-4 |
| John Abercrombie, "Bringing BioShock Infinite's Elizabeth to Life", GDC 2014 | GDC talk | https://www.gdcvault.com/play/1020831/Bringing-BioShock-Infinite-s-Elizabeth |
| Gamasutra video on Elizabeth | Video / article | https://www.gamedeveloper.com/design/video-how-irrational-gave-life-to-i-bioshock-infinite-i-s-elizabeth |
| Drew Holmes, "Thinking About Elizabeth: Part 1" | Designer essay | https://www.gamedeveloper.com/design/thinking-about-elizabeth-part-1 |
| Jeff Orkin, "Three States and a Plan: The AI of F.E.A.R.", GDC 2006 | GDC talk | https://gdcvault.com/play/1013282/Three-States-and-a-Plan |
| Orkin GDC 2006 archive | Free video | https://archive.org/details/GDC2006Orkin |
| Tommy Thompson, "Building the AI of F.E.A.R. with GOAP" | Article | https://www.gamedeveloper.com/design/building-the-ai-of-f-e-a-r-with-goal |
| Jeff Orkin, "Combat Dialogue in F.E.A.R.", Game AI Pro 2 Ch 2 | Book chapter (free PDF) | https://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter02_Combat_Dialogue_in_FEAR_The_Illusion_of_Communication.pdf |
| Damian Isla, "Managing Complexity in the Halo 2 AI System", GDC 2005 | GDC talk | https://www.gdcvault.com/play/1020270/Managing-Complexity-in-the-Halo |
| Isla GDC 2005 archive | Free video | https://archive.org/details/GDC2005Isla |
| Coverage of Isla GDC 2005 | Press summary | https://www.gamedeveloper.com/programming/gdc-2005-proceeding-handling-complexity-in-the-i-halo-2-i-ai |
| "Combat Evolved: The Encounter Design of Halo 3" | Article | https://www.gamedeveloper.com/design/combat-evolved-the-encounter-design-of-halo-3 |
| Matt Tonks, "2008 AI Postmortems: SPORE, Gears 2, BioShock", GDC 2009 | GDC talk (free) | https://www.gdcvault.com/play/1463 |
| Straatman et al., "Hierarchical AI for MP Bots in Killzone 3", Game AI Pro Vol 1 Ch 29 | Book chapter (free PDF) | http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter29_Hierarchical_AI_for_Multiplayer_Bots_in_Killzone_3.pdf |
| Guerrilla, "Out of Sight, Out of Mind: Improving Visualization of AI Info" | Article | https://www.guerrilla-games.com/read/out-of-sight-out-of-mind-improving-visualization-of-ai-info |
| Capcom RE Wiki, "Partner Action Button" (Sheva) | Wiki | https://residentevil.fandom.com/wiki/Partner_Action_Button |
| Game Rant, RE4 Remake Ashley AI commentary | Article | https://gamerant.com/resident-evil-4-remake-ashley-graham-ai-usefulness-last-of-us-ellie-god-war-atreus/ |
| Stevivor, RE4 Remake escort changes | Article | https://stevivor.com/news/resident-evil-4-remake-changes-ashleys-escort-missions/ |
| Mass Effect 2 squad AI community discussion | Forum | https://steamcommunity.com/app/17460/discussions/0/1870623436621105068/ |
| Mass Effect 2 squad AI design discussion | Forum | https://fextralife.com/forums/t242000/on-squad-ai-and-mass-effect-2 |
| Ubisoft, "The Division 2: Mutiny" companions announcement | Press | https://www.ubisoft.com/en-gb/game/the-division/the-division-2/news-updates/5fLsSWyLeFsYWiBxlByY4K/the-division-2-mutiny |
| MMORPG.com, Division 2 NPC Companions overview | Article | https://www.mmorpg.com/news/the-division-2-brings-recruitable-npc-companions-into-the-fray-2000136748 |
| "Branching Out: Watch Dogs Legion's Architecture for Group AI Behaviours", GDC 2020 | GDC talk | https://gdcvault.com/play/1027239 |
| Chae Dickie-Clark, "Hacking into the Combat AI of Watch Dogs 2", GDC 2017 | GDC talk | https://www.gdcvault.com/play/1024209/Hacking-into-the-Combat-AI |
| Game AI Pro book index (all volumes free PDFs) | Reference | http://www.gameaipro.com/ |

*GDC Vault links marked as members-only require a subscription; archive.org and Game AI Pro mirrors are free where listed. Where exact technical detail could not be verified from a publicly accessible recording, claims are attributed to the session description, press coverage, or named secondary source.*
