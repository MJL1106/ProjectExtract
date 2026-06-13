# AI Companion Testing Plan

Test scenarios for the AI Companion Prototype on the `AI-Companion-Prototype` branch.

---

## Setup
- Open `Lvl_FirstPerson` (or your test level)
- Confirm `BP_Companion` is placed near PlayerStart
- Confirm 3-4 `BP_EnemyBase` placed around the level (some near walls/cover)
- Confirm `NavMeshBoundsVolume` covers the playable area (press `P` to visualise — green = walkable)
- Open the **Output Log** (Window > Output Log) — filter by `LogCompanion`, `LogEnemy`, `LogExtraction`

---

## Test 1 — Follow Behaviour
**Goal:** Companion stays in formation at a comfortable distance.

| Step | Expected |
|------|----------|
| Walk forward | Companion follows behind/right at ~350cm |
| Turn 360° | Companion does NOT spin around you (uses velocity, not look direction) |
| Stand still | Companion stops, holds position |
| Sprint away | Companion sprints to catch up, then walks when close |
| Walk into companion | Companion holds ground, doesn't push through you |

**Failure signs:** jittering, getting inside player, sprinting at full speed all the time.

---

## Test 2 — Enemy Detection & Combat
**Goal:** Companion sees enemy, engages, fires accurately over time.

| Step | Expected |
|------|----------|
| Walk into enemy line of sight | Companion immediately rotates toward enemy |
| Watch debug lines | Yellow = miss, Red = hit. Should fire in 1.5s bursts with 0.5s pauses |
| Stay aiming at same enemy | Inaccuracy decreases (look at red lines clustering tighter) |
| Magazine empties | Companion stops firing, reloads (~2s), resumes |
| Enemy dies | Combat ends, returns to follow |
| Multiple enemies | Picks closest, switches if closer one appears |

**Output Log:** `Companion hit BP_EnemyBase for 25.0 damage`

---

## Test 3 — Cover System
**Goal:** Companion uses environment for cover before engaging.

| Step | Expected |
|------|----------|
| Place enemy near a wall/pillar | Companion runs to cover spot first |
| Engage from open area (no cover) | Companion engages from current position (graceful fallback) |
| Watch in-game | Companion ends up between enemy and a wall |

**Failure signs:** running into enemy, ignoring cover, freezing.

---

## Test 4 — Revive (THE MONEY SHOT)
**Goal:** Companion drops everything to revive you when downed.

| Step | Expected |
|------|----------|
| Press `H` ~4 times to reach DBNO | Player enters DBNO state |
| Watch companion | Stops current action immediately (mid-combat or follow) |
| Companion sprints to you | Max speed, ignores enemies |
| Companion arrives within 200cm | Stops, faces you, holds 4s |
| After 4s | Player exits DBNO, restored to 30% HP |
| Post-revive | Companion returns to combat/follow |

**Output Log:**
- `Companion starting revive sequence for BP_ExtractionCharacter`
- `Companion revived BP_ExtractionCharacter`

---

## Test 5 — Damage & Death
**Goal:** Companion takes damage and dies properly.

| Step | Expected |
|------|----------|
| Let enemies shoot companion | HP/shield decreases (Output Log: `Companion took X damage — HP: Y / Shield: Z`) |
| Shield depletes first, then HP | Standard health flow |
| Companion HP hits 0 | Stops moving/firing, collision off, destroyed after 3s |

---

## Test 6 — Behaviour Priority
**Goal:** Higher-priority behaviours interrupt lower ones.

| Scenario | Expected |
|----------|----------|
| Mid-combat, you go DBNO | Combat aborts instantly, companion sprints to revive |
| While following, enemy appears | Follow aborts, companion engages |
| Mid-revive, enemy fires at companion | Revive continues (highest priority) |

---

## Known Limitations (prototype scope)
- Cylinder mesh only — no animations
- No squad coordination (single companion)
- No voice lines / barks
- Companion ignores its own DBNO (dies immediately when HP=0)
- No equipment switching

---

## Bug Report Template
When something breaks, capture:
1. What you were doing
2. What companion was doing before
3. Output Log filtered by `LogCompanion`/`LogEnemy`
4. Screenshot if visual
