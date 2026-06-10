# EXTRACTION — Enemy AI: How It Plays

**Plain-language overview.** Same systems as `enemy_gameplay_as_built.md`, minus the property names and decimal points. Numbers are rounded and converted to metres. When you need the exact value, the as-built doc has it; when you need to know what to fix, `enemy_gaps_and_setup.pdf` has that.

A few things described here have working logic but **no visuals wired yet** (sniper laser, grenade warning ring, proper meshes). Those are tagged **[not wired yet]** so you know what you will and won't see in the first playtest.

---

## 1. How enemies notice you

Every enemy keeps a private **suspicion meter** about you, 0 to 100. Seeing you fills it. Hearing you bumps it. Losing track of you drains it. Combat starts at 100 — or instantly if you get too close or shoot them.

**What fills the meter when they can see you:**

- Distance — right in their face fills it fast, at the edge of their ~25m vision it crawls.
- Where you are in their view — centre of their vision is 3× worse for you than the corner of their eye.
- How you're moving — sprinting is 3× louder *visually* than creeping. Standing still is the stealthiest thing you can do.
- Stance — crouching halves how fast they make you.

**Play it out:** you're 12m from a guard, in the open, walking. He clocks you in about 1.5 seconds ("Did you hear that?" — he stops and stares), is actively coming to look in 3, and opens fire at 5. Sprint instead and you've cut that to 3 seconds total. Now do it crouched, slow, at 20m, off the centre of his vision: you have something like half a minute — and if you slip behind cover before the meter fills, it drains at a steady rate and he goes back to his route like nothing happened.

**Two instant-combat rules, no meter:** get within ~3.5m of an alert enemy's eyeline, or put a bullet in anyone — the victim immediately knows exactly who did it, even if he never saw or heard you.

**What they hear:**

| You do this | Heard within | Effect on one guard |
|---|---|---|
| Fire an unsuppressed shot | 30m | A third of the way to investigating — three shots and he's coming |
| Sprint | 12m | Same bump per footstep — sprinting past a doorway is loud |
| Walk | 4m | Minor |
| Crouch-move | 1.5m | Basically silent |
| Reload | 6m | Minor tell |

Sound never *confirms* you — it tops out just below combat. A guard who only ever hears you will come looking, weapon up, but he doesn't know it's hostile until he sees you. That's the design rule that makes noise a pressure tool instead of an instant alarm.

**Takedowns:** an enemy that hasn't noticed anything (fully Unaware, white-meter) can be silently killed from behind — within arm's reach, inside his rear 120° arc. No sound, no alert. But leave the body somewhere visible and the next guard who *sees* it goes straight to searching and tells the level about it.

**The level has a mood: Calm → Searching → Loud.** One enemy reaching "actively searching" puts the whole level on Searching. Any confirmed combat sighting — or a *second* body discovered — tips it to **Loud**, permanently. Loud is the big switch: every dormant guard wakes up and sweeps his post, and the director (§7) starts sending reinforcements. Your stealth run lasts exactly as long as you keep that switch unflipped.

---

## 2. When the fight starts

Enemies are deliberately not aimbots. Three mercies are built in, and they're your openings:

1. **Reaction pause** — roughly half a second between spotting you and firing (the rusher and sniper are quicker).
2. **First-burst grace** — their first burst is ~5× less accurate than their settled aim. It takes about 2 seconds of staring at you for their aim to tighten. **The first burst is your free move.**
3. **Movement tax** — if you're moving fast, everyone's aim gets meaningfully worse. Standing still in a firefight is how you die.

**The rhythm you'll fight against:** an enemy in cover pops up for about a second, fires a burst (4–12 rounds at 25 damage each), drops back down for one to two seconds, repeats. They claim cover spots like musical chairs — never two on the same piece, squadmates keeping ~3m apart — so a squad naturally forms a spread firing line instead of a conga.

**Who they shoot at:** each enemy weighs you against the companion — who's closer, who's visible, who hurt them in the last few seconds. Recent damage is the heaviest factor, and there's a stickiness so they don't ping-pong between targets.

**Play it out:** you're pinned by two grunts. The companion hoses one of them — that grunt's "who hurt me" math flips, he turns his fire on the companion, and the second one is now peeking at a 1-second rhythm you can time. Swing wide while they're both busy and you're shooting two side-on targets.

---

## 3. Getting their heads down — suppression

You don't have to hit enemies to control them. Rounds passing within about **1.5m** of an enemy rattle him — two close rounds and he's *suppressed* for a second or two: he stops peeking entirely, hugs his cover, and any return fire is wild. Keep the stream going and he stays down.

- The **sniper** is rattled by a *single* near miss.
- The **heavy** needs about six — he genuinely doesn't care.
- Suppression also cancels flanking runs and sniper shots mid-aim.

This is the companion's showcase: "suppress that one", head goes down, you cross the open ground. It also works in reverse — the heavy's whole job is doing this to you and your companion (the companion can be suppressed; you never are — your discipline is your own).

---

## 4. Fear — morale

Under the suspicion meter sits a second invisible meter: **confidence**, 0–100. You never see it; you hear and watch it.

Things that drain it: a squadmate dying nearby (noticeable), being pinned by suppression (steady drain), dropping under a third health (big one-time hit), and being shot from a direction they aren't facing — being flanked bleeds confidence *per second*, which is why getting behind enemies collapses fights.

Things that restore it: hitting you, downing you, time without bad news, and the officer's rally.

**Three visible states:**

- **Confident** — pushes, peeks often, holds angles.
- **Shaken** (below ~60) — same behaviours, less brave timing.
- **Broken** (below ~30) — "Falling back!" — abandons his cover for the *deepest* cover he can find, crouches, and only risks a wild half-second peek every 5–10 seconds. Suppress a broken enemy and he won't peek at all.

**The keystone:** when the **officer** dies, every normal enemy nearby takes a hit so large it breaks them *instantly*. One trigger pull turns a coordinated squad into individuals hiding behind furniture. The heavy shrugs it off (he anchors the wreckage), and the rusher and shield-bearer are *fearless* — they ignore morale completely, which is exactly what makes them scary when everyone else is cowering.

Nobody ever surrenders or runs off the map. Broken means turtled, not gone — the pressure never fully stops, it just goes quiet and defensive.

---

## 5. The seven enemies

All seven share the brain above. What differs is the body, the numbers, and one signature move each.

### Grunt — the yardstick
**100 HP · normal speed · fights at 6–18m.**
The baseline soldier: takes cover, peeks, bursts, flanks when the squad allows it, panics when things go wrong. Every other enemy is "a grunt, except…". He's also the only archetype that runs the officer's pin-and-bound plays.
**Beat him:** anything works — he's the tutorial. Two headshots from a 50-damage weapon, or suppress-and-flank.

### Rusher — punishes camping
**60 HP · very fast (6 m/s) · fearless · melee.**
The moment he knows where you are he sprints straight at you, spraying wildly on the move (accuracy is terrible by design — the threat is his *legs*), and at arm's reach he stops shooting and starts hitting you for 35 a swing every 1.5 seconds. Ignores cover, ignores fear, can't be meaningfully suppressed.
**Play it out:** you're comfortable behind a crate, working the peek rhythm — then the squad's rusher commits. You have roughly two and a half seconds of him crossing open ground. Drop him in that window (60 HP — one good burst) or he's inside your cover with you.
**Beat him:** never let him arrive. Burst him down, let the companion peel him, or backpedal through a door he has to funnel through.

### Heavy — the wall
**250 HP · slow (2.5 m/s) · frontal armour · turns like a tank · suppression-proof.**
Anchors in the open — he doesn't take cover, he *is* cover for his squad. Fires long 2–3.5 second hoses, and when you duck he keeps pouring fire into your cover, keeping you (and your companion's nerves) pinned. His front plates eat 70% of incoming damage until you've cracked all three; his back takes *bonus* damage; his head was never armoured.
The catch: he turns at about a quarter-rotation per second and physically can't shoot more than 60° off his facing. Strafe at close range and you're literally outrunning his gun. Getting behind him also bleeds his confidence — the one thing that dents him emotionally.
**Beat him:** circle, shoot the back (1.5×) or head (2×, no armour), or grenade him. Never duel his front plate — that's 470+ rifle rounds of raw damage. From behind it's about a third of that.

### Sniper — owns the long sightline
**70 HP · sees 40m (everyone else 25) · laser telegraph · jumpy.**
Finds the farthest standing-cover perch with an angle on you, paints you with a laser for a full **2 seconds**, fires one shot, then 4 seconds of cooldown. After two shots — or the *instant* he's hit or a single round cracks near him — he abandons the perch and moves to a new one. Bravest enemy in the game while hidden, most fragile the moment pressure arrives: one near miss breaks his aim, and morale-wise he shatters at the first bad news.
**Play it out:** crossing a courtyard, a red line sweeps onto your chest. Two full seconds. You can break line of sight, or yell for suppression — one companion round near his perch and the laser dies mid-aim. Then he's *moving*, and a moving 70 HP sniper in the open is a free kill.
**Beat him:** respect the laser, never the man.
**[not wired yet]** The laser's visual effect and his proper rifle damage — right now the beam is invisible and the shot stings instead of wounds. Both are top of the fix list; treat sniper encounters as untestable until they're in.

### Officer — the force multiplier
**100 HP · stays behind his men · the priority target.**
Personally just a grunt with a pistol-grip attitude. His value is everyone else: allies within 15m of him shoot **25% tighter**; every 10 seconds he designates a focus target ("Focus that one!") and the squad converges its fire; when squadmates break he rallies them back into the fight (once per ~25s); and only officer-led squads run the pin-and-bound maneuver (§6). He deliberately repositions every 3 seconds to stay *behind* the squad's centre of mass, away from you.
**Play it out:** a fight that feels strangely hard — their shots keep clipping you, and whenever you wound someone the whole squad seems to switch to you at once. That's an officer somewhere behind the line. Kill him and you can *hear* the fight break: "Falling back!" from three directions at once, the line dissolving into individuals.
**Beat him:** the puzzle is reach, not damage. Flank wide enough to see behind the line, or punch a hole with suppression and take the snap shot. Highest-value kill in the game.

### Grenadier — the flush
**100 HP · grunt with 3 grenades · throws at your cover, not at you.**
While he can see you he's an ordinary grunt. The moment your cover blocks his view, he starts counting. At **4 seconds** of you staying hidden: "Grenade out!", a 1-second wind-up, the lob — aimed at *where he last saw you* — and a 2.5 second fuse after landing. All told roughly four seconds of warning to be somewhere else. The blast is big (3.5m radius, 80 at the centre) and hurts *everyone*, his own squad included. He carries three, minimum 12 seconds apart, and won't throw at point-blank or past 20m.
**Play it out:** the cover that's kept you safe for the whole fight suddenly has a red ring on it. The grenade isn't trying to kill you — it's trying to make you *stand up* in front of his squad. Moving early, on your terms, is the counter; moving when the ring appears is the panic version; not moving is the wrong answer.
**Beat him:** never camp one spot past a few seconds when a grenadier's alive, or kill him early — he's grunt-soft.
**[not wired yet]** The warning ring visual and the "Grenade out!" line — currently the only telegraph is his 1-second arm wind-up animation, which also isn't hooked up. Fix before judging him.

### Shield — walks you down
**120 HP behind a 400 HP shield · slow methodical advance (2.2 m/s) · fearless.**
A walking wall that closes distance. The shield physically blocks bullets — not a damage reduction, an actual object your rounds hit instead of him. Every 2.5 seconds he stops, leans his sidearm out for a sloppy half-second burst, and resumes. He never panics, never retreats, never reconsiders.
The shield isn't forever: 400 damage breaks it, and grenades are brutal against it — a single grenade strips half the plate *and* hurts the man holding it. Once it pops he's a slightly tough grunt with bad aim, and (his quiet weakness) he *stays* fearless while suddenly having no reason to be.
**Beat him:** three doors — flank (anything that hits the body works normally), grenade (double damage to the plate, full to him), or focus fire through the plate. The pincer with your companion is the intended play: he can't face both of you.
**[not wired yet]** The shield mesh is a placeholder cube sitting *on* him, which currently blocks shots from **every** direction. Until it's replaced and offset forward, flanking him doesn't work and he's accidentally immortal. Known, top-priority fix.

---

## 6. Fighting a squad

Enemies placed with a squad ID (and everything the director spawns) fight as a unit. What that looks like from your side:

- **They share eyes.** One enemy confirms you, and within a second the whole squad is converging on that spot — not magically knowing where you *are*, but knowing where you *were*. Break contact and move, and their picture goes stale.
- **They spread.** No clumping, no shared cover — a squad arranges itself into a firing line with gaps.
- **One flanker at a time.** Periodically one member peels off and runs a wide arc toward your blind side, announcing it ("Flanking!" — squad rule: only one may try at once, with a cooldown). Wound or suppress the flanker and he aborts back to cover.
- **They focus fire.** Leaderless squads pile onto whoever hurt them first; officer squads pile onto whoever he says.
- **Self-preservation always wins.** A flanker who gets lit up abandons the plan. A broken enemy ignores every order and turtles. Squads never march a man into fire just because the plan said so.

**The pin-and-bound (officer squads only):** in a long sightline fight, one grunt opens up with deliberately long suppressing bursts on your position — 3 to 5 seconds at a time, "Suppressing!" — while a second grunt advances toward you in 7-8m sprints, *only moving while the covering fire is actually live*. When the runner arrives at his next position they swap jobs ("Covering — go!") and leapfrog again. It's the most dangerous thing enemies do, and it's deliberately fragile: kill or suppress the *shooter* and the runner freezes mid-advance; kill the officer and the whole behaviour is gone for the rest of the fight; the shooter reloading pauses the advance until his mag is back.

**Worth knowing for setup:** placed enemies with no squad ID get *none* of this — no shared sightings, no flanks, no focus fire. They'll look "dumb" in a way that's a level-setup problem, not an AI problem.

---

## 7. The director — who decides when it gets worse

While the level is Calm, nothing spawns, ever — the stealth phase is exactly the force the level designer placed. From the moment it goes **Loud**, a director starts watching your fight and managing pressure like a horror-film editor: build, peak, *breathe*, build again.

It keeps a **tension score** — your lost health, your kills, how many enemies are actively engaging you. Tension high enough? It stops sending anyone (a fight at its peak is never made bigger). Fight dies down? After the tension drains, you get a guaranteed **~25 seconds of genuine quiet** — loot, reload, revive, reposition — before the next wave is even considered. Reinforcements only ever arrive during the build-up, from spawn rooms 15–45m away that you can't currently see, and they arrive *as a squad, already heading toward your last known position*.

What arrives depends on the mission phase (set by level triggers):

- **Infiltration** — token response: a pair of grunts every ~45 seconds, hard cap of 8 enemies alive. (The cap counts the *placed* force too — a stealth level with 8 guards still alive gets no reinforcements at all until you thin it.)
- **Objective** — a real fight every ~25 seconds, cap 12: grunt teams, rusher-tipped pairs to crack a camper, officer-led squads, a grenadier package when it wants you moving.
- **Extraction** — the crescendo, every ~15 seconds, cap 20: heavy-plus-officer pushes, triple-rusher waves, shield walls, sniper overwatch teams. Spawning doesn't stop because you're leaving — the route *behind* you repopulates too.

The director never conjures the perfect counter — composition is weighted dice, not a mind-reader. It adapts *when*, not *what*.

---

## 8. A full mission, start to finish

**Calm.** Guards walk their routes. You crouch past the courtyard sentry at 20m — his meter twitches, never fills. The straggler at the gate dies silently from behind. His patrol partner finds the body two minutes later: "Man down!", he sweeps the area, the level mood ticks up. One more body found anywhere and it's all over.

**It goes Loud anyway** (it always does — a sightline you didn't respect). "Contact!" Every dormant guard in the level wakes up and sweeps his post. The squad that spotted you fans out across cover, keeps 3m spacing, and a flanker swings left — your companion's burst cracks past his ear and he thinks better of it. Forty seconds later the first reinforcement pair jogs in from a stairwell you can't see, already heading for the spot you were last seen.

**The objective fight.** An officer squad arrives and the fight changes texture — their shots group tighter, they all switch targets together, and when you break two of them the officer rallies them back into the fight. You learn the lesson the game is teaching: *shoot the man behind the line first.* When he drops, the squad audibly shatters. A grenadier flushes you off your favourite crate at the four-second mark; the heavy that walked in with him doesn't care about the grenade, your suppression, or his dead friends — you finally circle him and put six rounds in his backplate.

**Peak, then quiet.** The director watched all of that — and through the worst of it sent nothing. Now, fight over, twenty-five seconds of silence. Reload. Revive. Move.

**Extraction.** Every fifteen seconds the world gets worse: a shield wall grinding up the corridor, rushers taking the side route, a sniper team setting up overwatch on the courtyard you have to cross — laser, duck, companion suppresses, sprint. Behind you, the floors you cleared are filling back up. The officer squads bound down the long hallway in alternating sprints and covering fire. You're not clearing this level anymore. You're escaping it.

---

## 9. What you won't see in the first playtest

The logic above all runs today. The *presentation* layer has holes — full list and fixes in `enemy_gaps_and_setup.pdf`:

1. Sniper laser is invisible (logic fires, no beam effect bound).
2. Grenade warning ring missing, and the "Grenade out!" / "Suppressing!" voice lines were never written into the bark asset — both moves are currently silent.
3. Shield = placeholder cube blocking all directions (flank counter doesn't exist yet).
4. Sniper deals grunt-rifle damage (needs his real weapon).
5. Six of seven archetypes have no meshes/animations assigned yet; no flinch reactions on any of them.
6. Most barks display as "" speaking — the grunt's display name is blank.
7. The level needs its homework: spawn zones, mission-phase triggers, squad IDs on placed groups, navmesh — without them the director and squad layers sit idle.
