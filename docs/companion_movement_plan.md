# AI Companion — Movement and Next Steps

By Matthew Lowe

---

## Purpose

The AI companion prototype is now functional end-to-end: it follows the player, engages enemies, and revives the player when downed. This document outlines the prototype's current state, the issues that have surfaced through development and informal playtesting, and three directions the next phase of work could take. The aim is to discuss the direction before I commit to a detailed build plan.

---

## 1. Where the Companion Stands

### 1.1 What it does

- **Formation follow.** The companion holds a configurable offset behind and to the right of the player, predicts where the player is heading from their velocity, and sprints to catch up if it falls behind a threshold distance. It has idle hysteresis so it does not jitter when the player rotates in place.
- **Combat engagement.** It detects enemies via sight and hearing, picks the closest, fires in short bursts with inaccuracy that settles the longer it stays on the same target — giving a believable ramp-on-target feel rather than instant headshots. It reloads when its magazine empties and resumes.
- **Revive.** When the player goes down, the companion drops what it is doing, sprints to the player, holds proximity for four seconds, and revives. The task handles awkward edge cases — revived by someone else mid-task, bleedout expiry, path blocked, player moves out of range.
- **Shared traversal.** Vault, climb, and mantle logic now lives in a shared component used by both the player and the companion. When the player traverses an obstacle, the companion mirrors the traversal on the same wall.
- **Designer tuning.** Most knobs (formation distances, sprint threshold, mirror trigger range, recovery timeouts) live in a Data Asset rather than C++, so changes do not require a rebuild.

### 1.2 Recent progress

- Traversal logic now lives in a shared component, so any future improvement to player traversal automatically benefits the companion.
- The sprint-state latching bug — where the companion would catch up but stay stuck in sprint animation forever — is fixed, along with the construction-order trap that caused it.
- Weapon fire now traces from muzzle to target instead of along the companion's forward vector, eliminating misses caused by height differences between the companion and its target.
- A robust revive task with complete edge-case handling has landed.

### 1.3 Outstanding issues, grouped by theme

The remaining problems sit cleanly in three themes, which map directly to the three directions in §2.

**Theme A — Movement and traversal fidelity.** Mirror traversal still fails at certain wall positions, likely a wall-edge / collision-shape interaction. The companion approaches obstacles at an angle rather than head-on, so traces against the wall start from suboptimal positions. Locomotion animation is forward-only and tightly coupled to specific speed values, so tweaking the speeds breaks the visuals. There is no drop-down behaviour for getting off ledges.

**Theme B — Tactical depth.** The companion has a single combat pattern: get in range and shoot. There is no suppressive fire, no flanking, and no fall-back when low on health. Follow behaviour does not respect threat — the companion will pull out of position to maintain formation while the player is being shot at. There is no architectural slot for a second companion or any form of squad coordination.

**Theme C — Player agency and robustness.** The companion is fully autonomous — the player cannot direct it, hold it back, or point it at a specific target. There are no automation tests; the only safety net is a written set of manual test scenarios. No performance profiling pass has been done.

---

## 2. Three Directions for the Next Phase

### Direction A — Stabilise and Polish

**Frame.** Focus on stabilising the existing systems before extending them. The traversal and animation issues will continue to surface in any feature work layered on top, and become more expensive to debug once buried under additional behaviour.

**Concrete work:**

- Fix the remaining traversal clearance failures. A one-line approach-angle fix is already identified, and a focused debug session with existing trace visualisation should resolve the rest.
- Audit the companion's collision shape against the player's to rule out a size mismatch as the source of the wall-edge failures.
- Add strafe and back-step locomotion poses; decouple the animation blendspace from raw speed values so future tuning does not break visuals.
- A first pass of automation tests, mirroring the manual scenarios that already exist in writing.

**What it offers:** Confidence the companion behaves consistently across machines and after refactors. Removes the class of "works in editor, fails in playtest" surprises that erode trust in the prototype.

**What it does not offer:** Any new gameplay surface. The companion will appear the same to anyone watching — there are simply fewer bugs, not new features.

### Direction B — Behavioural Depth

**Frame.** Extend the companion's combat behaviour beyond a single engagement pattern. Today the companion is a competent autonomous actor; the next step is making its decisions visibly tactical.

**Concrete work:**

- Multiple engagement patterns — suppress, flank, push — chosen based on enemy count and the layout of the environment.
- Threat-aware follow: drop into a combat posture and prioritise positioning when the player is taking fire, even between engagements.
- Architecture pass for a second companion, including role assignment and arbitration over who responds when both companions could act (for example, who revives the player).

**What it offers:** The companion becomes a more distinct design element. A player can tell the difference between "follower with a gun" and "AI that makes tactical choices" — the second is much more memorable.

**What it does not offer:** Any improvement to the underlying movement, traversal, or animation issues. A more tactical companion that still trips over walls reads worse than a simpler companion that moves cleanly.

### Direction C — Player Agency (Command System)

**Frame.** Let the player drive the companion rather than the companion guessing. A control surface turns the companion from a passive sidekick into a resource the player engages with deliberately.

**Concrete work:**

- A command ping — hold a key, point at the world, issue an order: "go here", "hold this position", "focus that target", "revive me".
- Internal state values for player intent; the companion's behaviour tree gains a top-level branch that respects commands and overrides the autonomous logic.
- A minimal UI affordance, such as an indicator on the world and a command icon on the heads-up display.
- A clear hand-back rule — when an order completes or is overridden by a higher-priority event (player goes down), the companion returns to autonomous mode.

**What it offers:** The largest gameplay-loop change of the three. The companion becomes something the player commands, not just something that exists alongside them — a change in how the game is *played*, not just how it *runs*.

**What it does not offer:** Any improvement to the underlying behaviour quality. A weak follower with commands is still a weak follower; the command system rides on top of the existing AI rather than replacing it.

---

## 3. Recommendation and Open Questions

### 3.1 Recommended order

I would prioritise **Direction A first.** Without it, B and C are built on shifting sand. The traversal and locomotion issues will surface in any feature work — every new behaviour layered on top inherits them, and every new bug report needs to be triaged against the existing instability before the new feature itself can be addressed.

**Then Direction C.** A bigger perceived impact per hour of work than B. The command system also acts as a multiplier on B's later behaviours, because the player can see the companion respond to an order — the depth becomes visible rather than ambient.

**Direction B last.** Behavioural depth and squad-level coordination is the most expensive of the three and depends on a stable, controllable solo companion. Bringing it forward risks doubling down on systems that have not yet been validated.

### 3.2 Open questions

These are the points where guidance would most help the direction:

- **Visible feature progress this term, or invisible stability progress?** If a demoable feature is the priority for the next review point, Direction C ahead of A is defensible — the command system gives a clear talking point. If the priority is reliability ahead of a wider playtest, A is the right order.
- **Is a second companion or squad in scope for the project at all?** If yes, Direction B's architecture work should start earlier so the design does not commit to single-AI assumptions. If no, B drops down the list and Direction C absorbs more of the cycle.

### 3.3 Open risks

- **Traversal mirroring may need a deeper rework.** If the clearance failures turn out to be collision-shape-based rather than trace-based, Direction A's scope grows. This is a known unknown; the first focused debug session should give a confident answer.
- **Animation polish is gated on art availability.** Direction A's strafe and back-step poses depend on animation assets, not just on engineering time. If that resource is not available, the visual half of Direction A defers without blocking the rest.
- **The command system needs design input before code.** Direction C should not start with engineering — it should start with a brief design pass on what commands exist, how they are issued, and how the world communicates them back.
