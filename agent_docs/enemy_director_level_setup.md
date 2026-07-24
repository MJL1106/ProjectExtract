# Enemy Director Level Setup Guide

This is the practical setup guide for using the enemy director in a real building-interior level.

The short version: use one director scope for the whole building, then control the experience with spawn zones, mission phases, placed squads, patrol routes, and the director config data asset. For a first 3-floor level, do not try to make the scope volume itself be the floor-control system.

## What the director actually controls

The director does not own stealth patrols at the start of the level. Those are designer-placed enemies.

The director starts doing work only after global alert reaches `Loud`. That happens when an enemy confirms combat, an alarm is tripped, or body discovery escalates far enough. Once loud, the director ticks once per second and decides whether to spawn reinforcements.

The director controls:

- level-wide alert state: `Calm`, `Searching`, `Loud`
- current mission phase: `Infiltration`, `Objective`, `Extraction`
- tension: how much pressure the player is currently under
- spawn timing: whether another squad can arrive now
- spawn composition: what enemy group arrives
- spawn zone: where the group appears
- spawned squad setup: spawned enemies get grouped into a squad and seeded with the player's last known position

The director does not currently control:

- locked-room logic
- floor ownership
- cleared-area memory
- dynamic enabling/disabling of scope volumes from Blueprint
- bespoke objective logic
- placed enemy patrol routes

Those are level-design responsibilities.

## The pieces you place

### `EnemyDirectorScopeVolume`

Optional box volume that limits what the director counts and where it can spawn.

If no enabled scope volume exists, the director sees the whole world.

If one or more enabled scope volumes exist, the director only counts enemies and spawn zones inside any enabled scope. Multiple scopes behave like one combined area; they do not create separate director brains.

Use this to stop the director from counting test-gym enemies, exterior debug enemies, or another building.

For your first 3-floor building, place one scope around the whole playable building. Scale its Z high enough to cover all floors, roof spaces, stairwells, and spawn rooms.

Do not put it only at the base of the building unless the Z extent reaches every floor.

### `EnemySpawnZone`

Designer-placed box where director squads can spawn.

Important fields:

- `AreaTag`: label for you, logs, and future organization.
- `ActivePhases`: which mission phases can use this zone. Empty means all phases.
- `DirectorConfig`: optional config asset. The first registered zone with a config can feed the director if the director has not already been set explicitly.
- `ZoneBox` size: the box used to spread squad members.

The director rejects a zone if:

- it is not active for the current mission phase
- it is outside the active director scope
- it is closer than `SpawnDistanceMin`
- it is farther than `SpawnDistanceMax`
- the zone origin is visible from the player viewpoint
- any of the first 3 sample spawn points are visible from the player viewpoint
- the zone origin cannot project to navmesh

Spawn zones should be in service rooms, stair landings, back corridors, security rooms, maintenance rooms, loading rooms, or off-route side rooms. They must still be reachable by AI.

### `DirectorConfigData`

Data asset that controls pacing and enemy groups.

Global tension fields:

- `TensionDecayPerSecond`: how quickly pressure falls over time.
- `TensionPerPlayerHealthLost`: how much player damage increases pressure.
- `TensionPerKill`: how much enemy kills increase pressure.
- `TensionPerEngagedEnemy`: pressure added per combat enemy near the player each second.
- `PeakTensionThreshold`: tension value where spawning pauses into `Peak`.
- `ReliefEntryThreshold`: tension value where `Peak` drops into `Relief`.
- `ReliefDuration`: quiet time before spawning can resume.
- `SpawnDistanceMin`: minimum distance from player to spawn zone.
- `SpawnDistanceMax`: maximum distance from player to spawn zone.

Each mission phase has:

- `IntensityCeiling`: if tension is at or above this, that phase will not spawn.
- `SpawnCadenceSeconds`: minimum seconds between spawn attempts.
- `MaxAlive`: alive enemies allowed in the active director scope.
- `Compositions`: weighted enemy squads for that phase.

If a phase has no valid compositions, that phase cannot spawn anything.

If a composition would push alive enemies over `MaxAlive`, it is skipped.

Placed enemies count toward `MaxAlive`.

### Mission phase triggers

Level scripting calls `UEnemyDirectorSubsystem::SetMissionPhase`.

The available phases are:

- `Infiltration`
- `Objective`
- `Extraction`

Mission phase is global for the level. It is not per floor.

Use triggers at meaningful progress points:

- crossing from entry/service floor into main objective route
- entering the objective floor
- completing the objective
- entering the escape route

When phase increases, the director forces itself back to `Build`, which means the new phase can start pressure quickly once cadence and alive cap allow it.

### Placed enemies

Placed enemies are your stealth/readability layer.

For each placed enemy:

- leave `bIsolatedEncounter` false in real level content
- assign `SquadId` for enemies meant to fight together
- assign `PatrolRoute` for moving guards
- use guard-post enemies for fixed sentries
- place them on navmesh, not in cover meshes or wall edges

Enemies with no `SquadId` still fight, but they do not share sightings, flank as a squad, focus fire as a squad, or run officer-led plays.

Director-spawned enemies automatically become a squad.

## Recommended setup for a 3-floor building

Use this as the first real-level pattern.

### Scope

Place one `EnemyDirectorScopeVolume` around the whole building.

Cover:

- floor 1
- floor 2
- floor 3
- stairwells
- elevators or shafts if playable
- service rooms used as spawn rooms
- roof or extraction route if used

This keeps all building enemies under one pressure system. It also prevents weird cases where the player fights on floor 2 but the director ignores enemies on floor 3 because a scope is too thin.

Only use per-floor scope volumes later if you add code or Blueprint support to enable/disable them deliberately. Right now, multiple enabled scopes are just a combined union.

### Phases

For a first 3-floor interior, map phases to mission beats, not strictly floors.

Recommended layout:

| Level beat | Director phase | What it means |
|---|---|---|
| Entry, lobby, service access, floor 1 | `Infiltration` | low response, stealth mostly designer-placed |
| Main route and objective setup, floor 2 or floor 3 | `Objective` | active reinforcements, mixed squads |
| Objective complete, route back out, stairwell descent or roof escape | `Extraction` | high pressure, aggressive compositions |

If the level is "enter floor 1, climb to objective on floor 3, fight back down", use:

- floor 1 entry: `Infiltration`
- floor 2 transition: still `Infiltration` or switch to `Objective` if you want pressure earlier
- floor 3 objective: `Objective`
- objective complete: `Extraction`
- descent/backtrack: `Extraction`

Do not create a phase trigger every time the player changes floor unless that floor change is also a gameplay beat.

### Spawn zones per floor

Place 3 to 5 spawn zones per floor or major wing.

For each floor, use:

- 1 stairwell or lift lobby zone
- 1 service room zone
- 1 back corridor zone
- 1 optional side-office/security-room zone
- 1 optional far fallback zone for long routes

For a 3-floor building, a solid first pass is 10 to 14 zones total.

Example:

| Zone name | Phase | Location intent |
|---|---|---|
| `SZ_F1_Service_Loading` | `Infiltration` | behind loading/service entry |
| `SZ_F1_BackStair` | `Infiltration`, `Extraction` | rear stairwell, usable for entry response and escape pressure |
| `SZ_F1_SecurityOffice` | `Infiltration` | security room or side office |
| `SZ_F2_NorthOffice` | `Objective` | side office off main route |
| `SZ_F2_ServerService` | `Objective` | service room behind objective-adjacent space |
| `SZ_F2_BackStair` | `Objective`, `Extraction` | stairwell pressure |
| `SZ_F3_Maintenance` | `Objective` | maintenance room or plant room |
| `SZ_F3_RoofAccess` | `Extraction` | near escape route, not visible from objective |
| `SZ_F3_BackHall` | `Extraction` | behind player during escape |

Zones should be open to AI. Do not wall them off. If it is a spawn closet, it still needs a door-sized nav path into the level.

### Spawn-zone placement rules

Use these rules before tuning numbers:

- Put zones 15 to 45 metres from the likely fight space, matching the default `SpawnDistanceMin`/`SpawnDistanceMax`.
- Keep zones off the player's direct camera angle.
- Put at least one wall or corner between the fight and the zone.
- Ensure the zone origin and the first few spawn points are on navmesh.
- Make the room big enough for the largest squad composition in that phase.
- Do not put a zone inside a room the player can stare into for the whole fight.
- Do not put a zone inside a sealed room.
- Do not put enemies or spawn zones inside props, cover, desks, racks, or narrow doorframes.
- Make floors and ceilings block `Visibility`, or zones on another floor may fail sightline checks in strange ways.

If spawns feel late or absent, check distance first. A perfect spawn room 60 metres away is invisible to the default director.

### Placed enemies per floor

Placed enemies are the authored stealth layer. The director is the loud-response layer.

Suggested first pass:

| Floor | Placed enemies | Intent |
|---|---:|---|
| Floor 1 | 4 to 6 | guards, security, first contact |
| Floor 2 | 5 to 8 | patrols, office sentries, first real squads |
| Floor 3 | 4 to 7 | objective guards, officer squad, specialist preview |

Keep the Infiltration `MaxAlive` in mind. If Infiltration `MaxAlive` is 8 and you place 8 living enemies inside the scope, the director cannot spawn Infiltration reinforcements until some die.

For authored groups:

- give each group a shared `SquadId`
- use patrol routes in loops that are actually pathable
- keep officer-led squads for important spaces, not every room
- use guard-post enemies for readable sentries
- use patrol enemies for spaces the player watches before entering

## How to tune creative control

### More stealth, less chaos

Use:

- more placed enemies
- fewer/later phase triggers
- longer `SpawnCadenceSeconds`
- lower `MaxAlive`
- lower `IntensityCeiling`
- smaller/weaker compositions
- fewer zones active in `Infiltration`

Good for floor 1.

### More pressure after alarm

Use:

- shorter `SpawnCadenceSeconds`
- higher `MaxAlive`
- higher `IntensityCeiling`
- more spawn zones around the route
- compositions with rushers, officers, grenadiers, shields, or heavies

Good for objective and extraction.

### More breathing room

Use:

- higher `PeakTensionThreshold`
- higher `TensionPerKill`
- higher `TensionPerPlayerHealthLost`
- lower `ReliefEntryThreshold`
- longer `ReliefDuration`

Kills and player damage raise tension. High tension pauses spawns. This sounds backwards at first, but it creates the sawtooth: hard fight, then quiet.

### More constant pressure

Use:

- lower `ReliefDuration`
- lower `TensionPerKill`
- lower `TensionPerPlayerHealthLost`
- higher `TensionDecayPerSecond`
- higher `IntensityCeiling`

This makes the director return to Build sooner.

### More floor-specific control

Use `ActivePhases` on spawn zones.

Example:

- Floor 1 entry zones: `Infiltration`
- Floor 2 objective route zones: `Objective`
- Floor 3 objective/roof zones: `Objective`, `Extraction`
- Backtracking stair zones: `Extraction`

Avoid trying to use scope volumes for floor-specific control until the scope actor has a runtime enable/disable path.

## Suggested first config for the 3-floor level

Start conservative. Then tune after one full PIE run.

| Phase | Cadence | MaxAlive | Ceiling | Composition direction |
|---|---:|---:|---:|---|
| `Infiltration` | 40 to 50s | 8 | 35 to 45 | 2 grunts, pistol/grunt pair |
| `Objective` | 22 to 30s | 12 | 55 to 70 | 3 grunts, 2 grunts + rusher, 2 grunts + officer |
| `Extraction` | 12 to 18s | 16 to 20 | 80 to 90 | officer squad, rusher group, heavy push, shield group |

Use smaller squads until nav and spawn readability are proven.

Do not start with every archetype in every phase. Add one new archetype per pass so you can feel what changed.

## Gameplay flow for your first building level

Use this as the intended player experience:

1. Player enters floor 1 in `Infiltration`.
2. Placed patrols create stealth pressure.
3. If the player stays quiet, the director does nothing.
4. If the player goes loud, floor 1 response squads can arrive from service/stair zones.
5. Player reaches the main objective route and triggers `Objective`.
6. Objective zones unlock around floor 2/floor 3.
7. Director sends mixed squads while tension is low enough.
8. Fight peaks; director pauses.
9. Tension falls; relief timer gives a short quiet window.
10. Objective completes and triggers `Extraction`.
11. Extraction zones unlock, including zones behind the player.
12. Player escapes through pressure rather than fully clearing the building.

## Validation checklist

Before judging the director in PIE:

1. Build navmesh.
2. Confirm every placed enemy stands on navmesh.
3. Confirm every patrol route segment has a path.
4. Confirm every spawn zone center projects to navmesh.
5. Confirm every spawn room has a door or route into the level.
6. Confirm zones are not visible from the main fight angles.
7. Confirm every phase has at least one valid composition.
8. Confirm `MaxAlive` is higher than the largest intended placed fight plus one valid composition.
9. Confirm placed squads have `SquadId`.
10. Confirm placed enemies are not `bIsolatedEncounter`.
11. Confirm floor/ceiling collision blocks `Visibility`.
12. Confirm phase triggers call `SetMissionPhase`.

Useful log strings:

- `Director woke`
- `Mission phase`
- `Director spawned squad`
- `Director: no eligible spawn zone`
- `Director: no valid composition fits`
- `Director config validation`

## Common failure cases

### "No more enemies spawn"

Likely causes:

- alert never reached `Loud`
- no spawn zones registered
- no zone is active for the current phase
- all zones are visible to the player
- all zones are outside spawn distance
- all zones are outside the active scope
- zone origin is off navmesh
- phase composition array is empty
- composition size would exceed `MaxAlive`
- placed enemies already fill `MaxAlive`
- director is in `Peak` or `Relief`

### "Enemies spawn in bad places"

Likely causes:

- zones are in main rooms instead of side/service rooms
- zone box is too large and spills into walls or props
- first spawn samples land inside cover
- spawn room is not actually off-sightline
- `SpawnDistanceMin` is too low
- floor/ceiling does not block `Visibility`

### "Per-floor setup feels wrong"

Likely causes:

- using scope volumes as if they are floor states
- too many zones have empty `ActivePhases`
- phase triggers fire too early
- extraction zones are only ahead of the player, not behind or beside
- placed enemy count blocks the phase alive cap

## Recommended rule for this project

For the first real building:

- one building-wide director scope
- phase triggers by mission beat
- spawn zones grouped by floor and phase
- placed enemies for stealth and room identity
- director spawns for loud response and extraction pressure

Only split into per-floor director scopes if the level later needs hard isolation between floors and the scope actor gets runtime enable/disable support.
