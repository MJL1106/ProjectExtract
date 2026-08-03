# DemoMap migration plan — legacy flow → `AObjectiveStep` chain

Beat-by-beat authoring spec, derived from `objective_flow_migration_source.md` (the captured live
wiring) and the `ELevelObjectiveStep` / `ELevelObjectiveEvent` transition table. Execute in one
pass — see the single-swap constraint at the bottom.

Prerequisite: a **verified green build**. The round-4 patch has never been compiled.

## Main chain — 12 placed `AObjectiveStep` actors

`NextStep` points at the row below. Every step's `Label` is the HUD line. Only step 1 has
`bIsEntryStep = true`.

| # | Label | Condition | Payload | Side effects |
|---|---|---|---|---|
| 1 | Breach the roof door | `DoorOpened` | `TrackedDoor` = `BP_BreachableDoor` | **OnActivate** `SetExtracteeRescuable` → `ExtracteeCompanion_Room2`, `false` |
| 2 | Follow your partner | `RouteCompleted` | `TrackedRoute` = `CompanionRoute` | **OnActivate** `CommandCompanionRoute` → `CompanionRoute` |
| 3 | Breach the stairwell door | `DoorOpened` | `TrackedDoor` = `BP_Door27` | — |
| 4 | Clear the room | `EnemiesDead` | `TrackedEnemies` = the 7 `PE_R1_Grunt_*` | — |
| 5 | Find the office keycard | `ContainerLooted` | `TrackedContainers` = [`PE_R1_LootCache_B`] | **OnActivate** `ActivateActor` → step S1 (optional supplies) |
| 6 | Unlock the stairwell door | `DoorOpened` | `TrackedDoor` = `BP_Door17` | — |
| 7 | Switch your partner to stealth | `DoorOpened` | `TrackedDoor` = `BP_Double_Door7` | **OnActivate** `SetCompanionMode` → `Stealth` |
| 8 | Take them down together | `EnemiesDead` | `TrackedEnemies` = [`BP_Enemy_Grunt`, `BP_Enemy_Grunt2`] | — |
| 9 | Take them down together | `EnemiesDead` | `TrackedEnemies` = [`BP_Enemy_Grunt3`, `BP_Enemy_Grunt4`] | — |
| 10 | Reach the hostage | `ExtracteeRescued` | `TrackedExtractee` = `ExtracteeCompanion_Room2` | **OnActivate** `SetExtracteeRescuable` → `ExtracteeCompanion_Room2`, `true` |
| 11 | Defend the position | `WaveCompleted` | `WatchedWaveId` = `Room2Defence` | **OnActivate** `StartDirectorWave` (below) · **OnComplete** `UnlockGate` → `LevelCompletionLiftGate_Room2` |
| 12 | Take the lift out | `Interacted` | `TrackedInteractable` = `LevelCompletionLiftGate_Room2` | — (`NextStep` empty — chain ends) |

### Checkpoints
| Step | `bIsCheckpoint` | `CheckpointSpawn` |
|---|---|---|
| 5 · Find the office keycard | true | `TP_Checkpoint_FIND_OFFICE_KEYCARD` |
| 10 · Reach the hostage | true | `TP_Checkpoint_REACH_EXTRACTION_TARGET` |

### Step 11's wave request — copy verbatim
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

Step 11 also needs **`bFreezeMarkerAtActivation` = true** with **`MarkerTarget` left empty**. Do not
wire the VIP — he walks away from the ground the player is told to hold, which is the exact bug the
legacy `DefendAreaLocation` capture existed to fix.

## Optional supplies — 1 placed actor, parallel side chain

| Field | Value |
|---|---|
| Label | Gather supplies |
| `bIsEntryStep` | false (stood up by step 5's `ActivateActor`) |
| Condition | `ContainerLooted` |
| `TrackedContainers` | `PE_R2_Supply_01` … `PE_R2_Supply_07` |
| `bRequiresAllContainers` | true |
| `bShowWorldMarker` | **false** — text-only, no billboard, no edge indicator |
| `NextStep` | empty |

The marker re-points to the nearest unlooted crate automatically; that is the built-in
`ContainerLooted` derived anchor, so leave `MarkerTarget` empty.

## Marker wiring — mostly nothing to do

Leave `MarkerTarget` empty on every step. Precedence resolves it: derived anchors first
(step 4's enemy-group centroid, the supplies chain's nearest-unlooted crate), then the condition's
payload actor (steps 1/3/6/7 → their door, 10 → the extractee, 12 → the lift gate), then the step
actor's own location. Only step 11 overrides, via the freeze flag.

Step 4 keeps the legacy `Room1AreaLocation` behaviour for free — the centroid is captured at
activation, so the marker holds the room instead of chasing the last living grunt. Set
`AreaAnchorOverride` only if the auto-centroid reads wrong.

## Deletions — same change, not a follow-up

1. `LevelObjectiveFlow_DemoMap` — the legacy flow actor.
2. The downstairs `CompanionRouteTrigger` — step 2 now issues the route itself. Leaving it is not
   fatal (`StartRoute` refuses the second call) but whichever fires first wins, and the trigger
   fires on player position rather than on the beat.

## Why this must be one swap

Both systems wired to the same doors, enemies and containers drive each other:

- The step chain's checkpoint fast-forward calls `ForceOpenInstant()` on tracked doors, which
  **does** broadcast `OnDoorOpened` — driving the legacy flow's `Advance()` through arbitrary events.
- The fast-forward destroys tracked enemies. Legacy `IsEnemyGroupDead` requires a valid health
  component reporting dead, so a destroyed-but-never-killed enemy satisfies neither branch and the
  legacy `ClearRoom1` / takedown steps can then never complete.
- `MarkLootedForCheckpoint` is silent, but legacy's late-entry catch-up reads
  `KeycardContainer->IsLooted()` directly, so it still advances off the new system's state.

## Post-migration verification

1. Level loads with exactly one active objective (step 1) and no audit warnings in the log.
2. Walk the chain start to finish; each beat's marker points where the old one did.
3. Kill the level mid-run and restart from each of the two checkpoints — confirm doors are open,
   cleared enemies are gone, the keycard is granted, the supplies objective is still present, and
   **no wave starts at level load**.
4. Confirm the lift unlocks only after the wave completes.
5. `AuditWaveOrphans` should stay silent — step 11 starts and watches the same wave, which is the
   shape it checks for.
