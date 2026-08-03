# ProjectExtract — Bark Design Bible

The single design reference for all spoken barks in the game: the **enemy squad** (13 shipped types + archetype flavour + proposed additions) and the **AI companion** (new, ~42 types). Designer-facing — triggers, tone, line sets, delivery, priority/cooldown. No implementation code; the plumbing already exists and is summarised in one paragraph below.

Supersedes the earlier `companion_barks.md` (folded in here).

---

## 1. The system, in one paragraph

A shared bark subsystem picks a random line for a game event, shows it on the subtitle feed, and enforces cooldowns plus **one voice at a time** so the world never talks over itself. **Priority** lets critical telegraphs (grenade, man down, you're-hit) cut through ambient chatter. Speakers beyond ~40 m of the player are skipped entirely. Enemy line content lives in per-archetype data assets; the companion gets its own. Everything below is content and tuning — that's all a designer touches.

## 2. Shared conventions

- **Priority:** `2` = critical telegraph that must cut through (contact, grenade, man/companion down, breach, stealth-broken, reinforcements). `1` = normal combat/among-squad calls. `0` = ambient/banter, first to be dropped.
- **Cooldowns:** reactive combat 5–8 s · movement/among-fight 8–10 s · ambient/banter 45–120 s · one-shot event calls (revive, down, area-clear, officer-down) fire once per event.
- **4–6 variants per type** so nothing repeats inside one firefight — the system random-picks.
- **Delivery tag** per line feeds the AI voice generator directly (calm / urgent / shouted / whisper / pained / cold / manic / booming). Generate at that delivery, then process. Dry, flat reads are exactly what sounds amateur.
- **Diegetic vs radio:** enemy shouts and companion combat calls are **in-world** (they're physically near you) — process for space, not radio. Reserve the radio/comms crush for actual net traffic (officer-to-squad orders, an extraction handler, the player's earpiece).

## 3. Tone by faction

- **Enemy squad** — a professional hostile force, menace scaling with archetype; a shade colder and more aggressive than the companion. Use a small stable of voices so a squad doesn't sound like one man cloned.
- **Companion** — your battle-buddy. Clipped, functional callouts; warmth shows in the down/revive lines, not in chatter volume. One designed voice.
- **The un-corny rule (the whole point):** real operator brevity. No movie-hero one-liners, no exclamation-stacking, no quips mid-gunfight. Delivery and processing sell a plain line; a "clever" line sells nothing.

---

## 4. ENEMY BARKS

### 4a. Shipped bark types (the baseline 13)

These already fire from real systems (awareness ladder, morale, squad, officer/grenadier/flank/suppress tasks). Line sets below are suggested/expanded — reconcile against whatever text is currently in the enemy `DA_*BarkSet` assets and widen to 4–6 each.

| Type | Fires when | Pri | CD | Delivery | Example lines |
|---|---|---|---|---|---|
| `HeardSomething` | Minor noise, suspicion rises | 1 | 8 | wary, low | "The hell was that?" · "You hear that?" · "Something moved." · "Stay sharp." |
| `SearchArea` | Investigating / lost the player | 1 | 10 | searching | "Check it out." · "Spread out, find 'em." · "They're around here somewhere." · "Search the area." |
| `Contact` | Player spotted, engaging | 2 | 6 | sharp | "Contact!" · "There — enemy!" · "Eyes on, engaging!" · "Target spotted!" |
| `LostTarget` | Lost line of sight | 1 | 8 | frustrated | "Lost visual!" · "Where'd he go?" · "I've lost him!" · "Anyone got eyes?" |
| `BodyFound` | Found a corpse | 2 | 12 | alarmed | "Body over here!" · "They're picking us off." · "We got a dead man — check your corners!" · "Someone's here." |
| `GrenadeOut` | Throwing a frag | 2 | 10 | loud | "Frag out!" · "Grenade!" · "Fire in the hole!" · "Cooking off — move!" |
| `ManDown` | Squadmate killed | 2 | 8 | shaken | "Man down!" · "We lost one!" · "They got him!" · "He's gone — watch it!" |
| `Pinned` | Suppressed / can't move | 1 | 8 | strained | "I'm pinned!" · "Too much fire!" · "Can't move up!" · "Keeping my head down!" |
| `FallingBack` | Morale break, retreat | 1 | 10 | urgent | "Fall back!" · "Give ground — regroup!" · "Back, back!" · "Pull out!" |
| `Flanking` | Flank manoeuvre | 1 | 10 | focused | "Going around!" · "Flanking left!" · "Cut him off!" · "Taking his side." |
| `Suppressing` | Laying suppressing fire | 1 | 8 | aggressive | "Suppressing — move up!" · "Keep him down!" · "Covering fire!" · "Pin him — go!" |
| `FocusTarget` | Officer: concentrate fire | 2 | 8 | commanding | "Focus fire — that one!" · "All guns on him!" · "Concentrate fire!" · "Take him, now!" |
| `CoveringGo` | Officer/squad bounding move | 1 | 8 | directing | "Covering — go!" · "Move, I got you!" · "Bound up, I'll cover!" · "Go, go!" |

### 4b. Archetype flavour layer

The eight `EEnemyArchetype` values. Grunt is the default pool; each specialist gets a distinct **voice** and, where noted, **flavoured variants** of shared types plus a few **archetype-only** lines. This is the cheapest way to make a squad feel varied and readable ("that's a Heavy talking, push elsewhere").

| Archetype | Voice character | Extra / flavoured barks |
|---|---|---|
| **Grunt** (+ Pistol, Shotgun loadouts) | Standard infantry, 2–3 voices for squad variety | None — the baseline pool |
| **Officer** | Authoritative, projecting | Owns `FocusTarget`/`CoveringGo`. Rally lines: "Form up on me!" · "Push them — don't let up!" · "Hold this position!" **His death is a morale event** → squad barks `OfficerDown`: "Officer's down!" · "We've lost command!" (big morale hit) |
| **Grenadier** | Methodical, deliberate | Owns `GrenadeOut`. Adds: "Flushing 'em out!" · "Danger close — move!" · "Nowhere to hide now!" |
| **Sniper** | Cold, quiet, patient — mostly silent (menace through scarcity) | Sparse, long cooldowns: "I have the shot." · "Hold still…" · "Overwatch, set." · "Relocating." |
| **Rusher** | Manic, aggressive, high energy, short cooldowns | Flavours Contact/Flank/Suppress aggressive. Adds: "Rush him!" · "Get in there!" · "No mercy!" · [war-cry] |
| **Heavy** | Booming, slow, unbothered, taunting | Flavours `Pinned` as defiance not fear: "You think this scares me?" Adds: "That all you got?" · "Come on then!" · "I'm still standing!" |

### 4c. Proposed enemy additions (cool extras)

New types that map to systems already in the game (detection ladder, director/heat, morale, cover).

| Type | Fires when | Pri | CD | Delivery | Example lines |
|---|---|---|---|---|---|
| `CallingContact` | First squad-wide alert (vs personal Contact) | 2 | 10 | rallying | "All units — contact!" · "Enemy spotted, converging!" · "We got a live one — on him!" |
| `EnemyReloading` | Enemy reloads (audible tell — lets the player read the push) | 1 | 6 | quick | "Reloading!" · "Mag out — cover me!" · "Changing mags!" |
| `PlayerDowned` | Enemy puts the player into DBNO | 2 | — | triumphant | "Target down!" · "Got him — he's down!" · "That's one for us!" |
| `Reinforcements` | Director spawns support / heat spike | 2 | 15 | driving | "Reinforcements moving in!" · "More units inbound!" · "Wave two, push up!" |
| `Wounded` | Enemy takes a heavy hit but lives | 1 | 10 | pained | "I'm hit!" · "Ah — he winged me!" · "Still up — still up!" |
| `LastMan` | Squad reduced to its last member (morale) | 2 | — | defiant or breaking | Defiant: "Just me now — come on then!" — Breaking: "No… they're all dead. All of them." |
| `CoverCallout` | Locating a player in cover | 1 | 8 | directing | "He's behind the crates!" · "Left side, in cover!" · "Dug in by the door!" |
| `FlushOut` | Rooting out a camping/hiding player | 1 | 10 | menacing | "Smoke him out!" · "Flush him!" · "Come out and fight!" |
| `SearchGivingUp` | Extended search fails → back to unaware | 1 | 15 | dismissive | "Must've been nothing." · "Clear. Back to posts." · "Jumping at shadows." |
| `Regroup` | Reform after a fall-back | 1 | 10 | reassembling | "Regroup on me!" · "Form back up!" · "Together — don't scatter!" |

---

## 5. COMPANION BARKS

New system — the companion currently says nothing. ~42 types mapped to its real surface: `ECompanionMode` (Normal/Combat/Stealth), `ECompanionCommand` (Breach/Takedown/Loot/Explore), and the revive/DBNO/health events. Group 3 is the highest-value set — it's what makes the companion read as a person, not a turret. (Give the companion its own bark-type identity, kept separate from the enemy's, so shared-name calls like "grenade" don't dedup against each other — the one implementation note that matters.)

### Group 1 — Contact & combat callouts  ·  anchor: Mode → Combat + perception

| Type | Trigger | Pri | CD | Delivery | Example lines |
|---|---|---|---|---|---|
| `ContactCombat` | First hostile / combat starts | 2 | 6 | urgent, sharp | "Contact!" · "Contact front!" · "Tangos — eyes up!" · "We got company." |
| `EnemyDirection` | Hostile bearing resolved | 1 | 5 | clipped | "Left side!" · "On the right!" · "Up high — the balcony." · "Movement, twelve o'clock." |
| `EnemyArchetype` | Spotted Sniper/Heavy/Grenadier/Rusher | 2 | 8 | warning | "Sniper — get down!" · "Heavy, don't push it." · "Grenadier — watch the arc." · "Rusher, he's coming in!" |
| `TargetDown` | Companion kill / hostile dies | 1 | 5 | controlled | "Tango down." · "One down." · "Got him." · "Clear on mine." |
| `MultipleContacts` | Several hostiles / heat spike | 2 | 10 | tense | "Multiple hostiles." · "It's a whole squad." · "More coming." |
| `LostContact` | Sight lost, lull | 1 | 8 | low, wary | "Lost 'em." · "Where'd he go…" · "Hold — I don't see 'em." |

### Group 2 — Companion's own actions  ·  anchor: combat behaviours

| Type | Trigger | Pri | CD | Delivery | Example lines |
|---|---|---|---|---|---|
| `Reloading` | Companion reloads | 1 | 6 | quick | "Reloading!" · "Cover me — mag change." · "Reloading, hold 'em." |
| `LowAmmo` | Low / dry | 1 | 12 | strained | "I'm dry!" · "Down to my last mag." · "Running low here." |
| `Repositioning` | Moves in combat | 1 | 8 | brisk | "Moving up!" · "Repositioning." · "Shifting right." |
| `TakingCover` | Enters cover | 1 | 8 | settling | "In cover." · "Holding here." · "Set." |
| `ThrowingGrenade` | Throws a frag | 2 | 10 | loud telegraph | "Frag out!" · "Grenade — get clear!" · "Cooking one, move!" |
| `Suppressing` | Lays fire | 1 | 8 | firm | "Covering you — move!" · "Suppressing!" · "On it, go!" |
| `Flanking` | Flanks | 1 | 10 | focused | "Going around." · "Taking the side." · "Flanking 'em." |
| `PushingUp` | Aggressive advance | 1 | 10 | driving | "Pushing!" · "With me!" · "Advancing — stay on me." |

### Group 3 — Support & relationship (the heart)  ·  anchor: revive / DBNO / health

| Type | Trigger | Pri | CD | Delivery | Example lines |
|---|---|---|---|---|---|
| `PlayerDownReaction` | Player enters DBNO | 2 | — | alarmed | "You're hit — hang on!" · "No — cover, I'm coming!" · "Stay down, I've got you." |
| `RevivingPlayer` | Starts reviving player | 2 | — | urgent, caring | "Hold on, I got you." · "Stay with me — almost there." · "Don't you quit on me." · "You're not dying here." |
| `PlayerRevived` | Player revive completes | 1 | — | relieved, firm | "On your feet — let's move." · "Back up. Stay in it." · "Good. Come on." |
| `CompanionDown` | Companion enters DBNO | 2 | — | pained, urgent | "I'm hit — I'm down!" · "Down! I need a hand!" · "Can't… I'm down." |
| `CompanionCallForHelp` | DBNO, awaiting revive (repeat) | 2 | 8 | strained | "Bleeding out here!" · "Any time now…" · "I can't hold this." |
| `CompanionRevived` | Player revives companion | 1 | — | grateful | "I owe you one." · "Back in the fight." · "Thanks — let's finish this." |
| `CompanionHurt` | Heavy hit / low HP | 1 | 10 | pained | "I'm hit!" · "Taking fire!" · "That one stung." |
| `Reassurance` | Sustained pressure | 1 | 25 | steady | "We got this." · "Stay with me." · "Almost through it." |

### Group 4 — Stealth  ·  anchor: Mode → Stealth, takedown, stealth-break

| Type | Trigger | Pri | CD | Delivery | Example lines |
|---|---|---|---|---|---|
| `GoingStealth` | Mode → Stealth | 1 | 15 | hushed | "Going quiet." · "Dark from here." · "Slow and quiet." |
| `StealthSpotEnemy` | Hostile seen while stealthed | 1 | 8 | whisper | "Hold — tango ahead." · "Contact, don't move." · "One up front… I see him." |
| `TakedownConfirm` | Completes a takedown | 1 | 6 | hushed | Knife: "Down. Quiet." / "Got him." — Shoot: "Tagged." / "Suppressed, down." |
| `HoldForPatrol` | Waiting out a patrol | 1 | 12 | whisper | "Hold… let 'em pass." · "Wait. Not yet." |
| `StealthBroken` | Detected, Stealth → Combat | 2 | — | sharp | "They made us!" · "So much for quiet — go loud!" · "Cover's blown, move!" |

### Group 5 — Command acknowledgments  ·  anchor: player command confirm

| Type | Trigger | Pri | CD | Delivery | Example lines |
|---|---|---|---|---|---|
| `AckBreach` | Breach command | 2 | — | matches breach type | Tactical: "Stacking up." — Loud: "On the door — breaching!" — Quiet: "Quiet entry. Ready." |
| `Breaching` | Breach executes | 2 | — | explosive / hushed | Loud: "Go, go, go!" — Quiet: "In — quiet." |
| `AckTakedown` | Takedown command | 1 | — | focused, quiet | "On it." · "Moving to him." · "I'll take him." |
| `AckLoot` | Loot command | 1 | — | matter-of-fact | "Grabbing it." · "On the loot." · "Checking it." |
| `AckExplore` | Explore / ping command | 1 | — | ready | "Checking it out." · "Moving to the mark." · "I'll sweep it." |
| `AckGeneric` | General move / regroup | 1 | 6 | copy | "Moving." · "Copy." · "On my way." · "With you." |

### Group 6 — Navigation & follow  ·  anchor: follow / catch-up / path

| Type | Trigger | Pri | CD | Delivery | Example lines |
|---|---|---|---|---|---|
| `FallingBehind` | Sprint catch-up engaged | 1 | 12 | jogging, effort | "Hang on — catching up!" · "Wait up!" · "Moving to you." |
| `Blocked` | Path blocked / can't follow | 1 | 12 | frustrated | "I'm stuck back here!" · "Can't get through — go around?" · "Blocked!" |
| `Regroup` | Task done, returning | 1 | 10 | winding down | "Coming back to you." · "Regrouping." · "Task done — on you." |
| `Following` | Downtime while following | 0 | 90 | easy | "Right behind you." · "On your six." · "Got your back." |

### Cool extras — Group 7 (world) & Group 8 (personality)

| Type | Trigger | Pri | CD | Delivery | Example lines |
|---|---|---|---|---|---|
| `ObjectiveSpotted` | Sees objective (keycard/door/terminal) | 1 | 20 | noting | "There's the objective." · "That's our way through." · "Keycard — grab it." |
| `ExtractionCallout` | Near / at extraction | 1 | 15 | urgent-hopeful | "Extraction's close — keep moving!" · "Almost out." · "That's our ride — go!" |
| `HeatRising` | Director heat spike | 1 | 20 | tightening | "It's getting hot." · "More on the way." · "They're onto us — pick it up." |
| `AreaClear` | Combat ends / room clear | 1 | 15 | exhale | "Clear." · "That's all of 'em." · "We're good — for now." |
| `PostFightBanter` | Shortly after combat | 0 | 60 | dry, breathing | "That was close." · "Still standing. Barely." · "Remind me why we took this job." |
| `ApprovePlayerKill` | Player lands a good/multi kill | 1 | 20 | approving | "Nice shot." · "Good hit." · "That's how it's done." |
| `PlayerHurtWarning` | Player HP low | 1 | 20 | concerned | "You're hurt — patch up." · "Watch yourself, you're bleeding." |
| `IdleAmbient` | Long exploration downtime | 0 | 120 | quiet, wary | "Stay sharp." · "Quiet so far." · "Doesn't feel right, this quiet." |

---

## 6. VO generation plan

- **Enemy** — generate a **stable of 3–4 base voices** for Grunts so a squad isn't one cloned man, plus a **distinct voice per specialist**: Officer authoritative, Rusher manic, Heavy booming/low, Sniper cold/quiet, Grenadier deliberate. Design each in Hume Octave or ElevenLabs Voice Design; use Respeecher speech-to-speech for the screamed Rusher war-cries and the `Wounded`/`LastMan` lines where real exertion matters. Enemy shouts are **diegetic** — process for space and effort, not radio (except Officer squad-net orders).
- **Companion** — **one** designed voice. Respeecher perform-and-swap for the Group 3 revive/down/call-for-help lines so the strain is genuine; emotional-TTS is fine for the rest.
- **Both** — generate every variant at its **Delivery** tag, then run the processing chain (in-world reverb/distance + light comms crush where diegetic-radio). Drop WAVs + subtitle text into the per-archetype enemy bark sets and the companion bark set.
- Full tool list, licensing, and processing notes: `Downloads/ProjectExtract_Sound_Sourcing_Guide.md`.

## 7. Priority interplay (how the two factions share one voice channel)

Enemy and companion are separate speakers — both can bark — but the world plays **one line at a time**. Priority decides who wins a clash:

| Situation | What should win |
|---|---|
| Enemy `GrenadeOut` (2) vs companion `Following` (0) | Grenade — the telegraph the player must hear |
| Companion `PlayerDownReaction` (2) vs enemy `Suppressing` (1) | Companion — the player's own crisis |
| Two priority-1 combat calls | First to fire; the other is dropped by the 2 s gap |
| Anything (2) vs anything (2) | Both allowed through the reduced 0.5 s priority gap — critical telegraphs are never both eaten |
| Ambient/banter (0) during any combat | Dropped — banter only surfaces in lulls |

Rule of thumb: reserve priority 2 strictly for telegraphs the player is punished for missing (incoming grenade, someone going down, breach, reinforcements). Everything else stays at 1 or 0 so those 2s always land.

---

**Coverage:** enemy = 13 shipped + ~10 proposed + archetype flavour across 6 specialists; companion = ~42 types. Roughly 350–450 individual lines at 4–6 variants each once fully written. Prioritise, in order: enemy `Contact`/`GrenadeOut`/`ManDown` (already firing, highest audibility), companion Group 3 (relationship), then archetype flavour, then the cool-extras.
