# Director Test Playground — build spec

Grey-box test area exercising the full `UEnemyDirectorSubsystem` pipeline (spawning,
tension sawtooth pacing, mission-phase changes) in one space, built on the existing
`zoneDirector` block.

## Coordinate system

All positions below are **offsets in cm from the `zoneDirector` actor**, long axis = X,
width = Y, up = Z. First step of the build: query `zoneDirector`'s world transform and
top-surface Z; place everything relative to that (origin = block centre, floor = top
surface). Confirm the block is ≥ 80 m × 40 m of usable floor; if smaller, scale the X/Y
offsets down proportionally (keep zones in the 15–45 m band from their phase's fight).

Footprint: **8000 × 4000 cm** (80 × 40 m). Long axis X: −4000 → +4000. Width Y: −2000 → +2000.

## Phase bands (along X)

| Band | X range | Fight centre |
|---|---|---|
| Infiltration | −4000 → −1500 | ~(−2600, 0) entry room |
| Objective (arena) | −1500 → +1500 | (0, 0) arena |
| Extraction | +1500 → +4000 | (+3300, 0) extract pad |

## Geometry (grey-box, prototype-grid material)

- **Floor**: the existing zoneDirector surface (nav already baked — do not rebuild nav).
- **Arena walls**: rectangle X −1200..+1200, Y −1000..+1000, wall height **400 cm** (full LOS block).
  One **250 cm doorway** centred on each of the 4 sides.
- **Arena cover** (so fights sustain → tension builds): 2 full-height pillars (400 cm) at
  (−400, −300) and (+400, +300); 4 waist-high blocks (100 cm) clustered within ~600 cm of centre.
  At least one full-height blocker must always sit between arena centre and each spawn alcove.
- **Spawn alcoves**: each a 3-sided niche ~1000 × 1000 cm, wall height 400 cm, **open side facing
  AWAY from the arena** (this is what keeps the zone off-camera). Short offset from the nearest
  doorway so arena-centre line-of-sight into the niche is broken.
- **Extract pad**: raised/marked platform X +2900..+3700, Y −600..+600, distinct prototype colour.

## Spawn zones — `AEnemySpawnZone` (7 total)

Box uses default extent 400×400×200 cm (8×8×4 m) — each niche must contain it. Place the actor
at the niche centre. Set `DirectorConfig = DA_DirectorTestConfig` on **every** zone (first
registered wins; redundant assignment is safe).

| Actor | Pos (X, Y) | `ActivePhases` | `AreaTag` |
|---|---|---|---|
| SZ_Obj_N | (−500, +1700) | [] (empty = all) | Obj_N |
| SZ_Obj_S | (+500, −1700) | [] | Obj_S |
| SZ_Obj_E | (+1900, +300) | [] | Obj_E |
| SZ_Infil_NW | (−3200, +1500) | [Infiltration] | Infil_NW |
| SZ_Infil_SW | (−3200, −1500) | [Infiltration] | Infil_SW |
| SZ_Extract_NE | (+3200, +1500) | [Extraction] | Extract_NE |
| SZ_Extract_SE | (+3200, −1500) | [Extraction] | Extract_SE |

Each must sit on baked navmesh (the director nav-projects spawn points; off-nav = silently skipped).

## Data — `DA_DirectorTestConfig` (`UDirectorConfigData`)

Tension + spawn-distance fields: leave at class defaults (decay 4, perKill 8, perEngaged 3,
perHealthLost 0.8, peak 75, reliefEntry 40, reliefDur 25, dist 1500/4500). Tunable later.

Per-phase blocks (Compositions reference the project's BP enemy classes under
`/Game/Core/Enemies` — resolve actual `BP_Enemy_*` paths in-engine):

| Phase | IntensityCeiling | Cadence (s) | MaxAlive | Compositions (weight) |
|---|---|---|---|---|
| Infiltration | 40 | 20 | 6 | 2×Grunt (1.0); 1×Grunt + 1×Pistol (1.0) |
| Objective | 70 | 15 | 10 | 3×Grunt (1.0); 2×Grunt + 1×Shotgun (1.0); 2×Grunt + 1×Rusher (0.7) |
| Extraction | 90 | 10 | 12 | 3×Grunt + 1×Shotgun (1.0); 2×Rusher + 2×Grunt (0.8); 1×Officer + 3×Grunt (0.6) |

Empty Compositions = zero spawns even with zones placed — populate all three.

## Logic — phase triggers (new BP `BP_PhaseTrigger`)

Box-collision actor. On player begin-overlap: get world subsystem
`UEnemyDirectorSubsystem` → `SetMissionPhase(TargetPhase)`. Expose `TargetPhase` (EMissionPhase)
as an instance-editable var. Fire once (disable after first overlap).

- Trigger A: thin box at X = −1500, full Y width, `TargetPhase = Objective`.
- Trigger B: thin box at X = +1500, full Y width, `TargetPhase = Extraction`.

## First contact — seed enemies (3)

3 × `BP_Enemy_Grunt` in the entry room at ~(−2800, −400), (−2600, 0), (−2400, +400),
on navmesh, **`bIsolatedEncounter = false`** (default-true gym flag would stop them waking the
director). They patrol/stand; on sighting the player → Combat → alert → Loud → director ticks.

## Verify (screenshot each stage)

1. Block-out shell (walls/arena/cover/alcoves/extract) reads sane top-down + eye-level.
2. 7 zones sit inside niches, on navmesh, open side facing away from arena.
3. Config compositions populated; DA assigned to zones.
4. Triggers span the band boundaries; seed enemies placed and not isolated.

## Done = press-Play ready

Player enters → seed grunts spot them → director wakes → off-camera reinforcements stream from
the ring → cross X=−1500 to ramp into Objective waves → cross X=+1500 for Extraction-tier pressure.
