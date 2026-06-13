# AI Companion — Status Snapshot (2026-04-30)

**Branch:** `AI-Companion-Prototype`
**Latest commit:** `c62bdbf` — Fix companion sprint-state latching and locomotion init order
**Engine:** UE5.7 / C++ module `Extraction`

A "where the AI companion stands today" lookback intended as raw material for the final project report. Captures architecture, working capabilities, current limitations, and outstanding work.

---

## 1. Architecture overview

The companion is a state-driven AI character living inside the player's session. Server-authoritative; behaviour decisions and weapon firing run on the server and replicate to clients.

### Class layout

| Layer | Class / Asset | Role |
|---|---|---|
| Pawn | `ACompanionCharacter` (`Public/Companion/CompanionCharacter.h`) | Replicated AI character. Owns weapon, health, traversal; exposes `SetSprinting`, fire/reload API, aim target |
| Controller | `ACompanionAIController` (`Public/AI/CompanionAIController.h`) | Possesses companion, runs the BT |
| Behaviour | `BT_Companion`, `BB_Companion` (Content/Core/Blueprints/AI/Companion) | Behaviour tree + blackboard wiring tasks below |
| Animation | `UCompanionAnimInstance` (`Public/Animation/CompanionAnimInstance.h`) + `ABP_Companion` + `BS_Companion_Locomotion` | Anim instance reads `IsSprinting`/velocity; ABP routes through locomotion blendspace + traversal/combat montages |
| Components | `UHealthComponent`, `UTraversalComponent` | Health & shields; shared traversal (vault/climb/mantle) reused from player |

### BT tasks shipping today

| Task | File | Purpose |
|---|---|---|
| `BTTask_FollowPlayer` | `Private/AI/Tasks/BTTask_FollowPlayer.cpp` | Formation follow with sprint catch-up + idle hysteresis. Also drives the "sprint-to-target" mode used by revive |
| `BTTask_CompanionCombat` | `Private/AI/Tasks/BTTask_CompanionCombat.cpp` | Engages enemies within `MaxEngageRange` |
| `BTTask_RevivePlayer` | `Private/AI/Tasks/BTTask_RevivePlayer.cpp` | Reaches and revives a downed player |
| `BTTask_MoveToCover` | `Private/AI/Tasks/BTTask_MoveToCover.cpp` | Pathing to EQS-selected cover positions |
| `BTService_UpdateCompanionState` | (service) | Periodic blackboard updates |

---

## 2. Capabilities (verified working)

### Movement & locomotion
- **Formation follow:** maintains an offset relative to the player (configurable: back, right) using player velocity to predict where to stand
- **Sprint catch-up → walk transition:** when `DistToPlayer > SprintDistanceThreshold` (currently 800) the companion sprints; once back within range it correctly falls back to walk speed and walk animation. *(This was the bug fixed in commit `c62bdbf`; see §4.)*
- **Idle hysteresis:** stops moving once `DistToPlayer <= AcceptableRadius` (250) and waits to re-engage until distance exceeds `1.5×` that radius — prevents jitter when the player wiggles around the companion
- **Locomotion blending:** `BS_Companion_Locomotion` blendspace driven from `Speed` / `Direction` / `IsSprinting`, set up in `ABP_Companion`
- **Traversal:** vault / climb / mantle via shared `UTraversalComponent`; sprint flag is force-cleared on traversal start so the companion doesn't blend sprint anim onto a montage

### Combat
- Server-spawned weapon, attached to capsule, replicates to clients
- `StartWeaponFire` / `StopWeaponFire` / `Reload` API; combat task uses these
- Per-target inaccuracy that settles from `MaxInaccuracyDegrees` (8°) to `MinInaccuracyDegrees` (1.5°) over `InaccuracySettleTime` (1.5s) when locked on the same target — gives a ramp-on-target feel, not headshot-instant

### Health & death
- `UHealthComponent` HP + shield; takes damage from any source via `TakeDamage`
- On death: tick disabled, weapon stops firing, capsule collision off, movement stopped, destroy after `DestroyDelay` (3s)

### Revive
- `BTTask_RevivePlayer` handles approach + revive timing (`ReviveDuration` = 4s, `ReviveProximityRadius` = 200)

---

## 3. Tuning values (where they live)

These are the knobs designers/you will iterate on most:

| Knob | Where | Current |
|---|---|---|
| `WalkSpeed`, `SprintSpeed` | `BP_Companion` Class Defaults | 400, 650 |
| `SprintDistanceThreshold` | `BT_Companion` → FollowPlayer node | 800 |
| `AcceptableRadius` | `BT_Companion` → FollowPlayer node | 250 |
| `FormationOffsetBack`, `FormationOffsetRight` | `BT_Companion` → FollowPlayer node | 350, 200 |
| `MaxEngageRange` | `BP_Companion` | 2500 |
| `MinInaccuracyDegrees`, `MaxInaccuracyDegrees`, `InaccuracySettleTime` | `BP_Companion` | 1.5°, 8°, 1.5s |
| `ReviveDuration`, `ReviveProximityRadius` | `BP_Companion` | 4s, 200 |

C++ defaults exist in `ACompanionCharacter` but BP overrides win at runtime — see §4 for the construction-order trap.

---

## 4. Recent fix worth documenting (for the report)

**Bug:** companion would sprint to catch up correctly but stay stuck in sprint anim + sprint speed once it caught up.

**Two compounding root causes:**

1. **Sprint state was gated behind an unrelated early-return.** In `BTTask_FollowPlayer::TickTask`'s formation branch, `SetSprinting(...)` ran *after* a "if formation target hasn't moved much, return" guard. Once the player slowed/stopped, the formation target stabilised, the early-return fired every tick, and `SetSprinting` was never re-evaluated — whatever value it had during the chase latched. The idle-entry branch also didn't clear sprint. Fix: call `SetSprinting` every tick before the early return; clear sprint in idle entry; add `OnTaskFinished` to clear on abort.

2. **Construction-order vs Blueprint CDO overrides.** `ACompanionCharacter`'s constructor sets `MoveComp->MaxWalkSpeed = WalkSpeed` using the *C++ default* (400) — but `BP_Companion` overrides `WalkSpeed` to 600 (later tuned to 400). BP CDO values apply *after* the constructor, so the component's `MaxWalkSpeed` retained the constructor value (400) until the first `OnRep_IsSprinting` call read the BP-overridden `WalkSpeed` (600) and updated `MaxWalkSpeed` mid-game. Result: companion silently switched walk speeds after its first sprint event. Fix: `BeginPlay` re-applies `OnRep_IsSprinting()` so `MaxWalkSpeed` reflects BP-overridden values from spawn.

**Diagnostic value for report:** the bug was invisible without per-tick logging of `Dist / Sprint / MaxWalkSpeed / Vel` — once those four values were captured side-by-side, the latch was obvious in three log lines. The instrumentation is still in place at `VeryVerbose` (re-enable with console: `Log LogCompanion VeryVerbose`).

---

## 5. Limitations / known gaps

### Behaviour
- **Single companion only.** No squad coordination, no inter-companion awareness, no role assignment. Adding a second companion would surface ordering issues (which one revives the player, who flanks) that the current BT can't express.
- **No player-issued commands.** Companion picks its own targets and movement; no "hold position", "go there", "focus that target" control surface yet.
- **Combat behaviour is single-track.** One engagement pattern — get in range, fire with inaccuracy. No suppressive fire, no flanking, no retreat-when-low-HP.
- **Cover usage is opportunistic, not committed.** The `MoveToCover` task exists and the EQS context picks positions, but cover isn't integrated as a default combat posture — companion will leave cover to chase the player every time the player moves.
- **Revive flow has no fallback.** If the path to the downed player is blocked or the player moves out of `ReviveProximityRadius` mid-revive, behaviour is brittle.

### Animation / locomotion
- **Blendspace ranges are coupled to `WalkSpeed`/`SprintSpeed`.** Tweaking the speeds without re-tuning `BS_Companion_Locomotion` causes the wrong pose at the wrong velocity (this is what the user perceived as "stuck in sprint" before the speeds were tuned to 400/650). Future: drive locomotion via `IsSprinting` flag rather than raw velocity for pose selection.
- **No strafe/back-step poses.** Direction parameter exists but the blendspace currently leans forward-only.
- **No aim-offset polish in motion.** Aim pitch/yaw is computed but the visual layering with locomotion can drift on heavy turns.

### Networking / replication
- `bIsSprinting` replicates `COND_SkipOwner` — fine for AI (no owning client) but worth re-checking when listen-server vs dedicated-server flows are stress-tested.
- No client-side prediction. Acceptable for an AI companion at current bandwidth, but rapid sprint↔walk toggles will be 1-frame behind on remote clients.
- Not yet validated under packet loss / latency.

### Test coverage
- **No automation tests.** Manual scenarios live in `agent_docs/companion_testing.md`. Mirroring those into automation tests is the next obvious quality gate.
- No regression test for the sprint-latch bug specifically — would be a good first automation case.

### Performance
- `ACompanionCharacter::Tick` is enabled (used for `TimeAimingAtCurrentTarget` accumulation). Cheap today but the companion ticks every frame even when idle — could move to a 0.1s timer for that accumulator.
- `BTTask_FollowPlayer` is `bNotifyTick = true` (per-frame). Distance + formation calc every frame is fine for one companion; would re-evaluate at 4+ companions.
- No object pooling needed yet (no per-second spawns).

### Configuration / robustness
- The construction-order trap surfaced once (§4); now defended via `BeginPlay` re-apply, but the same pattern could re-occur on any future replicated property whose initial value depends on a BP-overridden field.
- BT decorator coverage is light — no aborts on health-low, no priority shifts based on player stance.

---

## 6. Suggested next steps (rough priority)

1. **Player command surface.** Even minimal — "hold here" / "follow" / "focus target" — adds significant gameplay depth and is a natural extension of the BT (top-level Selector branch driven by a blackboard `CommandedState` enum).
2. **Cover-as-default combat posture.** Re-wire combat to prefer cover positions, only leave cover when a fire opportunity demands it. Reuses the existing EQS + `MoveToCover` task.
3. **Automation tests** mirroring `companion_testing.md` scenarios (follow distance, sprint catch-up, revive trigger, weapon fire range).
4. **Squad slot.** Architect for a second companion before adding one — define who-does-what when both are in scope (avoid double-revives, target spreading).
5. **Locomotion refactor:** use `IsSprinting` to gate pose selection in the blendspace, decoupling visual gait from raw velocity. Would have prevented the 600-cm/s "sprint-looking walk" issue entirely.

---

## 7. Key files quick reference

```
Private/AI/CompanionAIController.cpp
Private/AI/Tasks/BTTask_FollowPlayer.cpp        ← formation + sprint catch-up
Private/AI/Tasks/BTTask_CompanionCombat.cpp
Private/AI/Tasks/BTTask_RevivePlayer.cpp
Private/AI/Tasks/BTTask_MoveToCover.cpp
Private/Companion/CompanionCharacter.cpp        ← pawn, weapon, health, sprint API
Private/Animation/CompanionAnimInstance.cpp     ← anim state from companion
Private/Movement/TraversalComponent.cpp         ← shared traversal (also used by player)

Content/Core/Blueprints/AI/Companion/
  BP_Companion.uasset                           ← BP class defaults (WalkSpeed/SprintSpeed/etc)
  BT_Companion.uasset                           ← behaviour tree
  BB_Companion.uasset                           ← blackboard
  ABP_Companion.uasset                          ← anim BP
  BS_Companion_Locomotion.uasset                ← locomotion blendspace
```

---

## Diagnostic log categories (for future debugging)

- `LogCompanion` — gameplay-side companion events (sprint transitions, BT branch changes)
- `LogCompanionAI` — AI-side warnings (e.g. missing weapon class)

To re-enable the per-tick formation/state logs that helped solve the sprint-latch bug:
```
console: Log LogCompanion VeryVerbose
```
