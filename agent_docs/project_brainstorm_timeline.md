<!-- PROJECT-TIMELINE-FULL | mirror of "Remainder of project brainstorm timeline.docx" — source of truth for scope, intent and the exact week-by-week. Keep in sync with that doc. -->

# ProjectExtract — Full Brainstorm & Timeline

The complete brainstorm and the exact week-by-week plan to follow **bit by bit**, mirrored from `Remainder of project brainstorm timeline.docx`. This file holds the full intent — the in-depth feature descriptions, the Best/OK/Worst scenario plan, and the precise weekly sequence. The concise, tickable status tracker is [project_roadmap.md](project_roadmap.md); read this file for *what each feature means* and *the order to build in*.

**Key dates:** Prototype 2 ships **Tue 7/7** · polished companion demo **~20/7** · target everything done **Sun 9/8** · NY from **Fri 31/7** (W7 worked full-time there) · sporadic buffer **10–19/8** · **hard deadline 19/8**. Dissertation draft structure due **Wed 9/7**.

Everything in the Overall list is *new* (not yet in game). Week tags are soft — the order may swap, but the core timeline holds.

---

## Overall list

### Companion
Different modes: Stealth, Normal/Explore, Combat.

#### Shared across all modes

**Core gameplay loop:**
- Not visible to enemies but must stay behind the player
- Companions weapon should naturally not get its gun shots audio picked up by the enemy ai
- No movement sounds picked up by enemies
- If enemies spot the player, must instantly break into normal combat mode

**Mechanics:**
- Need to have sequences in the level where the companion can take the lead, almost like a cinematic but not e.g. start of the mission is the top of a roof maybe the companion takes the lead at the start and follows a set placed path and then breaks off at the end of the path
- Player control system over the companion
  - Player should e.g. in stealth mode player can ping an enemy for a takedown and for a shoot take down
- Need to decide controls on this/could be scenario based maybe the player forgot to put a suppressor on their gun at the start of the mission so the takedown would need to be hand to hand takedown
- If they have suppressor they can still have the option for a takedown so it could just have the companion adapt and know if the player goes close and presses the takedown key the companion should be following close enough to do the animation too
- If its a gun takedown the companion by default will 1 tap their head. If the player misses their shot on their target then the compaion after a second and a half can assist in the head shot of the enemy
  - Player should be able to ping a door and companion can breach
- Breach should be loud, tactical, or quiet
- Loud break would be a door kickdown then the companion could go in guns blazing
- Tactical would be same as a loud except companion would kick the door down and then sort of go back behind the wall connecting to the door so it breaches the door but then kind of tactically plays with the player beore going straight in
- Quiet would be silently unlocking the door with the handle/opening the door
- Breach type is up to the player to control
  - Player should be able to control companion looting
- Scenario where there is a room to be looted e.g. cupboards, tables etc, the player should be able to ping a location the companion goes to loot, once the companion loots that location it can choose another location, if there is more, to loot. All items can go to like a shared objective/item inventory not items you can actually interact with e.g. keycard that you can then 'use' later in the mission
  - Player should be able to swap what mode the companion is in, missions can have a suggested mode e.g. stealth, but if the playr just wants to play guns blazing they can, this is where the ai director, implemented but needs proper though, would come in and help control the difficulty to make sure guns blazing on a stealth based mission isnt too op.
- There should be overall a manually placed waypoint system i can place in the level so the companion has a general idea of the path of the level, levels will be linear with side branches e.g. rooms to explore but level progress will intentionally be linear.
- Companions gun will be pre defined before the level starts, they will have infinite ammo

**Locomotion:**
- Needs slightly polishing including the companions grip stance and holding of the guns, i think can just re use the enemies animations/setup for this as they look pretty good.

#### Stealth

**Movement:**
- Crouched/slow movement behind the player
- No sprinting

**Core gameplay loop:**
- Acts based on player ping system
  - Exploring rooms
  - Killing enemies
- If enemies spot the player, must instantly break into normal combat mode
- Whisper voice lines

#### Normal
Current state for normal works well. One change for this would be not engaging first, it should only engage once the player is spotted / shot at but it should be able to recognise this. Its response time needs to be instant. Right now its like a walking combat robot and thats fine just think for normal mode it should be almost more precautionary but sit a level above stealth. So movement not so cautious, more confident, but still shouldn't be going ahead of the player whilst moving around the level.

#### Combat mode
- Aggressive, more confident, can take the lead over the player in terms of positioning
- Actively tries to gain space where space is available, shouldnt be stupidly rushing in to die but e.g. moving cover to cover should be prioritising moving forward, thinking maybe needs its own ai director like the enemies in a sense?

**Core gameplay loop:**
- Taking the lead where possible, think i will need to author/place things in the level similarly to the intro mechanism where i can place set paths for the companion to take as i will know where enemies will be positioned/whats in the next room
- If at a cover location it should take more peaks, longer peaks.

### Player
Traversal is already good main things for player is the weapon system. Just need to fix a bug where holding sprint then stopping but still holding sprint makes the players arm move as if sprinting evnethough stationary.

#### Weapon system
Goal is to have: Assault rifle, Pistol, SMG, shotgun. Player should have 2 weapons available, one primary and one pistol. Player can put attachments on the weapons that have different effects primary example of this would be load out systems in cod and battlefield.

Some missions may just lock a pistol only though.

Weapon grip by the player also needs polishing.

**Attachments:**
- Equipped before launching into the mission so in the main menu just like cod or battlefield
- Player will use the weapons and attachments from `/Game/InfimaGames/ModernGunsBundle/_Demo/Blueprints/Weapons`
- Visuals and holding states all stay using the preduralfpspack
  - Assault rifle with iron sight already working, need a skill and doc to make repeating new weapons easy, main thing is the right click ads alignment
- All attachments unlocked by default

**Ammo:**
- Ammo can be picked up from enemies via a drop chance from an enemy killed chance to drop up a pickup class. Can also be looted from rooms/lootable places
- Will be one ammo type per weapon type
- Companion can't give ammo to the player but if the companion loots a location that contains ammo it gets automatically added to the player

**Recoil:**
- Attachments can have an effect e.g. grips
- Default realistic recoil per weapon, nothing too difficult

### Enemies
- Ai director needs reviewing and testing
- Change enemy character models
- Maybe add a few attachments to their guns

### Level Design
Will purchase pre built from the marketplace e.g. a factory setup or an apartment complex. Levels will in general be quite cluttered/tighter spacing.

**Mechanics still needed:**
- As mentioned before way to author the companions route at times
- Objectives/Player director
  - Visible distance marker to tell the player where to go
  - Objectives
    - Collect item x
    - Explore room y
    - Extract person to location z
  - Not too complicated
- Hud/Minimap

**Core gameplay loop:**
- Depends on level type: Tactical level setup - reward/push player towards tactical approach such as stealth
- Start of the level will always be in a neutral part with no enemies not throwing the player instantly into combat
- Start at point A, fight way to point B. At point B there may be a neutral ai person e.g. person of interest to save or an item to extract e.g. intel laptop. Then with the item or person, extract back to point A or to point C
  - Could be an apartment building fighting from roof -> room with person -> bottom floor to extraction car or vise versa
- Level essentially split into two halves, reaching the point of interest and getting out from the point of interest.

**Difficulty:**
- Pretty consistent across levels not going for scaling difficulty

### Extraction Person
- Simple ai companion
- Hides at the back/behind the players companion
- Cant be damaged by enemies
- When shots rain in they take up a scared animation pose e.g. crouched down
- Locked to the companion
  - If companion gets downed they just freeze in place / takeup maybe a lying on the floor animation
- Once companion gets revived from downed state they can transition back into moving
- Their movement locomotion needs to be rather scared/horror game scared almost

### Cover system
Needs a re work its too static right now and not visibly flexibly to the environment. Its working from a gameplay and core mechanic but needs completely new animations.

**Goal:**
- Enemies and companion share the same system
- Animations wanted - as close to a COD or battlefield as possible from whats available
  - Shooting over a cover whilst hiding
  - Shooting around a cover whilst hiding
  - Visibly good locomotion system for crouch and standing covers
  - Peaking round a corner whilst aiming in / leaning
- Companion should not just default to always wanting to take cover when in a combat by that i mean defaulting to the locomotion system / system that enables the cover locomotion
  - It should always prefer cover but it shouldnt stop firing to then move to the cover / stop mid fight.
  - It should also be able to move and shoot around a cover as well not just locked to cover = cover locomotion
- Further depth: Move and shooting whilst standing when there is a crouch cover in front of it. If the companion is high hp and not under stress/morale suppression then it can be confident and hold last seen enemy angles instead of giving up the angle to take cover when not in danger

### UI/UX

#### Main menu
- Want to just purchase from the marketplace
- Should just be something Shooter game related

#### Class creation/editing
- Ideally from the market place
- Similar looking to a COD/Battlefield

#### In game HUD
- Weapon sketch with bullets
- HP
- Mini map

### Polish

**Voice shouts:**
- Enemy & Companion
- Companion should have a little bit of depth for voice lines

**Weapon:**
- Mag flash, sounds, reload sounds

**Music:**
- Menu music
- Maybe in game music e.g. when extraction starts / in combat

---

## Timeline

### Scenarios & tags
- **Target:** whole Overall list done by end of W7 (Sun 9/08). Hard deadline 19/08. 10–19/08 is a sporadic buffer for contingency, the dissertation paper and gameplay demo capture.
- **`[Core]`** ships in every scenario. **`[Stretch]`** flexes by scenario:

**Best case** — Everything: 4-gun arsenal, attachments and ammo economy, full cover-animation rework, all breach styles, takedown variants, full UI (menu / loadout / minimap), full audio and music.

**OK case (drop less)**
- Keep: AR + pistol + SMG, attachments, ammo economy, core cover-animation rework (skip lean / peek variants), functional menu + loadout + basic minimap.
- Drop or defer: shotgun, breach-style variants, class-creation visual polish, in-game music (to buffer).

**Worst case (drop more)**
- Keep: AR + pistol only, existing cover animations (cut the rework), one breach style, HP + ammo HUD only. Normal mode becomes a don't-engage-first flag on Combat.
- Drop: SMG, shotgun, attachments, ammo economy, cover-animation rework, breach variants, takedown variants, all UI beyond HUD, minimap, music.

**Not cuttable in any scenario** — One complete polished level (both halves), extraction person, HP + ammo HUD, all three companion modes, looting, objectives. The only level cut is no multiple level types — just one polished level.

**Biggest flex lever:** the cover-animation rework (W3–W4). Cutting it (worst case) frees about 1.5 weeks to stabilise everything else; the companion still uses cover intelligently, only the look downgrades.

---

### Week 22/06 — Foundations: level + companion control spine
- [Core] Buy marketplace level; block out the one polished level (neutral start -> A to B, rough full layout including the 2nd half).
- [Core] Companion player-control framework: ping system + follow / stay-behind (the spine every later ping builds on).
- [Core] Pre-authored route / waypoint authoring tool + author the intro cinematic route.
- [Core] Fix player sprint-arm bug (arms animate as if sprinting while stationary when sprint is still held).

### Week 29/06 — Prototype 2 build -> deliver Tuesday 7/7
- [Core] Companion-synced close-range takedown (player ping -> paired companion animation).
- [Core] Door breach v1 (one type: loud kickdown) via ping a room.
- [Core] Enemy model swap + wire a few enemy types into the level.
- [Core] Tune the 2-5 minute gameplay loop; test and finalise for delivery.

### Week 6/07 — Ship P2 (Tue), start cover rework, dissertation draft (Wed 9/7)

**This week's working tasks:**
- [Core] Deliver Prototype 2 (Tuesday 7/7) — see release objectives below.
- [Diss] Dissertation draft structure due Wed 9/7: section skeleton (intro / companion-AI design / implementation-to-date / evaluation / conclusion) pre-filled from work so far.
- [Stretch] Begin cover-animation rework (shared enemy + companion): shoot over cover, shoot around cover, crouch + stand cover locomotion.
- [Core] Enemy AI-director review + test pass.
- [Core] Companion locomotion / grip polish (reuse enemy setup).

**Prototype 2 deliverable Min Spec — Tuesday 7/7 delivered:**
- First half of a level
- Couple minutes of gameplay 2-5 minutes
- Few enemy types in use
- 1 player weapon no attachments
- Showcase a close range takedown sync of an enemy with the companion
- Showcase companion cinematic/pre authored route movement at the start of a level
- Player basic controls over the companion
  - Ping / mark a room for the companion to open
- Enemy model changes

**Depends on progress — spec extras in order of prio:**
- Cover System re work
- Ai director
- Second half of level complete
- Companion Locomotion
- Enemy weapon attachments

**Definite No for this prototype:**
- Multiple player and companion weapons
- Ui/ux
- Stealth and Combat modes for companion
- Vfx and sound effects
- Extraction Person

### Week 13/07 — Cover finish + companion combat brain
- [Stretch] Finish cover rework: peek / lean round corners, move-and-shoot around cover, hold-the-angle logic (don't give up an angle when high HP and not under pressure).
- [Core] Companion Combat mode: aggressive, takes the lead, cover-to-cover, prioritises moving forward, own combat director; predefined gun + infinite ammo.
- [Core] Companion Normal mode: doesn't engage first, only engages once player is spotted / shot at, instant reaction, confident but not ahead of the player.

### Week 20/07 — Polished companion vertical-slice demo (the dissertation demo)
- [Core] Companion Stealth mode: crouch / slow, no sprint, ping-driven, whisper voice lines + stealth rules (not seen by enemies, gun + movement sounds not picked up, instant break to combat if player spotted).
- [Core] Mode-swap + suggested-mode per mission + AI-director difficulty balancing (keeps guns-blazing on a stealth mission fair).
- [Core] Companion looting: ping a location -> companion loots -> shared objective inventory (keycards etc. used later).
- [Core] Objectives system + visible distance marker (collect item / explore room / extract person).
- [Stretch] Full breach types (tactical + quiet) on top of loud.
- [Core] Takedown variants: hand-to-hand vs gun, suppressor logic, companion miss-assist headshot after ~1.5s.
- [Core] Light game-feel polish (muzzle flash + core weapon sounds); capture demo footage.

### Week 27/07 — Arsenal + extraction person + 2nd half (home Mon-Thu, NY from Fri 31/7)
- [Core] Second half of level complete (point of interest -> extract back out).
- [Core] Extraction person (reuse companion logic: locked to companion, can't be damaged, scared pose under fire, scared locomotion, freeze when companion downed + resume on revive) ~1 day.
- [Stretch] Weapon system: AR / pistol / SMG / shotgun, 2-weapon loadout (primary + pistol), attachments + effects, per-weapon recoil, ADS-alignment skill + doc, grip polish.
- [Stretch] Ammo: enemy drop-chance pickups + room loot, one type per weapon, companion-looted ammo auto-adds to player.
- [Stretch] Enemy gun attachments.

### Week 3/08 — UI/UX, polish, integrate (full-time in NY) — target everything done by Sun 9/08
- [Core] In-game HUD: player HP + ammo (required).
- [Stretch] UI/UX: main menu (marketplace, shooter-themed), class-creation / loadout editor (COD/BF style), minimap.
- [Stretch] Full polish: voice shouts (enemy + companion, companion with depth), weapon FX + reload sounds, music (menu + combat / extraction).
- [Core] Full level both halves, full objective flow, difficulty-consistency pass.
- [Core] Integration + verify the whole Overall list is present; capture final demo footage.

### After W7 — 10–19/08 sporadic buffer (NY -> return; hard deadline 19/08)
- Contingency: absorb any slipped Stretch (in-game music, 2nd-half dressing, UI polish).
- Dissertation paper: write and finalise from captured notes + footage.
- Cut and finalise the gameplay demo from captured footage.
- Final bug-fix / stabilise pass to the 19/08 deadline.
