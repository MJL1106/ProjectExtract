# Enemy Morale Rework — cautious-posture states (no rout)

**Status:** approved design, in implementation (branch `Enemies`).

## Goal
Delete the morale "Broken = retreat to deep cover and turtle (Fallback)" rout. Morale state now dials **how cautiously** an enemy fights, never **whether** it fights. The morale inputs/recovery/thresholds stay exactly as they are — only what the states *do* changes.

Killing the officer keeps its −75 morale hit (unchanged) → that lands the whole squad in **Broken = hunkered but still shooting**, which together with the existing automatic loss of the officer's aura / focus-fire / bounding gives the "leaderless, no buffs, cautious" reaction the director wants.

## State → behaviour (the ONLY thing changing)
- **Confident (>60):** unchanged. Full aggression — pursues/advances, standalone flank, joins bounding maneuvers, uses cover opportunistically.
- **Shaken (30–60):** cautious. Fights defensively from cover (peek-fire). **No** standalone flank, **no** advancing/pursuing the player, **no** bounding participation. Holds the line, still trades fire. One-off reaction on entry (bark).
- **Broken (≤30):** *enhanced* Shaken — hunkered. Everything Shaken does, plus more conservative timing: shorter/rarer peeks, longer pauses between bursts, stays glued to cover, only moves to escape a compromised slot. Still returns fire when it has a safe shot. **Never** a retreat/rout.

Escalation: **push → hold-cover-and-trade → hunker-but-still-shooting.**

## Implementation map

### C++
1. **`BTTask_EnemyCombatFire`** — read `BB_MoraleState` each tick. `bAggressive = (Confident)`, `bHunkered = (Broken)`.
   - **Pursue gating:** every path that currently advances toward the target on no-LOS / out-of-range (the `Pursuing` phase transitions in `ExecuteTask` and `TickTask`) must only run when `bAggressive`. When NOT Confident: instead of pursuing, seek cover (`TryReseekCover`) if not in cover, else hold in cover (crouch + Pause, wait for LOS). MUST stay `InProgress` while Awareness==Combat — never fail out, never advance toward the player.
   - **Hunkered timing:** when `bHunkered`, scale the peek loop conservatively — shorter Expose, shorter bursts, longer Pause. Use named scalars (or DA fields).
2. **`UEnemySquad::IsEligibleForManeuver`** — require `MoraleState == Confident` (Shaken/Broken enemies are never picked as bounding suppressor/flanker).
3. **`UEnemyMoraleComponent::EvaluateState`** — already barks on entering Broken; add a one-off bark on entering **Shaken** (reuse a fitting existing `EBarkType`; designer can add dedicated lines later).
4. **`BTTask_EnemyFallback`** — leave the class in place but UNUSED for now (its BT branches are removed in-engine first to avoid breaking BT asset load). Clean-up/delete the class in a follow-up build once no BT references it.

### In-engine (Behavior Trees)
1. **Delete the `[MoraleState==Broken] → Enemy Fallback` branch** from every archetype combat subtree (`BT_EnemyCombat_*`) that has it.
2. **Gate the standalone `Enemy Flank` branch on `MoraleState==Confident`** (built-in `BTDecorator_Blackboard`, key `MoraleState`, value `Confident`) so Shaken/Broken skip it. `MoraleState` already exists on `BB_Enemy` — no new key.

## Officer death
−75 unchanged → squad → Broken (hunkered) + automatic aura/focus-fire/bounding loss. No morale value edit needed.

## Out of scope (tracked separately)
- Companion-fight target-handoff gap (decayed suspicion track → ~0.5–2s wrong-way search when a fixated-on companion dies) — separate bug.
- `enemy.PersistCorpses 0` disabling the whole squad-death relay (officer-death focus-clear/morale/bounding-abort wired to corpse-spawn) — separate coupling cleanup. (Default is 1; the old MEMORY.md "default-0" note is stale.)
- Earlier bounding-teardown engage-in-place handoff (`ManeuverHoldUntil` + `BTDecorator_RecentManeuverHold`) already shipped this session — valid improvement, stays.
