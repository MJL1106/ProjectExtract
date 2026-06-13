# AI Companion: Gameplay Design

ProjectExtract, May 2026

## Mission types

| Type | Objective | Companion's role | Fail condition |
| --- | --- | --- | --- |
| VIP rescue | Extract a named person (asset, hostage, source) | Shepherds the VIP. Stays close, guards, escorts. | Player and companion downed simultaneously |
| Intel grab | Retrieve an object (laptop, drive, sample) | Combat buddy. Loots for the player. No escort target. | Same |

The enemy wants the VIP alive (interrogation is the in-fiction reason the rescue exists), so VIPs cannot be shot by hostiles. The VIP follows the companion by default; one command flips them onto the player. If the companion goes down, the VIP cowers in place until rescued.

Levels lean linear with branching side-rooms. Vertical infiltration is the standard layout: helicopter onto a roof, fight down through floors, zipline out. Combat shifts between corridors, cover-heavy rooms, and tighter stealth sequences.

## Three modes

The companion runs in one of three modes at any moment. Mode does not change identity or capabilities, it modulates how the AI behaves.

|  | Combat | Stealth | Explore |
| --- | --- | --- | --- |
| Move speed | Sprint between cover, walk in cover | Walk only, never sprint | Walk near player |
| Fire policy | Engage any enemy in LoS | Hold unless target marked | Engage (auto-flips to Combat) |
| Audio | Full volume | Whisper subset + silent gestures | Conversational |
| Cover | Default posture, opportunistic reposition | Sticks unless undetected | Loose, near player |
| Enemy detection | Full perception | Sound only (visual ignored) | Full perception |

The stealth-invisible-to-vision rule follows The Last of Us / Ellie. A fully simulated stealth AI that enemies can spot breaks more often than it succeeds and breaks the player's flow when it does.

### Mode switching

Hybrid. Hotkey (1, 2, 3) for direct flip. Auto-suggest runs in parallel and proposes a flip via a HUD toast; the player accepts or ignores. Suggestion never force-flips.

| Trigger | Suggested mode |
| --- | --- |
| Unsilenced fire while enemies aware | Combat |
| Crouch + suppressor + no alert | Stealth |
| No enemies present for sustained period | Explore |

## Player commands

Three input layers:

- **Ping** (single key, context-aware): aim at enemy = mark target; aim at loot = mark loot; aim at ground = move-to; aim at door = breach. Mode disambiguates the same ping (Stealth ping = paired silent fire, Combat ping = focus fire on shot).
- **Hotkeys**: 1/2/3 for modes, dedicated regroup key.
- **Command wheel** (held key): commands that need a menu pick — which grenade, ask for med, suppress, breach, revive request.

| Command | Bucket | Mode | Issued via |
| --- | --- | --- | --- |
| Set Mode | Mode | Any | Hotkey |
| Mark Target | Combat / Stealth | Combat (focus), Stealth (paired) | Ping enemy |
| Mark Loot | Utility | Explore | Ping lootable |
| Move to Point | Movement | Any | Ping ground |
| Hold Position | Movement | Any | Ping current spot, or wheel |
| Regroup | Movement | Any | Hotkey |
| Focus Fire | Combat | Combat | Ping enemy |
| Suppress | Combat | Combat | Wheel |
| Use Ability | Combat | Combat | Wheel (pick grenade / flash / smoke) |
| Breach / Boost | Utility | Any | Ping door / wall |
| Med Request | Utility | Any | Wheel |
| Revive Request | Utility | Player-DBNO | Wheel or auto |

Persistence: Set Mode and Hold Position are persistent state, held until the player issues a different command. Everything else is one-shot — complete the task, fall back to default behaviour.

### The looting beat

The companion has infinite reserve ammo but cannot share from it (the fiction breaks the moment your AI buddy hands you bullets indefinitely). What it can do is loot the world: the player marks a lootable spot, the companion goes, picks up anything useful (especially ammunition matching the player's weapon), and returns to transfer it. This is the signature Explore-mode utility — gives the companion a job during downtime, makes Explore mode mechanically real instead of decorative.

## Vulnerability

Shield-over-HP, same shape as the player. HP at zero puts the companion into DBNO rather than killing it outright. Player revives via the mirror of the companion's existing RevivePlayer behaviour.

| Lever | Companion value | Reason |
| --- | --- | --- |
| Base HP | Comparable to player | Avoids bullet-sponge feel |
| Shield regen delay | Shorter than player | Forgives AI imperfection on glancing exposure |
| DBNO bleed time | Longer than player | Panic revive mid-firefight is hard but doable |
| Damage resistance | None | Honest hitbox, not magic plating |
| Threat priority | Dynamic score (below) | Companion only draws fire when it earns it |

### Threat priority

Per-enemy, per-tick score across candidate targets. Inputs:

- Distance to target
- Recency of damage taken from target
- Whether target is exposed or in cover
- Companion mode (Stealth zeros the companion's score against unalerted enemies)
- Recent teammate kills by the target

A companion in cover and quiet is largely ignored. A companion firing in the open draws fire. The companion *can* be shot; it just doesn't get gang-focused unless it has earned it.

### Mission failure

Only fail condition: player and companion downed at the same time. Team wipe, not single death. Changes the failure mode from "did your AI buddy die" to "did your team get wiped" — fairer, and pushes the player toward keeping the companion in good cover rather than ignoring them.

## Character and voice

Each mission ships with a different named operator. Voice, personality, bark set, look. No backstory the player learns in any depth, no season-long arc. Closer to a Battlefield squadmate than to Ellie — present, voiced, named, but not the centre of the game.

Per-character VO budget is roughly 30 to 50 lines across all bark buckets, which keeps the cost of adding a new operator at "record one more bank against existing systems" rather than "write a new chapter."

Stretch: persistent character recurrence with a small set of reactive lines on a second appearance ("you again, alright then").

## Communication

Two layers: barks and HUD.

### Bark catalogue

| Bucket | Examples | Combat | Stealth | Explore |
| --- | --- | --- | --- | --- |
| Threat alert | "Contact!", "Hostile!", "Tango!" | Full volume | Whisper | Conversational |
| Self-state | "Reloading!", "I'm hit!", "I'm down!" | Full volume | Whisper (urgent only) | Conversational |
| Mission-state | "Objective ahead", "VIP is moving" | Full volume | Silent (HUD only) | Conversational |
| Command response | "On it", "Holding", "Moving up" | Full volume | Whisper | Conversational |

The threat alert bucket carries **no spatial language** ("two upstairs", "RPG on the roof"). Hundreds of spatial VO lines are out of scope; the actual spatial information is on the HUD instead. Barks carry personality and atmosphere, the HUD carries position.

In Stealth, a tight subset of urgent lines uses whispered audio ("contact ahead", "ready to breach", "moving up"). The rest are silent gestures plus on-screen text — keeps the mode feeling alive rather than mute without breaking its auditory rules.

### HUD

Persistent:
- Companion HP and shield bar
- Mode indicator icon (Combat / Stealth / Explore)
- Command-state indicator ("Holding", "Marking", "Reviving", "Looting")
- Low-ammo warning

Dynamic:
- World-space markers for enemies the companion has spotted. Marker appears for several seconds when first spotted, fades, refreshes on a new sighting. This is the **load-bearing scout function** — what turns the companion into a real second pair of eyes rather than a voice in the player's ear.
- Mode-suggestion toast.

## Cover and movement

Cover is the companion's default combat posture, not a fallback. Authored cover slots in each level with peek metadata, sub-slot variants per region so the AI rotates rather than committing to one spot.

### Progression model

**Player-paced with opportunistic fallback.**

- Default: companion moves cover when the player moves. Player controls pace.
- Fallback: when current cover stops working (flanked, LoS lost, sustained fire), companion repositions on its own.

This is what makes the companion feel responsive without erratic. Fully autonomous tactical movement (flanking, pushing, retreating independently of the player) sits in stretch — adds visible-tactical depth but is a meaningful BT extension.

### Traversal

Vault, climb, mantle shared with the player through a common component. Anything the player gets, the companion gets. Drop-down for getting off ledges is the missing piece, on the polish list.

## Tutorial

First mission acts as the tutorial. No separate training level, no overlay tutorials that pause the game. Onboarding lives in the companion's dialogue, triggered on context rather than scripted sequence.

| Player learns | Companion teaches it |
| --- | --- |
| Mark a target | "Tag him with Q and we'll drop them together" |
| Mode switching | "Switching to stealth, quieten up" |
| Hold position | "Give me a sec, hold here" |
| Revive | "If I go down, you walk over and pick me back up" |
| Looting | "Mark anything useful and I'll grab it" |
| The wheel | "Hold Q if you need a med or a smoke" |

Triggers fire when the situation calls for the lesson — the player approaches a lootable spot for the first time, the player presses the ping key without aiming at anything, and so on.

## Example mission

Player + companion drop on the roof of an office block. **Explore mode**. Companion sticks near the player, glances over the railing, points at a skylight. Player crouches with a suppressor, auto-flips to **Stealth**. Through the skylight: two unaware enemies. Player aims at one and pings; companion whispers "got him" and turns to the other. Player fires, companion fires on the shot. Both drop without alerting the rest of the floor.

Corridor onward. Companion whispers "movement ahead" as it spots a patrol through a door. Player marks the patrol's path, ducks behind a desk. Companion holds alongside.

Through the door, alarm trips. **Combat** auto-suggested, player accepts. Companion to cover behind a pillar, opens fire on the closest hostile, barks out loud. Player flanks; takes a hit, drops to DBNO. Companion sprints to the player ignoring the remaining enemy, holds the revive prompt, player back in. Companion swears, enemy retreats around a corner, player chases.

VIP in the next room, hands bound. Companion's role shifts to shepherd. Glues to the VIP, picks cover positions that shield them. Player clears a final two-enemy room ahead. Companion holds back with the VIP.

Player ziplines off the roof. Companion covers from above, then jumps down behind. Mission ends.

Every system in this document is doing work during that walkthrough.

## Stretch goals

| Goal | What it adds | Cost |
| --- | --- | --- |
| Class system (stealth / aggressive / balanced) | Per-class tuning of loadout, mode bias, cover preferences | Data tuning + balancing |
| Second companion slot | Fireteam of three (player + two AI) | Significant arbitration logic |
| Walk-up silent knife kill | Stealth takedown on marked target | Anim pair + new BT task |
| Persistent character recurrence | Same operator returns across missions with reactive lines | One extra VO line per recurring character |
| Banter / downtime lines | Quiet-stretch conversation | VO budget |
| Autonomous tactical combat | Companion flank / push / retreat independently | Largest BT extension |
