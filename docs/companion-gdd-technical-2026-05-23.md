# AI Companion: Technical Design

ProjectExtract, May 2026

## Class layout

| Class / asset | Role |
| --- | --- |
| `ACompanionCharacter` | Pawn. Owns weapon actor, health, traversal, anim BP. |
| `ACompanionAIController` | Possesses the pawn, runs the BT, binds external events to BB keys. |
| `BT_Companion` / `BB_Companion` | Behaviour tree + blackboard. |
| `UHealthComponent` | HP, shield, DBNO, revive interaction. |
| `UTraversalComponent` | Vault / climb / mantle. Shared with the player. |
| `UBarkComponent` | Event-driven bark playback, mode-filtered. |
| `UCompanionTuningDataAsset` | Designer-tunable formation, mirror, warp values. |
| `UCompanionCharacterDataAsset` | Per-mission identity (mesh, voice, name, portrait, per-character overrides). |
| `UCompanionClassDataAsset` | Optional stretch — class tuning preset. |

## Behaviour tree shape

Top-level Selector, left-to-right priority:

| Priority | Branch | Decorator |
| --- | --- | --- |
| 1 | Revive Player | `PlayerDowned` |
| 2 | Mirror Player Traversal | `PlayerTraversalActive` |
| 3 | Execute Player Command | `CommandedAction != None` |
| 4 | VIP Escort | `EscortingVIP` |
| 5 | Combat | `HasEnemyTarget` |
| 6 | Follow / Formation | (default, always succeeds) |

Branch interrupts are decorator aborts (Self / Lower Priority / Both as appropriate). The aborts are what give the companion its responsiveness.

| Condition flip | Effect |
| --- | --- |
| `PlayerDowned` → true | Combat / follow / command all abort. Revive takes over. |
| `CommandedAction` set | Combat / follow abort. Command takes over. |
| `HasEnemyTarget` → false | Combat aborts. Follow resumes. |
| `EscortingVIP` → true | Combat / follow continue, but inside the escort branch's distance constraint. |
| `IsDowned` → true (own) | All non-downed branches abort. |

## Blackboard schema

| Key | Type | Set by | Read by |
| --- | --- | --- | --- |
| `PlayerActor` | Actor | AIController on possess | Revive, Follow, Command |
| `PlayerDowned` | Bool | Player HealthComponent event | Revive decorator |
| `PlayerTraversalActive` | Bool | Player TraversalComponent event | Mirror decorator |
| `PlayerTraversalObstacle` | Vector | Player TraversalComponent event | Mirror task |
| `PlayerTraversalLanding` | Vector | Player TraversalComponent event | Mirror task |
| `CommandedAction` | Enum | Player Controller (ping / wheel) | Command decorator |
| `CommandTargetActor` | Actor | Player Controller | Command tasks |
| `CommandTargetLocation` | Vector | Player Controller | Command tasks |
| `CompanionMode` | Enum | Player input / auto-suggest acceptance | All mode-aware tasks |
| `SuggestedMode` | Enum | Auto-suggest service | HUD only |
| `HasEnemyTarget` | Bool | Combat service | Combat decorator |
| `EnemyTarget` | Actor | Combat service | Combat tasks |
| `CurrentCoverSlot` | Actor | MoveToCover task | Combat sub-tree, cover-quality service |
| `CoverStillGood` | Bool | Cover-quality service | Combat sub-tree |
| `EscortingVIP` | Bool | Mission setup | VIP decorator |
| `VIPActor` | Actor | Mission setup | VIP tasks |
| `IsDowned` | Bool | Own HealthComponent | Top-level decorator |
| `TargetMarked` | Actor | Player ping in Stealth | Stealth fire gate |

## Mode system

`CompanionMode` is a single enum on the blackboard, read at the point of decision rather than branched on at the top of the BT. Adding a new mode-aware behaviour means updating one task, not restructuring the tree.

Per-task modulation:

| Behaviour | Combat | Stealth | Explore |
| --- | --- | --- | --- |
| Move speed | Sprint between cover, walk in cover | Walk only | Walk near player |
| Fire policy | Any enemy in LoS | Hold unless `TargetMarked` | Any enemy in LoS (auto-flips to Combat) |
| Audio policy | Full bark bank | Whisper subset only | Conversational subset |
| Cover policy | Default; opportunistic reposition | Sticky unless undetected | Loose; near player |
| Enemy perception | Standard | Visual ignored, sound only | Standard |

### Auto-suggest

Lives on the player controller. Watches gameplay events, writes `SuggestedMode` (not `CompanionMode`). HUD reads `SuggestedMode` and shows a toast. Player acceptance writes the mode change to `CompanionMode`. Suggestion never bypasses the player.

| Trigger | Suggested mode |
| --- | --- |
| Unsilenced fire while enemies aware | Combat |
| Crouch + suppressor + no enemies alerted | Stealth |
| No enemies present for sustained period | Explore |

## Command surface

Three input methods routed to the same blackboard keys.

- **Ping** — camera trace from player controller, resolves to a `CommandedAction` based on what the trace hit and the current mode.
- **Hotkeys** — mode set (1/2/3) and regroup (`G` or similar).
- **Wheel** — held key, radial menu for commands needing a menu pick.

### Ping resolution

| Trace hit | Combat / Explore | Stealth |
| --- | --- | --- |
| Enemy actor | Focus Fire | Mark Target (paired silent fire on player's shot) |
| Lootable actor | Mark Loot | Mark Loot |
| Ground | Move to Point | Move to Point |
| Door / interactable | Breach | Breach |
| Nothing in range | Hold Position at cursor | Hold Position at cursor |

### Persistence

- **Persistent state**: `SetMode`, `HoldPosition`. Held until cleared by a new command.
- **One-shot**: everything else. Complete, clear `CommandedAction`, fall through to lower-priority branches.

The command branch is a Selector of children, each gated on a specific `CommandedAction` value.

## Cover system

Authored. `CoverSlot` actors carry peek metadata. `CoverRegion` actors group slots so the AI rotates between sub-slots within a region rather than committing to one spot for a whole engagement.

### Cover selection EQS

`EQS_Companion_Cover` query, generated against slots in radius around the player. Weighted tests:

| Test | Weight | Effect |
| --- | --- | --- |
| LoS to enemy | High | Slot useless without it |
| Distance to player | Medium | Penalises too-far slots |
| Peek direction match | High | Authored peek faces the threat |
| Not in teammate fire arc | High | Avoids being shot in the back |
| Slot height vs threat | Medium | Reject cover too short to fire over |
| Distance from current slot | Low | Mild stickiness |

Highest score wins.

### Cover-still-good check

Service running ~1 Hz re-evaluates the current slot against the same tests. Score drop below threshold flips `CoverStillGood` to false; Combat sub-tree picks a new slot. This is the engine behind the opportunistic-reposition behaviour described in the gameplay overview.

### Safety nets

- **Watchdog**: timeout on path-to-slot. Picks a different slot or warps on expiry.
- **Reload gate**: no fresh peek cycle while reloading.

## VIP escort

Branch slotted between command and combat priority. Active only when `EscortingVIP` is set (mission setup writes it at start).

- **Default**: companion holds within configurable distance of `VIPActor`. Cover EQS adds a "distance to VIP" weighted test on top of the standard cover scoring.
- **VIP follow leader** is the companion. A "Stay With Me" command from the player flips the VIP's leader to the player; reverse flips back. Single boolean swap, no new behaviour.
- **Companion-downed → VIP cower**: HealthComponent fires `CompanionDowned` event to GameState. VIP switches to cower-in-place idle. State clears on companion revive.
- **Mission failure** held on the GameMode. Listens for both player-DBNO and companion-DBNO events. Concurrent = checkpoint reload.

## Vulnerability

HealthComponent state machine:

| State | Entered when | Behaviour |
| --- | --- | --- |
| Alive | Spawn / Revived | Standard combat |
| ShieldBroken | Shield at 0 | HP takes damage. Shield-regen recovery timer. |
| Downed | HP at 0 | Combat / command branches abort via `IsDowned`. Bleed timer + revive interaction exposed. |
| Dead | Bled out without revive | Only the player normally reaches this. Mission failure path. |

Damage routes through `TakeDamage`: shield first, then HP. Revive task is shared between player and companion via a `RevivingActor` key the target's HealthComponent watches.

## Threat priority

Per-enemy, per-tick perception service. Scores candidate targets:

| Input | Weight | Effect |
| --- | --- | --- |
| Distance to target | High | Closer = higher |
| Recency of damage from target | High | Target who just shot us = higher |
| Target exposed vs in cover | Medium | Exposed = higher |
| Companion in Stealth + no alert | Override | Zeros companion's score |
| Target killed teammate recently | Medium | Bumps |

Enemies engage the highest scorer. The Stealth override implements the "invisible to vision" rule from the gameplay overview — the companion can still be heard, but can't be scored on visual perception alone.

## Class / character system

DataAsset-driven. No code change per character.

### `CompanionCharacterDataAsset` (per-mission)

- Skeletal mesh
- Animation blueprint
- Voice bank (bark audio)
- Display name + portrait
- Per-character overrides (accuracy ramp, reaction time, etc.)

### `CompanionClassDataAsset` (stretch)

- Default loadout (weapon class, ammunition type, abilities)
- HP / shield baselines
- Accuracy ramps
- Cover preferences (a stealth class biases against noisy positions like windows)
- Default mode bias

The class is an additional layer; player commands still override class defaults at runtime.

`CompanionTuningDataAsset` already exists and holds formation, mirror, and warp values. Character + class assets are the next layer up.

Adding a new companion = one new character asset, optionally a new class asset. Nothing else changes.

## Communication

### Bark pipeline

Event source → `UBarkComponent` → bucket lookup → mode-filtered set → random pick → play.

| Event source | Bucket |
| --- | --- |
| Perception OnEnemySpotted | Threat alert |
| Weapon OnReloadStart | Self-state |
| HealthComponent OnDowned | Self-state |
| AIController OnCommandReceived | Command response |
| Mission OnObjectiveUpdated | Mission-state |

If the mode-filtered set is empty (no whisper variant of this line exists for stealth, say), no audio plays. HUD layer can still surface the event silently.

### Enemy-spotted marker

Separate from barks. Perception broadcasts a spotted-target event through the AIController; HUD draws a world-space marker pinned to the enemy. Fades after a few seconds, refreshes on new sighting. **Load-bearing scout function** — barks are flavour, this carries the actual position information.

### HUD widgets

Delegate-bound, no per-frame polling.

| Widget | Bound to |
| --- | --- |
| HP / Shield bar | HealthComponent `OnHealthChanged` |
| Mode indicator | `CompanionMode` (replicated to player) |
| Command-state indicator | `CommandedAction` |
| Low-ammo warning | Weapon `OnLowAmmo` |
| Mode-suggestion toast | `SuggestedMode` (replicated) |

## Stretch goal mapping

| Stretch | Architecture cost |
| --- | --- |
| Class system | Add `CompanionClassDataAsset`, route through existing tuning hooks. Data only. |
| Second companion slot | `TeamRoster` array on GameState. Per-companion AIController + BT instance. Arbitration logic for command dispatch and revive priority. High. |
| Walk-up silent knife kill | New BT task gated by `TargetMarked && CompanionMode == Stealth && Distance < Threshold`. Animation pair (companion + victim). Low-medium. |
| Persistent character recurrence | Save-game flag, per-character reunion bark line. Low. |
| Banter / downtime lines | New bucket in bark bank, timer service in Explore mode. Low. |
| Autonomous tactical combat | Tactical decision service in Combat sub-tree. New flank / push / retreat sub-trees. Cover system already supplies the destinations. High. |

## Build state

Prototype runs end-to-end: follow, combat, cover (authored slots + peek matrix + walk-approach), traversal (mirror + warp safety nets + falling locomotion), revive. Recent work since the last status doc: reload gates, LoS-gated targeting, watchdog timers, mesh-attached weapon, HP widget anchor.

### Outstanding implementation, in order

| # | System | Notes |
| --- | --- | --- |
| 1 | Cover progression | Inter-slot movement triggered by player movement |
| 2 | Mode system | `CompanionMode` BB key + auto-suggest service on player controller |
| 3 | Command surface | Ping resolution, wheel, BT command branch |
| 4 | Companion DBNO | Mirror of player DBNO in HealthComponent |
| 5 | Player-side revive interaction | Walk-up + hold-key flow |
| 6 | VIP mission setup | Escort branch, VIP cower state, fail-on-team-wipe check |
| 7 | HUD layer | Markers, indicators, command-state widget |
| 8 | Bark system | `UBarkComponent` + first character's VO |
| 9 | Tutorial integration | First mission's in-fiction lesson triggers |

Each is independent against the existing prototype.
