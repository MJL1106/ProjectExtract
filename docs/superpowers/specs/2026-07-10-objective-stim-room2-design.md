# Objective Flow, Stims, and Room 2 Encounter Design

## Goal

Make DemoMap present one reliable primary gameplay objective at a time, add the optional Room 2 supply-room objective, create usable health stims in player slot 3, and author the two requested double-takedown encounters.

## Objective Flow

The primary sequence is:

1. Breach the rooftop door — ping with Middle Mouse.
2. Follow the companion downstairs.
3. Breach the stairwell door — ping with Middle Mouse.
4. Clear Room 1.
5. Find the office keycard.
6. Unlock the stairwell door.
7. Switch the companion to Stealth.
8. Perform the first double takedown on Grunt and Grunt 2.
9. Perform the second double takedown on Grunt 3 and Grunt 4.
10. Reach and interact with the extraction target.
11. Defend the position until the finite Director wave is dead.
12. Use the lift to complete the level.

A level-specific objective-flow actor owns this sequence and replaces one stable primary objective ID as steps advance. Breach steps advance when the door finishes opening, takedown steps advance after both assigned enemies die, Room 1 advances after its explicitly assigned encounter enemies die, and extraction/lift steps use their existing encounter events.

The extraction target must not register its reach objective at BeginPlay. It becomes active only when the primary flow reaches it.

## Optional Supply Objective

Room 2 adds one optional objective: “Search the side rooms for supplies — Ping with MMB, press I to loot.” It never blocks the primary flow.

Only the nearest unlooted designated crate receives the optional world marker. This prevents seven simultaneous yellow markers while keeping the next supply location discoverable. The objective completes after all seven designated crates are looted, including crates swept automatically by the companion’s existing nearby-loot behavior.

Loot interaction remains unchanged: Middle Mouse marks the loot and I sends the companion. Companion sweep behavior remains enabled.

## Stim Behavior

Stim is a new loot grant type handled by the existing loot-container and mission-inventory path.

- Each stim crate grants one stim.
- Maximum carried stims: 3.
- Pressing 3 uses one stim and restores 50 health.
- Stims never restore shield.
- At full health, pressing 3 consumes nothing.
- When already carrying 3, looting another stim reports “Stims full”; the crate still counts toward the optional objective.
- The X companion-mode picker continues consuming 1/2/3 only while open.
- Slot 3 replaces the marketplace melee-slot binding for this vertical slice.
- A Blueprint animation event is exposed but left unwired for the later animation pass.

Stim count is player-owned and server-authoritative. A focused replicated component owns the count and use operation; the world mission inventory remains the shared acquisition route for containers.

## Room 2 Content

Four stim crates are placed behind doors 20, 22, 24, and 26. Rifle-ammo crates are placed behind the shared 18/19 room, door 23, and door 25. Existing BP loot-container visuals are used without adding room props.

One takedown volume contains Grunt and Grunt 2. A second contains Grunt 3 and Grunt 4. Grunt 4 moves beside Grunt 3 and faces the same direction because their current 652 cm separation cannot read as a coherent double takedown. The two volumes do not overlap.

## HUD Behavior

The primary objective is singular. The optional supply objective is labelled separately and may coexist with it intentionally.

The world marker remains attached to its target, but screen translation is frame-rate-independent and smoothed so normal camera bob does not make the yellow marker visibly bounce. Distance text continues updating from the actual target position.

Slot 3 displays the current stim count. Existing loot notifications report stim acquisition, full inventory, successful use, and invalid full-health use where appropriate.

## Validation

- Automated tests cover stim capacity, full-health rejection, health-only restoration, authoritative consumption, objective step transitions, optional crate completion, and nearest-marker selection.
- PIE verifies both double-takedown pairs, Ping + I looting and sweep behavior, slot-3 input, marker smoothing while running, the full objective sequence, the finite defend wave, and lift completion.
- Multiplayer validation confirms only the owning player consumes their replicated stims and health changes originate on authority.

## Edge Cases

- A dead or DBNO player cannot consume a stim.
- Destroyed or already-looted crates are treated as complete for optional-progress counting.
- Dead takedown targets cannot stall the flow if they died before their step activated.
- Full stim inventory does not prevent a crate from becoming looted or the optional objective from completing.
- If a referenced objective actor is missing, the flow logs an error and does not silently skip the step.

