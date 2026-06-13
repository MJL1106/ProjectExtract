# Companion Cover-Switching — Design Spec

ProjectExtract, 2026-05-26. Branch `AI-Companion-Prototype`.

Scope: the AI companion's *cover repositioning* behaviour — deciding when the current slot is no longer good enough and choosing a new one. Initial cover *entry* is already solved by `UBTTask_MoveToCover` + `UCoverRegistrySubsystem::FindBestCoverFor` and is out of scope here.

This doc has two parts. **Section 4** is the ideal full system — what we want eventually. **Section 5** is the prototype version we're building now, which is a strict subset.

Background research lives in `agent_docs/companion_cover_switch_research.md` (gitignored) — 28 sources surveyed across Naughty Dog, Bungie, Irrational, Guerrilla, Monolith, Epic, Capcom, BioWare, Ubisoft. Patterns referenced inline.

---

## 1. Goals

- Companion repositions cover legibly in response to the player moving, so the player can deliberately reproduce a switch by walking somewhere new.
- No oscillation between near-equal slots.
- No abandoning cover mid-burst-fire.
- Implementation is small, isolated, and additive — reuses the existing slot picker and BT shape.
- The system composes with the future expansion of *transition animations* (stand→crouch slide, crouch→stand crouch-sprint) without needing redesign.

## 2. Non-goals

- Cover-destruction handling. No destructibles in the build yet.
- Uncharted-style velocity-projected yield-first behaviour (player approaches companion's slot → companion hunkers before relocating). Premature for prototype.
- Multi-NPC squad arbitration. Only one companion exists.
- Initial cover entry — already solved.
- Replication. The decision logic runs server-only; movement is already replicated by the existing CharacterMovementComponent path.

## 3. Existing system context

The following are in place and will be reused without modification by the prototype.

| Component | Role | File |
|---|---|---|
| `AAICoverSlot` | Designer-placed cover actor. Exposes Stand/Crouch height, fire arc, peekable corners, continuous Alpha-along-line API. | `Public/AI/Cover/AICoverSlot.h` |
| `UCoverRegistrySubsystem` | World subsystem holding all registered slots. Provides `FindBestCoverFor(QuerierLoc, Target, MaxRadius)`. | `Public/AI/Cover/CoverRegistrySubsystem.h` |
| `UBTTask_MoveToCover` | BT task that runs the picker, claims a slot, and moves the companion to it. Clears `BB_HasCoverPosition` on abort. | `Public/AI/Tasks/BTTask_MoveToCover.h` |
| `UBTTask_CompanionCombat` | BT task that runs while the companion is in cover. Owns the firing/peeking state. | `Public/AI/Tasks/BTTask_CompanionCombat.h` |
| `UBTService_UpdateCompanionState` | BT service that keeps blackboard keys (combat target, has-cover, posture) in sync. | `Public/AI/BTS/BTService_UpdateCompanionState.h` |
| `UCompanionTuningDataAsset` | Designer-tunable values. Will gain three new fields for cover switching. | `Public/AI/CompanionTuningDataAsset.h` |
| `ACompanionAIController` | Owns blackboard keys including `BB_CoverSlot`, `BB_CoverLocation`, `BB_HasCoverPosition`. | `Public/AI/CompanionAIController.h` |

`FindBestCoverFor` already filters by claim state, search radius, fire arc, LoS to target, and rejects hide-only stand cover. It scores by proximity to querier, distance to target (sweet spot 500-1200 cm), and stand-fire-over preference. The prototype simply calls it again from a new tick site.

## 4. Ideal full design (target state, not all in prototype)

### 4.1 Invalidation triggers

The current slot becomes invalid (a re-pick is queued) when any of the following becomes true:

| # | Trigger | Notes |
|---|---|---|
| T1 | **Formation-distance.** Player's formation point (player position + `FormationOffsetBack/Right`) is more than `CoverSwitchInvalidationRadius` from the current slot. | Primary trigger. Ellie / Dom / Sheva pattern. |
| T2 | **LoS-to-target lost (sustained).** Current slot has had no LoS to the BB combat target for > `CoverSwitchLosLostWindow` seconds. | Secondary, gated on `BB_CombatTarget` being valid. Smoothed window prevents flicker from foliage / particles. |
| T3 | **Slot externally released.** Slot's claim was lost (destroyed, another AI took it, runtime fault). | Defensive fallback. |
| T4 | **Re-eval-tick.** Periodic safety net at `CoverSwitchReEvalInterval` (~1 s). Runs `FindBestCoverFor` from formation point; if best candidate beats current score by `CoverSwitchScoreMargin`, invalidate. | Catches missed events. |

T1, T2, T3 are *event-driven*. T4 is *periodic*. The event-driven triggers fire instantly when the predicate flips true; T4 is the only one that fires on a clock.

### 4.2 Gates / interlocks (block invalidation from committing)

A trigger fires → invalidation is *queued*, but the following must clear before the switch commits:

| # | Gate | Default |
|---|---|---|
| G1 | **Min dwell.** Time since arriving at current slot must be ≥ `CoverSwitchMinDwell`. | 1.0 s |
| G2 | **Debounce.** Trigger condition must remain true for `CoverSwitchDebounceTime` of wall time. If predicate drops false inside the window, queued switch is cancelled. | 0.3 s |
| G3 | **Player-velocity duration.** Only applies to T1. If player's velocity > walk speed at end of debounce, extend debounce by another `CoverSwitchDebounceTime`. Repeats until player settles. | 0.3 s extension |
| G4 | **Don't-switch-while-firing.** If `BTTask_CompanionCombat` is in an active burst-fire window, the switch is held until the burst ends. Hard cancel only on T3. | n/a |

G3 specifically scopes the "wait for player to commit a reposition" interlock to player-driven triggers — does not block LoS-driven switches, which need to be responsive.

### 4.3 Picker — filters & gates added on switch path

The switch path calls `FindBestCoverFor` with the same scoring as initial entry, but with the following extra rules applied to candidates:

| # | Rule | Notes |
|---|---|---|
| P1 | `QuerierLoc` is the **formation point**, not the companion's current location. | Drives proximity toward where the companion *should be following*, not where it currently is. |
| P2 | `MaxRadius` tightened to **~75 %** of initial-entry `SearchRadius`. | Switches stay local — Halo "areas" pattern. ~900 cm vs entry's 1200 cm. |
| P3 | **Exclude current slot** from candidates. | Otherwise picker can pick itself. |
| P4 | **Exclude just-vacated slot for `CoverSwitchPostVacateCooldown`.** | Prevents snap-back when player backtracks. Uncharted 4 pattern. |
| P5 | **View-cone penalty.** Candidates that lie within the player's forward view cone for the next 0.5 s of projected facing receive a score penalty (not a hard veto). | Keeps companion out of fire lane. Ellie pattern. |
| P6 | **Margin gate.** New slot's score must beat current slot's score by `CoverSwitchScoreMargin` (multiplicative, default 1.2× = 20 % better). | Anti-oscillation. Universal pattern. |
| P7 | **Validity smoothing.** LoS / view-cone signals fed through 0.3-0.5 s rolling window before they're allowed to affect a score. | Anti-jitter. |

### 4.4 Hysteresis defaults (full system)

| Tunable | Default | Purpose |
|---|---|---|
| `CoverSwitchMinDwell` | 1.0 s | G1 |
| `CoverSwitchReEvalInterval` | 1.0 s | T4 tick rate |
| `CoverSwitchDebounceTime` | 0.3 s | G2 / G3 |
| `CoverSwitchInvalidationRadius` | 500 cm | T1 threshold |
| `CoverSwitchLosLostWindow` | 0.5 s | T2 threshold |
| `CoverSwitchScoreMargin` | 1.2× | P6 |
| `CoverSwitchSearchRadiusFactor` | 0.75 | P2 (multiplier on entry SearchRadius) |
| `CoverSwitchPostVacateCooldown` | 3.0 s | P4 |
| `CoverSwitchViewConePenalty` | 0.4 | P5 (multiplicative score factor) |
| `CoverSwitchPlayerWalkVelocityThreshold` | 200 cm/s | G3 threshold |

### 4.5 Animation handling (future expansion)

Transition animation is **picked at move-start**, not at decision-time, from the tuple `(StartPosture, EndPosture, Distance, Urgency, ThreatExposure)`. The switch decision system has no opinion on animation — it just clears `BB_HasCoverPosition` and lets the existing `BTTask_MoveToCover` flow handle locomotion. When richer transition animations land later, they slot in at the move-start without any change to the switching decision logic.

Expected future mappings:

| Start → End | Distance | Anim |
|---|---|---|
| Stand → Crouch | short | combat slide |
| Crouch → Stand | short | crouch-sprint stand-up |
| Stand → Stand | long | sprint |
| Crouch → Crouch | short | crouch-walk |
| any | very short (< 200 cm) | direct sidestep / step-over |

This table is illustrative — not part of the prototype.

### 4.6 Legibility tell (future expansion)

At commit-time (after gates clear, before move begins), play one quiet tell:

- A short VO line from the bark bank ("Moving up", "Repositioning"), OR
- A footstep / equipment-shift audio cue.

Single cue, ~0.2 s, before the locomotion starts. Avoids the "where did the companion go" disorientation called out in F.E.A.R. and Elizabeth research. Not in prototype.

### 4.7 Combat-end cleanup

When the perception system reports the threat is gone (combat target cleared, hostile count = 0), exit cover via the existing combat→follow BT branch transition. No new code — but flagged so we don't accidentally make the switch logic gate on combat-target presence in a way that traps the companion in cover after combat ends.

---

## 5. Prototype scope (THIS implementation)

A strict subset of section 4. Goal: demonstrable cover switching with minimal surface area. ~80-120 lines of new code.

### 5.1 What's included

- **T4 only**: periodic re-eval at `CoverSwitchReEvalInterval`. No T1 / T2 / T3 yet — but T4 implicitly handles T1 because the picker scores proximity to the formation point, so a far-away player will cause the proximity score of a closer slot to dominate.
- **G1 only**: min dwell. No debounce, no velocity gate, no firing interlock.
- **P1, P3, P6 only**:
  - `QuerierLoc` = formation point.
  - Exclude current slot from candidates.
  - Margin gate (new slot must beat current by 20%).
- **No P2 radius tightening** — use the existing entry `SearchRadius` (1200 cm) as-is.
- **No P4 post-vacate cooldown** — risk of snap-back is low at a 1 Hz re-eval cadence.
- **No P5 view-cone penalty** — known failure mode, deferred.
- **No P7 validity smoothing** — only matters once T2 / P5 are in.

### 5.2 Prototype tunables (added to `UCompanionTuningDataAsset`)

| Field | Type | Default | Purpose |
|---|---|---|---|
| `CoverSwitchMinDwell` | float | 1.0 s | G1 |
| `CoverSwitchReEvalInterval` | float | 1.0 s | T4 tick rate |
| `CoverSwitchScoreMargin` | float | 1.2 | P6 |

All three are `EditAnywhere`, clamped to sane ranges (min dwell ≥ 0.1 s, interval ≥ 0.25 s, margin ∈ [1.0, 3.0]).

### 5.3 Implementation surface

| Change | File | Lines (est.) |
|---|---|---|
| Add 3 tuning fields | `Public/AI/CompanionTuningDataAsset.h` | +6 |
| New BT service `BTService_CoverSwitchMonitor` (header) | `Public/AI/BTS/BTService_CoverSwitchMonitor.h` | ~40 |
| New BT service (impl) — ticks, runs T4, applies G1+P6, clears `BB_HasCoverPosition` on switch | `Private/AI/BTS/BTService_CoverSwitchMonitor.cpp` | ~80 |
| Register new public/private subfolders if needed | `Extraction.Build.cs` | already covers `AI/BTS` |
| Compute formation point helper (utility) — may already exist in follow code | TBD during impl | ~10 |

Behaviour-tree wiring (in-editor, separate step after C++ lands): place the new service on the same node as `BTService_UpdateCompanionState` inside the Combat branch's in-cover subtree. Author configures blackboard key references in the editor.

### 5.4 Decision algorithm (prototype)

Pseudocode for the new service's `TickNode`:

```
on TickNode(Owner, NodeMemory, DeltaSeconds):
    if not in cover (BB_HasCoverPosition == false): reset state, return
    if !IsValid(BB_CoverSlot): reset state, return

    TimeSinceArrival += DeltaSeconds
    TimeSinceReEval  += DeltaSeconds

    if TimeSinceArrival < CoverSwitchMinDwell: return            # G1
    if TimeSinceReEval  < CoverSwitchReEvalInterval: return     # T4 tick

    TimeSinceReEval = 0

    FormationPoint = ComputeFormationPoint(Player)
    Best = CoverRegistry.FindBestCoverFor(FormationPoint, BB_CombatTarget, SearchRadius)

    if !Best || Best == BB_CoverSlot: return                     # P3

    CurrentScore = ScoreSlot(BB_CoverSlot, FormationPoint, BB_CombatTarget)
    BestScore    = ScoreSlot(Best,        FormationPoint, BB_CombatTarget)

    if BestScore < CurrentScore * CoverSwitchScoreMargin: return # P6

    # Commit the switch
    BB_HasCoverPosition = false
    BB_CoverSlot.Release(Owner)
    TimeSinceArrival = 0
    # BT re-flows into BTTask_MoveToCover next tick — claims Best, moves there
```

`ScoreSlot` is the same scoring expression as inside `FindBestCoverFor` but exposed as a static helper so we can score the current slot for the margin check. This is the one refactor needed inside `CoverRegistrySubsystem` — extract the scoring expression into `static float UCoverRegistrySubsystem::ScoreSlotFor(Slot, QuerierLoc, Target, MaxRadius)`. Existing call site inside `FindBestCoverFor` uses the same helper, preserving behaviour exactly.

Arrival detection: when `BB_HasCoverPosition` transitions from false → true (companion just reached cover), reset `TimeSinceArrival = 0`. Service tracks the previous-tick value of `BB_HasCoverPosition` in NodeMemory.

### 5.5 Deferred from ideal (explicit cut list)

| Feature | Why deferred | Failure mode in prototype |
|---|---|---|
| Don't-switch-while-firing | Coupling with `BTTask_CompanionCombat` firing state | Companion may abandon cover mid-burst (rare; burst < re-eval interval) |
| View-cone penalty | Needs player facing projection | Companion may briefly cross player fire lane |
| Velocity-duration gate | Needs player velocity smoothing | Companion may pick a slot you've already left if you're sprinting at re-eval moment |
| Debounce | Needs queued-trigger state machine | Edge case only — won't trigger at 1 Hz tick |
| Post-vacate cooldown | Needs vacated-slot timestamp tracking | Companion may snap back to just-left slot if you reverse course |
| LoS-sustained trigger | Needs smoothed LoS signal | T4 will eventually pick up via score, with ~1 s lag |
| Slot-released trigger | Defensive only | Recovered by next T4 tick (~1 s) |
| Legibility tell | Bark bank wiring | Switches happen silently |
| Search radius tighten | Optimisation | Slightly larger candidate set per re-eval |

The failure-mode column is the visible jank we accept for the prototype. Each is on the list to fix later, in roughly the priority order shown.

### 5.6 Test scenarios (prototype acceptance)

These are the demonstrations the prototype must satisfy. To be run manually in PIE.

| # | Scenario | Setup | Expected |
|---|---|---|---|
| TS1 | Walk-away switch | Player + companion in cover near each other. Player walks to a new room with cover authored nearby. | Companion abandons original slot after ~1 s, moves to a slot near the player's new position. |
| TS2 | No-switch when staying | Player remains stationary near companion's cover for 10 s. | Companion does not switch. |
| TS3 | No-oscillation between near-equal | Two cover slots equally well-placed relative to formation point. Companion picks one. | Companion does not flip back and forth on subsequent re-evals. |
| TS4 | Min-dwell respected | Player walks past quickly, returns. Companion's last re-eval was < 1 s ago when player passed. | Companion does not switch while inside dwell window. |
| TS5 | Re-pick respects existing rejection rules | Move player to a position where only hide-only stand cover is available. | Companion does not switch to hide-only cover (existing `FindBestCoverFor` rejects it). |
| TS6 | Combat-end clean exit | Kill all enemies while companion is in cover. | Companion exits cover via the combat→follow BT branch transition. New service does not block this. |

### 5.7 Open questions (prototype, to resolve during implementation)

- **Where to place the new service in the BT**: top-level of Combat branch vs inside in-cover subtree. The latter avoids ticking the service while the companion is in transit. Decide during impl after reading the BT graph.
- **`ScoreSlot` extraction**: confirm no other consumer of `FindBestCoverFor` will be broken by exposing the scoring helper. Should be drop-in given current code.
- **Formation-point helper**: may already exist inside `BTTask_FollowPlayer` / equivalent. If so, lift to a shared utility rather than duplicate.

## 6. Test plan

Unit / automation: deferred — companion AI is currently QA'd via the manual scenarios in `agent_docs/companion_testing.md` (referenced in CLAUDE.md). The six TS scenarios above will be added there once the prototype is committed.

Build verification: standard project build after impl. Reviewers in the team pipeline cover safety / performance / edge-case checks.

## 7. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Re-eval at 1 Hz causes per-frame work spike | Re-eval already O(N) over slot count, currently <50 slots. Single-frame cost is negligible. |
| Companion ping-pongs between two slots in a room with two equally good candidates | Margin gate (20%) plus min dwell. Confirmed by research as sufficient at this tick rate. |
| `BTTask_MoveToCover` aborts the move halfway and leaves companion in open ground | Existing task already has stopped-short tolerance + claim release on abort. No new failure mode introduced. |
| Future T1-T4 expansion forces redesign of the service | Service is intentionally additive — the prototype tick body becomes one of multiple paths in the full version. No public API on the service to break. |

## 8. Out of scope (this spec)

- Player-issued cover commands ("hold here", "fall back"). Already handled by `CommandedAction` BB key + command branch.
- Stealth-mode cover policy (sticky unless detected). Already encoded in `CompanionMode` → cover policy table in technical GDD.
- Cover slot authoring tooling. Slots are placed by hand; no editor tools changes here.
