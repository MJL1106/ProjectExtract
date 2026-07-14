# Room 2 Gameplay Spine Design

Date: 2026-07-10  
Status: Approved in conversation; awaiting written-spec review

## Goal

Turn Room 2 into a complete playtest slice that forces players to exercise stealth, coordinated companion commands, adaptive combat, looting, defence, and mission completion.

The implementation must remain reusable outside DemoMap. Room-specific sequencing is authored with placeable actors and references rather than a monolithic Level Blueprint.

## Existing Coverage

The following requirements already exist and are not rebuilt:

- `X` opens the companion-mode picker; `1/2/3` select its modes.
- Companion double-takedown logic exists, including the shoot-takedown path.
- Objective markers and objective-completion delegates exist.
- Loot containers support ammo and keycards.
- The Enemy Director already provides adaptive tension, Build/Peak/Relief pacing, mission phases, squad compositions, spawn-zone selection, and scoped enemy counting.

Health-stim inventory/use is explicitly deferred until the purchased injection animation is available. The extraction NPC's escort/follow behaviour is also deferred.

## Gameplay Flow

1. Entering the Room 2 gate area activates the objective: **Switch companion to Stealth**.
2. `BP_Double_Door7` remains closed until the companion enters Stealth mode.
3. Selecting Stealth completes the objective and permanently unlocks the door for that playthrough.
4. Stealth-discipline volumes monitor sustained sprinting and repeated normal gunfire. Brief sprinting, accidental shots, enemy detection, and shoot takedowns do not trigger punishment.
5. Crossing the warning threshold displays a subtle **Maintain stealth** message. Continuing aggressive play activates that volume's Director punishment profile.
6. Leaving the stealth volume stops new punishment spawns. Enemies already spawned remain active.
7. Interacting with the posed extraction NPC disables further NPC interaction, changes the objective to **Defend the position**, and starts a finite adaptive wave.
8. After the configured number of Director squads have spawned and every wave member is dead, the lift interaction unlocks and the objective becomes **Use the lift**.
9. Interacting at the lift pauses gameplay and shows **Level Complete**.
10. The completion screen's **Restart Level** button reloads the current level.

## Architecture

### Reusable world interaction

Add a small world-interaction interface used by the extraction NPC and lift gate. It exposes whether interaction is currently allowed, performs the interaction, and supplies prompt text. The player's existing interaction trace checks this interface before its current loot/keycard handling.

This interface is justified by two immediate implementations and the planned extraction/lift expansion. Existing loot and door systems are not refactored into it in this pass.

### Companion-mode door gate

A placeable companion-mode gate references one `ADoorBase` actor and owns a trigger volume for objective activation. For Room 2, its target is `BP_Double_Door7` and its required mode is Stealth.

The door base gains an external-gate state checked by native breach and AI auto-open paths. The gate listens to the companion command component's mode-change event. Once the required mode is observed, it clears the door gate permanently and completes its objective.

Before implementation, inspect `BP_Double_Door7` while PIE is stopped. If the marketplace Blueprint has a direct player-open path that bypasses native `CanBreach`, route that path through the same external-gate condition without changing unrelated animation logic.

### Stealth-discipline volume

A placeable box volume measures aggressive play only while the player is inside.

Pressure sources:

- Sustained sprinting adds pressure over time after a short grace period.
- Every normal weapon shot adds pressure; repeated automatic fire therefore rises quickly.
- A shot consumed as the commit for a companion shoot takedown is exempt.

Pressure behaviour:

- Pressure decays while the player is not sprinting or firing.
- A lower threshold emits one subtle warning.
- A higher threshold activates the volume's punishment profile.
- Detection by itself does not add pressure.
- Leaving resets the volume and tells the Director to stop further punishment spawns.

The volume samples sprint state on a low-frequency timer rather than Tick. Weapon fire uses existing weapon/player delegates, extended with enough context to identify a shoot-takedown commit.

Each placed volume exposes sprint pressure, grace period, pressure per normal shot, decay rate, thresholds, warning text, and a punishment Director-config reference. Sequential volumes are supported. Overlapping active punishment volumes are rejected with a validation warning in this first version.

### Director contextual profiles

The Director keeps its existing base mission config. Temporary contextual profiles sit above it:

1. Finite extraction wave profile — highest priority.
2. Active stealth-punishment profile.
3. Base mission config.

Ending a contextual profile restores the next valid profile without overwriting the base config. This avoids the existing one-time explicit-config rules and prevents a stealth area from permanently changing the mission.

### Finite adaptive wave mode

Add a server-authoritative finite-wave request containing:

- Target squad count.
- Mission phase.
- Optional Director config override.

Wave mode reuses the existing tension sawtooth, composition selection, spawn-zone validation, squad creation, and combat seeding. A squad counts only after at least one member spawns successfully. Failed or starved spawn attempts do not consume the target count.

The Director tracks wave-spawned enemies separately from placed enemies and unrelated punishment reinforcements. Completion fires once when:

- The requested number of squads has spawned successfully; and
- Every tracked wave enemy is dead or destroyed.

The Director exposes wave-started, progress, completed, and failed/blocked events for objectives and future UI. A spawn-starvation watchdog reports the missing zone/config problem but never falsely completes the wave.

### Extraction NPC

The placeable extraction NPC is a non-damageable actor with:

- Skeletal-mesh component.
- Designer-assigned animation class or single pose/animation.
- Interaction collision.
- Objective labels.
- Finite-wave settings.
- Completion-action dropdown.
- Optional referenced completion target.

Interaction is one-shot. It starts the wave and registers for its completion event.

The completion-action dropdown supports:

- **Unlock Exit** — current Room 2 behaviour; unlocks the referenced lift gate.
- **Complete Level** — directly opens the completion screen for future encounters.
- **Broadcast Only** — exposes completion to Blueprint without built-in follow-up.

### Lift interaction gate

Do not modify the marketplace elevator implementation. Place a separate lift-interaction gate at the existing lift and reference the lift actor for objective positioning.

The located marketplace asset is `/Game/UWC_Modular_Skyscraper/Blueprints/Prefab_Parts/BP_Elevator_Lift`; related button and gate assets remain untouched.

Before wave completion, interaction is consumed and reports **Eliminate remaining enemies**. After the extraction NPC's **Unlock Exit** action, interaction triggers level completion. This actor can later be replaced or extended to drive the actual elevator without changing the Director.

### Level completion

Mission completion is server-authoritative. It:

- Pauses the game.
- Shows the Level Complete widget to each local player.
- Switches to UI input and shows the cursor.
- Prevents duplicate completion.

Restart requests return through the authoritative gameplay layer, unpause as needed, and reload the current map. The widget class is designer-assigned; C++ contains no `/Game/...` asset path.

## Multiplayer Rules

- Director, door-gate, stealth-pressure decisions, NPC interaction, wave tracking, lift unlock, and mission completion execute on authority.
- Client interaction requests are validated for target, range, and current state.
- Door unlocked state, active objective result, lift unlocked state, and completion state are replicated or delivered through authoritative client notifications.
- AI controllers remain server-only; clients observe replicated enemy/companion state.

## Failure Handling

- Repeated NPC interactions cannot start a second wave.
- Repeated lift interactions cannot complete twice.
- A missing Director config or eligible spawn zone logs a precise warning and leaves the defence objective active.
- Failed squad spawns do not count toward completion.
- Destroyed wave enemies are pruned safely through weak references.
- Leaving a stealth volume stops only that volume's future punishment spawning; committed enemies remain.
- Starting the extraction wave supersedes any active punishment profile.
- Missing door, lift, or objective references generate validation warnings before PIE where possible.

## Verification

- Enter Room 2: the Stealth objective appears and only `BP_Double_Door7` is gated.
- Select Stealth through `X -> numbered mode`: the objective completes and the door stays permanently unlocked after later mode changes.
- Brief sprint, accidental shot, enemy detection, and shoot takedown: no punishment activation.
- Sustained sprint plus repeated normal fire: one warning, then the configured punishment profile.
- Exit stealth volume: no new punishment squads; existing reinforcements remain.
- Interact with extraction NPC twice: exactly one finite wave begins.
- Adaptive wave: exactly the configured successful squad count is produced; placed enemies do not block completion.
- Spawn starvation: wave remains active and logs the blocking configuration instead of completing.
- Final wave enemy dies: lift gate unlocks and objective changes to Use the lift.
- Lift before completion: interaction reports remaining enemies and does nothing else.
- Lift after completion: Level Complete appears, gameplay freezes, and duplicate completion is ignored.
- Restart Level: current level reloads into a clean mission state.
- Multiplayer: server controls the encounter; clients see the same door, wave, lift, and completion state.

## Deferred Work

- Health-stim inventory, input, injection animation, and healing flow.
- Extraction NPC escort/follow behaviour and combat-reactive poses.
- Actual elevator travel/animation integration.
- Stealth score or grading on the completion screen.

## Expected implementation areas

Enemy Director and its data types; reusable mission/world actors; player interaction and weapon-shot context; GameMode/PlayerController completion flow; Level Complete UMG; DemoMap references for `BP_Double_Door7`, extraction NPC placement, stealth volumes, spawn zones/scopes, and lift gate.
