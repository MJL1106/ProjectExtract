# DemoMap objective-flow wiring — migration source of truth

Captured from the live editor (VibeUE, DemoMap loaded) on 2026-07-27, before migrating
`ALevelObjectiveFlow` onto the data-driven `AObjectiveStep` chain. **This file is the spec
for the migration** — the rebuilt chain must reproduce it exactly.

Map: `/Game/UWC_Modular_Skyscraper/Maps/DemoMap`
Flow actor: `LevelObjectiveFlow_DemoMap` (class `LevelObjectiveFlow`, `bAutoActivate = true`)

## Migration constraints — read before placing anything

**Single swap, one change.** Delete `LevelObjectiveFlow_DemoMap` in the same edit that places the
step chain. Do NOT ship or test a state where both systems are wired to the same doors, enemies
and containers — they fight in both directions:

- A step-chain checkpoint fast-forward calls `ForceOpenInstant()` on tracked doors, which DOES
  broadcast `OnDoorOpened`, driving the legacy flow's `Advance()` through arbitrary door events.
- The fast-forward destroys tracked enemies. `LevelObjectiveFlow::IsEnemyGroupDead` requires a
  valid health component reporting dead, so a destroyed-but-never-killed enemy satisfies neither
  branch and the legacy `ClearRoom1` / takedown steps can then never complete.
- `MarkLootedForCheckpoint` is silent (no `OnLootCompleted`), but the legacy late-entry catch-up
  reads `KeycardContainer->IsLooted()` directly, so it still advances off the new system's state.

**Author the extraction wave as an `OnActivate` effect on the beat that watches it**, not as an
`OnComplete` effect on the rescue beat. `StartDirectorWave` never replays on a checkpoint resume,
so the OnComplete shape leaves a resume at the Defend beat watching a wave that was never started,
with no way to start one. The level audit warns about this, but the safe shape avoids it outright.

**Delete the downstairs `CompanionRouteTrigger`** when the `FollowCompanionDownstairs` beat lands —
the step now issues the route itself via the `CommandCompanionRoute` side effect. Leaving the
trigger is not fatal (`StartRoute` refuses the second call) but whichever fires first wins, and the
trigger fires on player position rather than on the beat.

**The Defend beat needs `bFreezeMarkerAtActivation` ticked and `MarkerTarget` left empty.** Do not
wire the VIP — he walks away from the ground the player is told to hold, which is the exact bug
legacy added `DefendAreaLocation` to fix.

## Wired references

| Field | Value |
|---|---|
| `RoofDoor` | `BP_BreachableDoor` |
| `DownstairsRoute` | `CompanionRoute` |
| `StairwellBreachDoor` | `BP_Door27` |
| `Room1Enemies` | `PE_R1_Grunt_WindowWest`, `PE_R1_Grunt_WindowEast`, `PE_R1_Grunt_PatrolNorthA`, `PE_R1_Grunt_PatrolWestUtility`, `PE_R1_Grunt_PatrolRear`, `PE_R1_Grunt_LootGuard`, `PE_R1_Grunt_KeycardCarrier` (7) |
| `KeycardContainer` | `PE_R1_LootCache_B` |
| `Room1ExitDoor` | `BP_Door17` |
| `Room2EntryDoor` | `BP_Double_Door7` |
| `FirstTakedownPair` | `BP_Enemy_Grunt`, `BP_Enemy_Grunt2` |
| `SecondTakedownPair` | `BP_Enemy_Grunt3`, `BP_Enemy_Grunt4` |
| `SupplyCrates` | `PE_R2_Supply_01` … `PE_R2_Supply_07` (7) |
| `ExtractionTarget` | **None** — the flow owns the extraction wave itself |
| `Extractee` | `ExtracteeCompanion_Room2` |
| `LiftGate` | `LevelCompletionLiftGate_Room2` |
| `Room1AreaLocation` | zero — auto-computed centroid at activation |

`ExtractionTarget` being null matters: there is no `AExtractionTargetActor` in DemoMap, so the
double-objective-registration edge case does not apply to this migration.

## Extraction wave (`ExtractionWave`)

| Field | Value |
|---|---|
| `WaveId` | `Room2Defence` |
| `TargetSquads` | 6 |
| `MissionPhase` | `Objective` |
| `ConfigOverride` | `DA_DirectorConfig` |
| `bAutoEngage` | true |
| `SpawnCadenceOverride` | 8.0 |
| `FirstSquadDelaySeconds` | 2.0 |
| `BlockedWarningSeconds` | 30.0 |
| `GuaranteedSquads` | squad 3 → `HeavyPush`, squad 6 → `HeavyPush` |
| `ExtractionCompletionAction` | `UnlockExit` (target: `LevelCompletionLiftGate_Room2`) |

## Checkpoints

| Step | Spawn point |
|---|---|
| `FindOfficeKeycard` | `TP_Checkpoint_FIND_OFFICE_KEYCARD` |
| `ReachExtractionTarget` | `TP_Checkpoint_REACH_EXTRACTION_TARGET` |

## Director actors in DemoMap

One scope volume, `EnemyDirectorScope_Room2`. **No phase triggers placed** — the level never
leaves the default mission phase on its own, so the `Objective`-only spawn zones below are live
from the start and the wave's own `MissionPhase` is what drives the defence.

| Zone | AreaTag | ActivePhases | Config | WaveEligible | Bias |
|---|---|---|---|---|---|
| `EnemySpawnZone_Room2_West` | `Room2_West` | Objective | DA_DirectorConfig | **false** | 0 |
| `EnemySpawnZone_Room2_North` | `Room2_North` | Objective | DA_DirectorConfig | true | 0 |
| `EnemySpawnZone_Room2_East` | `Room2_East` | Objective | DA_DirectorConfig | true | **20.0** |

## Loot containers

| Actor | Grant |
|---|---|
| `PE_R1_LootCache_A` | Ammo · Rifle · 60 |
| `PE_R1_LootCache_B` | Keycard · `OfficeFloor_BP_Door17` |
| `PE_R2_Supply_01/03/05/07` | Stim · 1 |
| `PE_R2_Supply_02/04/06` | Ammo · Rifle · 60 |

The keycard id `OfficeFloor_BP_Door17` must keep matching `BP_Door17`'s `RequiredKeycardId`
through the migration — it is the one soft-lock in the chain.
