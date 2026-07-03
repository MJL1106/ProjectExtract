<!-- ROADMAP-CHECKLIST v1 | auto-maintained by .githooks/roadmap-update.sh — keep this line -->

# ProjectExtract — Feature Roadmap & Build Checklist

The remaining work in build order (W1 → buffer), as a tickable checklist to follow bit by bit. Everything here is *new* (not yet in game). Each item is written so a fresh chat can act on it cold — tick items as they land.

**Status key:** `- [ ]` to-do · `- [~]` in progress · `- [x]` done.
**Week tags** `(Wn)` are *soft* — order can swap, but the core sequence holds. `(stretch)` items are the first to cut under the OK/Worst scenarios below.

**How this file stays current:**
- **Primary (reliable):** whoever commits reconciles this file against the diff *before* committing, so statuses ship inside the work commit. Works regardless of which chat made the changes.
- **Backstop (automatic):** a git `post-commit` hook (`.githooks/roadmap-update.sh`) auto-reconciles from the commit via headless `claude -p`, wherever that CLI is authenticated. Never blocks or breaks a commit; if it can't run, it skips. Re-wire after a hooks reset with `sh .githooks/install.sh`.

**Milestones:** Prototype 2 ships **Tue 7/7** · polished companion demo **~20/7** · target everything done **Sun 9/8** · NY from **Fri 31/7** · sporadic buffer **10–19/8** · **hard deadline 19/8**. Dissertation draft structure due **Wed 9/7**.

---

## Scenarios (Best / OK / Worst)

Target: whole list done by end of W7 (Sun 9/8). `[Core]` ships in every scenario; `[Stretch]` flexes:

- **Best** — everything: 4-gun arsenal, attachments + ammo economy, full cover-animation rework, all breach styles, takedown variants, full UI (menu / loadout / minimap), full audio + music.
- **OK (drop less)** — keep AR + pistol + SMG, attachments, ammo economy, core cover-animation rework (skip lean/peek variants), functional menu + loadout + basic minimap. Drop/defer: shotgun, breach-style variants, class-creation polish, in-game music.
- **Worst (drop more)** — keep AR + pistol, the existing cover animations (cut the rework), one breach style, HP + ammo HUD. Normal mode becomes a don't-engage-first flag on Combat. Drop: SMG, shotgun, attachments, ammo economy, cover rework, breach/takedown variants, all UI beyond HUD, minimap, music.
- **Never cut:** one complete polished level (both halves), extraction person, HP + ammo HUD, all three companion modes, looting, objectives. (The only level cut is one polished level instead of multiple level types.)
- **Biggest flex lever:** the cover-animation rework (W3–W4) — cutting it frees ~1.5 weeks; the companion still uses cover intelligently, only the look downgrades.

---

## Timeline — week by week

### Week 22/06 (W1) — Foundations: level + companion control spine
- [~] [Core] **Marketplace level + blockout.** Buy a pre-built level (factory or apartment complex; cluttered/tight spacing) and rough-block the single polished level: a neutral no-enemy start → fight from point A → B → point of interest → extract back out. Need a stage to demo everything on.
- [~] [Core] **Companion control framework — the "ping" system.** The player marks something in the world (a "ping") — an enemy, a door, a loot spot — and the companion acts on that ping. This input layer is the spine every later companion command (takedown, breach, loot, mode-swap) plugs into. *(Follow / stay-behind movement already exists — not in scope.)*
- [x] [Core] **Pre-authored companion route tool.** A way to hand-place a path in the level that the companion follows on cue — e.g. a cinematic "companion takes the lead off the rooftop at mission start, follows the set path, then breaks off." Reused later for combat-mode lead.
- [x] [Core] **Fix player sprint-arm bug.** Holding sprint, then stopping while still holding the sprint key, leaves the player's arms animating as if sprinting while stationary.

### Week 29/06 (W2) — Prototype 2 build → deliver Tue 7/7
- [x] [Core] **Companion-synced takedown.** Player pings an enemy for a close-range takedown and the companion plays a paired/synced takedown animation alongside. *(The base player takedown already exists; the companion-side sync is the new part.)* — shoot/gun path done: ranged headshot kill synced to player gunfire (2-enemy) / autonomous 2–4s (lone), with face → aim-in → 2 shots → drop → lower presentation. Knife paired path runs via instant-kill fallback (stab anim still missing).
- [x] [Core] **Door breach v1 (loud).** Player pings a room/door; the companion kicks it down and goes in guns-blazing. Just the loud style for P2 — the other styles come in W5. *(Breach door has a mesh + collision; companion paths to it, breaches on arrival, door swings open, companion holds ~3s then resumes follow.)*
- [ ] [Core] **Enemy model swap.** Replace the enemy character models and wire a few enemy types into the level.
- [ ] [Core] **Tune the loop.** Get 2–5 minutes of gameplay reading well; test and finalise for the Tuesday 7/7 delivery.

### Week 6/07 (W3) — Ship P2 (Tue), start cover rework, dissertation draft (Wed 9/7)
- [ ] [Core] **Deliver Prototype 2** (Tue 7/7) — release objectives in the note below.
- [ ] [Diss] **Dissertation draft structure** (due Wed 9/7). Section skeleton (intro / companion-AI design / implementation-to-date / evaluation / conclusion) pre-filled from what's built so far.
- [x] [Stretch] **Start cover-animation rework.** Shared by enemies + companion; new COD/BF-style animations — shoot over cover, shoot around cover, crouch + standing cover locomotion. *(Cover behaviour already works as a mechanic; this replaces the static-looking animations.)* *(Shipped on Cover-system-revamp: enemy CROUCH cover (Kubold montages, root-motion over-top + corner peeks, per-scenario weapon align, solid cover-wall collision, Confident-morale reseek, force-peek/force-height debug cvars); STANDING cover peeks (enemy + companion, weapon-align from tuned socket poses); companion cover-align built from scratch; cover REPOSITIONING for angles (EQS side-flag weighting + composite shuffle scoring + angle-impulse relocate); reload-gated peeks + proactive tucked reload; randomized peek/hide timings + long-hide feint; reload-in-cover torso tuck (dynamic idle-capture); move-along-wall montage scaffold (clips pending); drift-correct facing fix. Enemy + companion use identical anims.)*
- [ ] [Core] **Enemy AI-director review + test.** The existing enemy director needs a review/tuning/test pass.
- [ ] [Core] **Companion locomotion / grip polish.** Tidy the companion's gun grip and stance by reusing the enemy animation setup.

> **P2 min-spec (release objectives, Tue 7/7):** first half of a level · 2–5 min gameplay · few enemy types · 1 weapon, no attachments · takedown sync with companion · companion cinematic route at level start · basic player control over companion · ping a room for the companion to open · enemy model changes.
> **P2 priority extras (if time, in order):** cover rework · AI director · second half of level · companion locomotion · enemy weapon attachments.
> **P2 definite-no:** multiple weapons · UI/UX · stealth & combat modes · VFX/sound · extraction person.

### Week 13/07 (W4) — Cover finish + companion combat brain
- [~] [Stretch] **Finish cover rework.** Peek/lean round corners while aiming, move-and-shoot *around* cover (not glued to it), and hold-the-angle logic: when high-HP and not under pressure the companion holds the last-seen enemy angle instead of ducking into cover. It should prefer cover but never stop firing mid-fight just to reach it. *(Peek/lean-round-corners + move-and-shoot / cover-to-cover repositioning shipped on Cover-system-revamp. Remaining: hold-the-angle logic — high-HP, low-pressure companion holds the last-seen angle instead of ducking to cover.)*
- [ ] [Core] **Companion Combat mode.** Aggressive; takes the lead; moves cover-to-cover prioritising forward progress; gains space without suicidally rushing in; likely needs its own combat "director" like the enemies. Companion's gun is predefined per level with infinite ammo.
- [ ] [Core] **Companion Normal mode.** Doesn't engage first — only engages once the player is spotted/shot at, with an instant reaction; confident but never moving ahead of the player. Sits one level above stealth.

### Week 20/07 (W5) — Polished companion vertical-slice demo (the dissertation demo)
- [ ] [Core] **Companion Stealth mode.** Crouched/slow movement behind the player, no sprinting, acts on the ping system, whisper voice lines. Plus stealth rules: not seen by enemies, the companion's gunfire and footsteps aren't picked up by enemy AI, and it instantly breaks to combat if the player is spotted.
- [ ] [Core] **Mode-swap + AI director.** Player can switch the companion's mode; missions suggest a mode (e.g. stealth) but the player can ignore it, and the AI director scales difficulty so going guns-blazing on a stealth mission isn't overpowered.
- [ ] [Core] **Companion looting.** Player pings a lootable spot (cupboard, table…); the companion goes and loots it, then moves to the next if there's more. Loot goes into a shared objective inventory — e.g. a keycard you "use" later, not a hands-on item.
- [ ] [Core] **Objectives + distance marker.** A simple objective system (collect item X / explore room Y / extract person to location Z) with a visible on-screen distance marker showing where to go.
- [ ] [Stretch] **Breach styles (tactical + quiet)** on top of loud. *Tactical:* kick the door, then fall back behind the wall connecting to it and play it cautious before entering. *Quiet:* silently unlock the handle and open the door. Player picks the style.
- [~] [Stretch] **Takedown variants.** Hand-to-hand vs gun takedown: no suppressor forces a hand-to-hand takedown; on a gun takedown the companion one-taps the head, and if the player misses their shot the companion assists with a headshot after ~1.5s. — gun takedown (companion head-tap) + headshot damage (65% of max HP, enemies-only, shield-first) done. Pending: suppressor-gated hand-to-hand-vs-gun selection + the player-miss → companion-assist timing.
- [ ] [Core] **Light game-feel polish.** Muzzle flash + core weapon sounds so the demo reads well; capture demo footage.

### Week 27/07 (W6) — Arsenal + extraction person + 2nd half (home Mon–Thu, NY from Fri 31/7)
- [ ] [Core] **Second half of level.** Finish the back half: point of interest → extract back out (to the start, or a separate extraction point).
- [ ] [Core] **Extraction person.** A simple escort NPC that hides behind the companion, can't be damaged, takes a scared/crouched pose under fire, is locked to the companion, freezes (lying-down pose) if the companion is downed and resumes when revived; scared/horror-style locomotion. Reuses companion logic (~1 day).
- [ ] [Stretch] **Weapon system.** Full arsenal — AR / pistol / SMG / shotgun; 2-weapon loadout (primary + pistol; some missions pistol-only); attachments with effects; per-weapon realistic recoil; a reusable skill + doc for the right-click ADS (aim-down-sights) alignment so adding new weapons is easy; player grip polish. Weapons/attachments from the Infima ModernGunsBundle; visuals stay on the procedural FPS pack.
- [ ] [Stretch] **Ammo economy.** Ammo drops from killed enemies (drop-chance pickup class) and is lootable from rooms; one ammo type per weapon type; ammo the companion loots is auto-added to the player.
- [ ] [Stretch] **Enemy gun attachments.** Add a few attachments to enemy weapons.

### Week 3/08 (W7) — UI/UX, polish, integrate (full-time in NY) — target everything done Sun 9/8
- [ ] [Core] **In-game HUD: HP + ammo** (required). Player health and a weapon/ammo readout.
- [ ] [Stretch] **UI/UX.** Main menu (marketplace-bought, shooter-themed), class-creation / loadout editor (COD/BF style — set weapons + attachments pre-mission), and a minimap.
- [ ] [Stretch] **Full polish.** Enemy + companion voice shouts (companion with a bit of personality/depth), weapon FX + reload sounds, music (menu + in-game for combat / extraction).
- [ ] [Core] **Integrate the full level.** Both halves end-to-end, full objective flow, a difficulty-consistency pass (no scaling across the level).
- [ ] [Core] **Final verify.** Confirm the whole list is present and working; capture final demo footage.

### After W7 — 10–19/08 sporadic buffer (NY → return; hard deadline 19/8)
- [ ] **Contingency** — absorb any slipped Stretch (in-game music, 2nd-half dressing, UI polish).
- [ ] **Dissertation paper** — write and finalise from captured notes + footage.
- [ ] **Gameplay demo** — cut and finalise from captured footage.
- [ ] **Final bug-fix / stabilise** pass to the 19/08 deadline.
