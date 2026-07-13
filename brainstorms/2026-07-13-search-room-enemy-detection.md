# Search-room enemy detection: Brainstorm / Discovery Notes
Date: 2026-07-13 · Goal: decide how enemies should react to the companion during a commanded door-search, so a door-watching enemy and the companion don't stand in a mutual blind spot

## Context (pre-session)
- Enemies do not detect the companion at all (by design — only the player is a perception target).
- The companion CAN engage enemies, but unaware enemies need the engagement grant (SetPostBreachEngagement) before they're valid targets outside normal combat flow.
- New door-search flow: ping door → breach entry → room check (LoS-gated) → engage/loot/dwell → follow.
- Failure case: enemy in the room staring at the door. Companion breaches in. Enemy never reacts (can't see companion); companion... (grant makes enemy engageable, so companion should fire — verify). Net feel: enemy ignores a door getting kicked in, in its face.
- My prior suggestion on record: mode-dependent mutual visibility via the existing breach-noise hook (Loud/Tactical make companion detectable briefly; Quiet stays ghost).

## Summary / key decisions (final)
**Feature: "search exposure" — a commanded door entry lifts the companion's enemy-side cloak for the room it's entering.**
1. **Feel:** real two-way firefight — the room enemy detects the companion as a genuine combat target and fights with full normal behavior (Q1).
2. **All modes expose, including Stealth** — ordering the companion into a watched room carries the risk (Q2). Companion-side quiet contract unchanged: a Stealth search never initiates (no grant/auto-loot), it only fights back if caught (Q7); being seen = full stealth break, normal re-pin rules (Q5).
3. **Scope:** visual detection for enemies in range of the room anchor WITH LoS to the companion — includes a corridor enemy staring through the open doorway (Q3, Q9). Everyone else stays visually blind but hears the fight through the normal noise/alert channels, range-gated (Q3; companion gunfire already emits WeaponFire noise).
4. **Window:** door-swing (or entry start on an open door) → end of the companion's business in that room, INCLUDING a chained same-room loot (Q4, Q4b). Fight starting inside the window hands over to normal combat rules (combat entry already lifts the Normal cloak permanently for that enemy).
5. **Reaction speed:** normal suspicion ramp + a strong startle bump from the existing door-swing noise event — a staring watcher escalates in well under a second (Q6).
6. **Ordered Breach command gets the same exposure** (Q8), running through the whole WaitingForPlayer hold — breach behavior itself unchanged (empty room: hold as today, then return to follow), exposure ends when the command ends (Q8b).

**Implementation anchor points found in code:** `UEnemyAwarenessComponent::IsCompanionSightCloaked` / `CanSelectCompanionTarget` (the stealth hard-block at L977-982 must also honor the exposure), `IsCompanionFireInaudible`, breach-noise event in `CompanionBreachStatics::OpenBreachDoor`, room set ≈ anchor+radius+LoS as used by the search task's `ExploreViewerHasLoS`. Likely shape: a time/command-boxed exposure state on `ACompanionCharacter` (anchor + radius), set by the search/breach tasks, checked inside the cloak functions.

## Q&A log

### Q1 — target feel
- Asked: when the companion breaches into a watched room, what should happen?
- Captured: **Real two-way firefight.** Enemy detects the companion as a genuine combat target for the entry window — both shoot at each other; enemy uses its normal combat behavior (cover, callouts, Director alert). Not scripted, not damage-only.

### Q2 — mode dependence
- Asked: does companion mode gate enemy-side detection during search entry?
- Captured: **All modes detectable — including Stealth/Quiet.** "You ordered it into a watched room, it takes the risk." The entry window overrides the stealth ghost contract for enemy-side detection. (Note: companion-side quiet behavior — no grant, no auto-loot — is a separate lever; only the DETECTABILITY is universal. Resolved in Q7: quiet entry still never initiates.)

### Q3 — detection scope
- Asked: which enemies can see the companion during the window?
- Captured: **Visual detection = only enemies inside the searched room** (the LoS-gated room set). Outside enemies do NOT get vision of the companion. BUT — the resulting firefight must not be silent to the outside world: **enemies outside the room, if in range, should notice the fight** (gunfire noise → investigate/alert via the normal channels), not ignore a gunfight next door. Visual = room-scoped; audio/alert propagation = normal rules, range-gated.
- Flags: verify whether companion gunfire already reports an AISense_Hearing noise event / Director alert that enemy perception consumes — if not, that's part of this feature's work → me (code check).

### Code findings (checked during session)
- Companion weapon fire ALREADY reports `WeaponFire` hearing noise (`WeaponBase.cpp:869`) — Q3's "outside enemies hear the fight" is mostly existing plumbing.
- Enemy-side "companion cloak" already exists: `UEnemyAwarenessComponent::IsCompanionSightCloaked` (`EnemyAwarenessComponent.cpp:1344-1368`):
  - Combat mode → NOT cloaked (fully perceivable always) → the Q1 firefight already works today in Combat mode.
  - Stealth active → hard-invisible (and `CanSelectCompanionTarget` L977-982 blocks stealth companion as combat target outright — the exposure window must override BOTH).
  - Normal → cloaked until THIS enemy is in Combat state or global alert = Loud.
- `IsCompanionFireInaudible` (L1370-81): stealth fire audible unless suppressed; Normal pre-fight cloak silences fire.
- Feature shape implied: a time/command-boxed "search exposure" on the companion that IsCompanionSightCloaked (and the stealth target-block) treats as cloak-lifted, scoped per-enemy to the searched room (anchor + radius + LoS ≈ the existing room set).

### Q4 — window bounds
- Asked: when does the exposure start/end?
- Captured: **Door-swing → search-command end.** Starts at the swing (or entry start on an already-open door), covers entry + room check + dwell, ends when the search command completes (incl. the chained loot? → see flag). A fight starting inside the window hands over to normal combat rules (combat entry lifts the Normal cloak permanently for that enemy anyway).
- Flags: (resolved by Q4b below)

### Q4b — loot chain
- Asked: does exposure carry through the chained Loot command?
- Captured: **Yes — one continuous exposure from door-swing until the companion is done in that room** (search + chained same-room loot). An enemy waking mid-loot still reacts.

### Q5 — stealth consequences
- Asked: stealth-active companion seen by a room enemy during the window — what happens?
- Captured: **Stealth breaks fully.** Being seen during the exposure = a normal stealth break (bStealthBroken path); broken stealth already falls through to fight-on rules, and re-pin happens per the existing stealth re-pin rules. No re-cloak-mid-fight weirdness.

### Q6 — reaction speed
- Asked: instant lock vs normal detection ramp for the door-watcher?
- Captured: **Normal suspicion ramp + a startle bump from the door-swing.** The existing breach-noise event at the doorway should give a strong suspicion bump so a staring watcher escalates to combat in well under a second; a distracted enemy takes a beat. No special instant-combat path.

### Q7 — companion side in Stealth searches
- Asked: does a stealth search pre-emptively engage room enemies now that it's exposed?
- Captured: **Only fights back.** Quiet contract kept companion-side: no grant, no initiated fire, quiet dwell/loot skip as today. Exposure only means it CAN be caught — if seen, stealth breaks (Q5) and it defends itself via the fight-on rules.

### Q9 — room-edge definition
- Asked: does a corridor enemy with a clear sightline through the open doorway count as a "room enemy"?
- Captured: **Yes — include doorway watchers.** The test is range-from-anchor + LoS to the companion; anyone who can literally see the entry reacts. No inside/outside geometry test needed.

### Q10 — completeness backstop
- Asked: anything else in your notes?
- Captured: no — session complete.

### Q8 — ordered-breach parity
- Asked: does the ordered Breach command get the same exposure?
- Captured: **Yes — both commanded door entries expose.** Same window shape: swing → command end. Consistent rule, no asymmetry.

### Q8b — ordered-breach window end (flag resolved post-session)
- Asked: does exposure extend through the WaitingForPlayer hold?
- Captured: **Yes.** "If it breaches and there's no enemy, no loot, then it should hold like it does, then return to follow" — behavior unchanged (hold for the player as today), and the exposure runs through that whole hold, ending when the command ends and it returns to follow.

## Open flags (pending input)
- (none — all resolved)
