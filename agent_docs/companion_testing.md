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

## Test 7 — Shoot Takedown + Headshot Damage
**Goal:** Companion executes a marked enemy with a clean ranged "headshot"; player headshots deal 65% of max health.

| Scenario | Expected |
|----------|----------|
| Headshot a full-HP (100) enemy with your gun | ~65 dmg; **2 headshots kill**. Body/limb shots unchanged |
| Ping (MMB) a **lone** unaware enemy + press U (shoot) | Companion turns to face → aims in ~1s → fires 2 shots → enemy **drops promptly** → companion lowers. Fires solo after a 2–4s beat |
| Ping the companion's target with a **2nd** eligible enemy in the same takedown volume + U | Companion holds aim; kills the marked enemy the instant **you fire your own gun** |
| Watch the marked enemy during the kill | Must **not** spin 180 / return fire before dying — clean execution, no firefight |
| Knife takedown (Y) | Unchanged — still triggers off your own melee takedown (T) |
| Let an enemy headshot the player | Flat body damage, **no** headshot spike (player is immune) |

Tuning (no rebuild): `BP_Companion` → `ShootAimInDuration` / `ShootShotInterval` / `ShootShotCount` / `ShootLowerDelay`; enemy archetype → `HeadshotMaxHealthFraction` (0.65), `RangedTakedownRagdollDelay` (fall speed). The aim-in/lower *pose blend* lives in the companion AnimBP.

---

## Test 8 — Search-Room Enemy Detection

| Scenario | Expected |
|----------|----------|
| Normal Search; enemy faces the closed door | One breach startle, Combat in under one second, then normal combat behavior |
| Normal Search; enemy faces away with same-room loot | Detection rises to the breach-startle floor and the enemy turns/searches; the companion loots and watches but never fires before the enemy reaches Combat |
| Stealth/Quiet Search; enemy faces the closed door | No hearing event, one visual startle, stealth breaks at Combat, companion never fires first |
| Stealth/Quiet Search; visible unaware enemy plus same-room loot | Companion continues to Loot, watches at low-ready while stationary, never fires first, and aborts Loot when the enemy reaches Combat |
| Quiet Search; enemy is blocked from seeing the entry | No startle or visual detection until normal gunfire/alert propagation reaches it |
| Clear-sight corridor enemy inside the room radius; equally visible enemy outside it | Inside enemy detects the companion; outside enemy remains cloaked |
| Search through an already-open door | Exposure begins with the enter move; detection works with no door-startle bump |
| Empty Search dwell; ordered-Breach WaitingForPlayer | Exposure remains active until the command returns to Follow |
| Same-room Search/Breach → Loot chain | Exposure remains active through Loot; a newly visible enemy reacts mid-loot |
| Replacement ping; failed/aborted task; DBNO; death | Exposure clears without damaging the replacement command or later-room behavior |
| Change companion mode during an active Search/Breach chain | Exposure remains active until command completion |
| Outside Search/Breach | Combat remains weapons-free; Normal/Stealth retain no-first-shot, cloak, loot, and gunfire-alert behavior |

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
