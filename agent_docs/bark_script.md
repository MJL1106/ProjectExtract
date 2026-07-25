# Bark script — companion & extractee VO

Working script for the companion and VIP voice lines. **Edit this file directly** — delete lines you don't
want, rewrite ones you'd say differently, add your own. When you're done I generate the WAVs from it and
write the text into the `Line` fields on `DA_Barks_Companion` / `DA_Barks_Extractee`.

**Why this file exists:** this is the written record of what every take says. The `Line` fields on
`DA_Barks_Extractee` are empty, so the VIP's pre-existing script is unrecorded.

**Subtitles are retired — kept on purpose.** `FBarkVariant::Line` is not rendered anywhere player-facing;
its only live consumer is the bark debug display. It stays because it records what each take says and is
the hook a subtitle system would read from if one is added later. Populate it when authoring new takes.

**How to read it:**
- **Part 1** — types flagged as corny. These get regenerated wholesale; whatever you leave here is the new
  take list for that type. Options are deliberately over-supplied — cut down to 3–5 per type.
- **Part 2** — the VIP rescue exchange. Pick one lettered option, delete the others.
- **Part 3** — types staying as-is. Listed for reference with take counts. Lines shown are from
  `bark_design.md`; where a type has more takes than lines, the extra takes' text is unknown. Mark a type
  `REGENERATE` in the heading if you want it redone too.
- **Part 4** — two decisions that cut across everything.

Register throughout: real operator brevity. Fragments carrying information, not sentences carrying
sentiment. If a line could be a movie poster, it's wrong.

---

## Part 1 — Regenerate (flagged as corny)

### ApprovePlayerKill · 6 takes currently
Cut: "That's how it's done."

- "Good hit."
- "Solid."
- "Clean."
- "That's one."
- "Confirmed."
- "Nice."
- "Got him."

### PostFightBanter · 9 takes currently
Cut: "Remind me why we took this job." · "Still standing. Barely."

- "Check your mags."
- "Reload while you can."
- "Watch the doorways."
- "Keep it moving."

### RevivingPlayer · 4 takes currently
Cut: "Don't you quit on me." · "You're not dying here."

- "Hold still."
- "Stay with me"

### StealthBroken · 3 takes currently
Cut: "So much for quiet — go loud!"

- "Compromised."
- "We're made — weapons free."
- "They've seen us. Move."

### CompanionHurt · 3 takes currently
Cut: "That one stung."

- "Hit."
- "I'm hit — still up."

### IdleAmbient · 9 takes currently
Cut: "Doesn't feel right, this quiet."

- "Nothing on my side."
- "Clear here."
- "Nothing yet."

### CompanionRevived · 3 takes currently
Cut: "Thanks — let's finish this." · "I owe you one."

- "Up."
- "Good. Moving."
- "I'm back — on you."
- "Functional. Let's go."

### MultipleContacts · 5 takes currently
Cut: "It's a whole squad."

- "Multiple."

### ThrowingGrenade · 3 takes currently
Cut: "Cooking one, move!"

- "Frag out."

### ExtractionCallout · 3 takes currently
Cut: "That's our ride — go!"

- "Almost out — don't slow down."

---

## Part 2 — VIP rescue exchange

Three beats today: companion speaks on rescue start, companion speaks on pistol handoff, VIP replies once
armed. Pick **one** option — mixing across options breaks the register.

### Option A — minimal (fits current 3-slot timing as-is)
1. Companion, rescue start: "Hold still. Cutting you loose."
2. Companion, handoff: "Here, take my Sidearm."
3. VIP, reply: "Copy. On you."



---

## Part 3 — Keeping as-is

Reference only. Add `REGENERATE` to a heading to pull it into the rewrite.

### Contact & combat
- **ContactCombat** · 8 takes — "Contact!" · "Contact front!" · "Tangos — eyes up!" · "We got company."
- **EnemyDirection** · 8 takes — "Left side!" · "On the right!" · "Up high — the balcony." · "Movement, twelve o'clock."
- **EnemyArchetype** · 4 takes — "Sniper — get down!" · "Heavy, don't push it." · "Rusher, he's coming in!"
- **TargetDown** · 8 takes — "Tango down." · "One down." · "Got him." · "Clear on mine."
- **LostContact** · 5 takes — "Lost 'em." · "Where'd he go…" · "Hold — I don't see 'em."

### His own actions
- **Reloading** · 3 takes — "Reloading!" · "Reloading, hold 'em."
- **Repositioning** · 3 takes — "Moving up!" · "Repositioning."
- **TakingCover** · 3 takes — "In cover." · "Holding here." · "Set."
- **Suppressing** · 3 takes — "Covering you — move!" · "Suppressing!" · "On it, go!"
- **Flanking** · 3 takes — "Going around." · "Taking the side." · "Flanking 'em."
- **PushingUp** · 3 takes — "Pushing!" · "With me!" · "Advancing — stay on me."

### Support & relationship
- **PlayerDownReaction** · 2 takes — "You're hit — hang on!" · "Stay down, I've got you."
- **PlayerRevived** · 3 takes — "On your feet — let's move." · "Back up. Stay in it." · "Good. Come on."
- **CompanionDown** · 3 takes — "I'm hit — I'm down!" · "Down! I need a hand!" · "Can't… I'm down."
- **CompanionCallForHelp** · 3 takes — "Bleeding out here!" · "Any time now…" · "I can't hold this."
- **Reassurance** · 7 takes — "We got this." · "Stay with me." · "Almost through it."
- **PlayerHurtWarning** · 2 takes — "You're hurt — patch up." · "Watch yourself, you're bleeding."

### Stealth - needs to be whispered voice regerneate
- **GoingStealth** · 3 takes — "Going quiet." · "Dark from here." · "Slow and quiet."
- **StealthSpotEnemy** · 3 takes — "Hold — tango ahead." · "Contact, don't move." · "One up front… I see him."
- **TakedownConfirm** · 3 takes — knife: "Clean kill" . "Clean"
- **HoldForPatrol** · 2 takes — "Hold… let 'em pass." · "Wait. Not yet."

### Commands
- **AckBreach** · 3 takes — Tactical: "Stacking up." Loud: "On the door — breaching!" Quiet: "Quiet entry. Ready."
- **Breaching** · 2 takes — Loud: "Go, go, go!" Quiet (whisper): "Move in"
- **AckTakedown** · 3 takes — "On it." · "Moving to him." · "I'll take him."
- **AckLoot** · 3 takes — "Grabbing it." · "On the loot." · "Checking it."
- **AckExplore** · 3 takes — "Checking it out." · "Moving to the mark." · "I'll sweep it."
- **AckGeneric** · 4 takes — "Moving." · "Copy." · "On my way." · "With you."

### Navigation, world & ambient
- **FallingBehind** · 3 takes — "Hang on — catching up!" · "Wait up!" · "Moving to you."
- **Blocked** · 3 takes — "I'm stuck back here!" · "Can't get through — go around?" · "Blocked!"
- **Regroup** · 3 takes — "Coming back to you." · "Regrouping." · "Task done — on you."
- **Following** · 7 takes — "Right behind you." · "On your six." · "Got your back."
- **ObjectiveSpotted** · 3 takes — "There's the objective." · "That's our way through." · "Keycard — grab it."
- **HeatRising** · 5 takes — "It's getting hot." · "More on the way." · "They're onto us — pick it up."
- **AreaClear** · 7 takes — "Clear." · "That's all of 'em." · "We're good — for now."

### Scripted / level-anchored
`ExtractApproach` ×2 · `HighFloor` ×2 · `LobbyEntry` ×2 · `ObjectiveApproach` ×2 · `Stairwell` ×2 ·
`PistolHandoff` ×1. Text unrecorded.

---

## Part 4 — Decisions

**1. "Tango" or "hostile"?** He currently uses both. One word needs to run through ContactCombat,
TargetDown, StealthSpotEnemy and EnemyDirection. "Hostile" reads current-doctrine; "tango" reads
2000s-era. Picking either pulls those four types into the regenerate list.

> Choice: both are fine — no change, nothing pulled into regeneration.

**2. Ten types have VO but no trigger.** Suppressing, Flanking, PushingUp, HoldForPatrol, Blocked,
Regroup, ObjectiveSpotted, ExtractionCallout, HeatRising, PostFightBanter — all recorded, none called
from C++. Either wire the call sites or the audio stays dead.

> Wire them up? yes — all ten.

**3. Grenadier dropped.** EnemyArchetype keeps Sniper/Heavy/Rusher; grenadier sightings are silent.

**4. Single-take types ship as-is.** MultipleContacts, ThrowingGrenade, ExtractionCallout each play one
clip every time.
