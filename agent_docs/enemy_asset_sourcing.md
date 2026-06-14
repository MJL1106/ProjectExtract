# EXTRACTION — Enemy Asset Sourcing (realistic military, COD/BF)

**Written 2026-06-13 on `AI-Companion-Prototype`.** Curated shopping list closing the asset gaps in `enemy_gaps_and_setup.md` (§4) and `enemy_inengine_setup_manual.md` (§0.7). Style target: realistic modern military (Call of Duty MW / Battlefield). Researched across Fab + reputable UE5 sources.

**Skeleton rule that drives every pick:** the Grunt runs on the **Epic UE5 Mannequin (Manny/Quinn)** skeleton with a shared ABP. Epic-skeleton assets are plug-and-play; UE4-mannequin assets retarget in ~15-30 min via the built-in `RTG_Mannequin`; custom skeletons cost a half-day IK Rig build. Picks below are weighted toward zero/low retarget.

**Price confidence:** `[V]` verified on listing · `[L]` strong evidence, confirm on page · `[?]` couldn't open listing (Fab 403'd), verify before buying.

**Two gaps no purchase solves — build in-engine (~1-2h total):** the grenade warning ring and the suppression-cower pose. Detailed at the bottom.

---

## DECIDED SHORTLIST (director's plan — 2026-06-14)

This is the buy plan settled on. The "lean kit" and category menus below are the full set of options it was chosen from — kept for reference and for the deferred items.

| Need | Decision | Link | Status |
|---|---|---|---|
| Characters — all 7 enemy archetypes + companion | **Quantum Assets — Modular Character Mega Bundle** ($299.99, 72 presets + 1,000 modules, single UE5 skeleton, MetaHuman-compat, 4.8/5). Replaces the separate Slayver+Heavy+Ghillie picks. | https://www.fab.com/listings/95c31b7c-9eb1-4fa7-953b-94ccea23f082 | **BUY** |
| Rifles (now) | Free **bonus static-mesh weapons** included in the Mega Bundle — placeholder for enemy hands (no fire anims; fine at distance) | (same listing) | use included |
| Rifles (later) | **Iron Edge Arsenal** — modular AR, 48 parts, 49 camos ($349.99) | https://www.fab.com/listings/dd14c66d-eb98-4a48-905e-ab5a16f64d63 | LATER |
| Locomotion + aim offsets | **GASP — Epic Game Animation Sample** (free, Manny-native, harvest clips). Also supplies the slide-into-cover / crouch-sprint transitions. | https://www.fab.com/listings/880e319a-a59e-4ed2-b268-b32dac7fa016 | **BUY (free) — DO FIRST** |
| Takedown victim (`OnTakedownExecuted`) | **RamsterZ Stealth Finishers** ($29.99) | https://www.ramsterzanimations.com/store-buy/p/stealth-finishers-knife-and-hand-fbx-only | **BUY** |
| Cover anims (companion + enemy) | **VanillaLoop Cover SET** (anims-only, ~$25) — NOT the Cover *System* bundle (`bb06916a`, bundles a BP framework you'd harvest around) | https://www.fab.com/listings/94f67182-1c8d-4896-9dd0-9e580099cd4c | **BUY** |
| Riot shield | deferred | — | OPEN |
| Audio (barks/SFX) | deferred | — | OPEN |
| Hit-reaction cluster | deferred (see watch-out below) | — | OPEN |

**GATE BEFORE BUILDING — confirm the Mega Bundle's skeleton is the Epic UE5 Manny.** Everything else (GASP, VanillaLoop, RamsterZ, and the existing enemy/companion ABP) assumes Manny. Quantum's packs are Manny + MetaHuman based so this is almost certainly true — but if it ships a custom skeleton, retarget all anims onto it once before wiring.

**Order of operations:**
1. Buy + import the Mega Bundle; confirm the Manny skeleton.
2. Harvest GASP locomotion + aim offsets onto the characters (free, must-have first — they can't do anything without it).
3. Buy VanillaLoop Cover Set + RamsterZ; layer onto the locomotion ABP.
4. Defer riot shield, audio, the hit-reaction cluster.

**Watch-outs:**
- Mega Bundle is **Tall/Masculine body only** — companion can't be female / alt-build from this pack.
- Bonus weapons are basic static meshes (no fire anims) — fine for enemy hands at distance; upgrade via Iron Edge later.
- **"Hit reactions later" is a whole cluster**, not one pack: hit-react (`OnHitReact`) + suppression cower (`OnSuppressedStateChanged`) + melee swing/`OnMeleePerformed` (rusher) + grenade throw (grenadier) + officer command gestures. Candidate sources when picked up: JKMotion Hit Reaction ($40), ByteSumPi Hostage (~$25, cower), Kubold Rifle Animset (melee + grenade), Mixamo (gestures, free).
- Cover packs include **no** slide-into-cover or crouch-sprint transition — source both from GASP (already in the plan).

**Still open (decide later):** riot shield mesh, audio (barks/SFX), the hit-reaction cluster, and the build-yourself VFX (sniper laser, grenade ring) from §6.

---

## The lean kit — closes every P0 + most P1 (~$450 + a Heavy mesh)

| Category | Asset | Price | Skeleton | Closes |
|---|---|---|---|---|
| Characters | Slayver — Modular Military Character 2 | ~$80 `[L]` | Epic UE4+UE5, plug-and-play | Grunt / Rusher / Officer / Grenadier via gear |
| Characters | Modern Heavy Soldier (or Heavy G Soldier) | ~$30 `[?]` | Epic UE4 (auto-retarget) | Heavy / Shield body |
| Animation | Kubold — Rifle Animset Pro | ~$60 `[V]` | UE4 (auto-retarget) | locomotion, aim offsets, melee, hit-react, reload, grenade-throw, death |
| Animation | Kubold — Cover Animset Pro | ~$50 `[V]` | UE4 (auto-retarget) | enter/exit cover, peek, lean, blind-fire |
| Animation | JKMotion — Hit Reaction Pack | ~$40 `[L]` | UE4+UE5 native | 96 directional hit-reacts + light flinches (`OnHitReact`) |
| Animation | Frank Stealth Kill | ~$35 `[L]` | UE4 (auto-retarget) | takedown victim reaction (`OnTakedownExecuted`, 0.8s) |
| Weapons | Gun Weapon Grenade (AGTRI) | $25 `[V]` | static/skel meshes | AR + sniper + LMG + grenade in one pack |
| Weapons | Military Police CQB Ballistic Riot Shield | $15 `[V]` | static mesh | the riot-shield gap (`DA_Enemy_Shield → ShieldMesh`) |
| VFX | Niagara Laser Beam Weapon V3 | ~$30-40 `[?]` | Niagara, BP wrapper | sniper laser (`OnLaserChanged`) |
| VFX | Shooter VFX Starter Pack | $30 `[V]` | Niagara | explosions, muzzle/tracers, impacts, decals, plate-break |
| Audio | Military Voice Pack PRO (Cafofo) | ~$20 `[V]` | WAV | 2,426 barks, clean + radio variants |

The Kubold pair + JKMotion + Frank cover every animation delegate the AI code exposes. Gun Weapon Grenade + the riot shield close all four weapon gaps for $40. The two Niagara packs cover the sniper laser and explosion/impact polish. Military Voice PRO is the single highest-value audio buy (radio-processed barks, the exact tactical vocabulary the bark system fires).

---

## 1. Characters — Gap 7 (mesh/ABP/tint on 6 BP children)

**Buy:** **Slayver Modular Military Character 2** — ~$80 `[L]`, Epic UE4+UE5 skeleton, 112 modular meshes (heads/bodies/vests/pouches/helmets/caps), "millions of combos." Covers Grunt, Rusher, Officer, Grenadier through gear + the 6 tint MIs the doc calls for.
- https://www.fab.com/listings/fcc4a754-95b7-47e5-b4e8-2207cde09c44

**Heavy** (dedicated bulky read): **Modern Heavy Soldier** ~$30 `[?]` (Epic UE4, zero extra retarget) or **Heavy G Soldier** `[L]` (UE Mannequin) — either reads as juggernaut without the retarget cost of the premium option below.
- https://www.fab.com/listings/a6cb27d9-fe48-4e49-8bb0-d0d82be2559d
- https://www.fab.com/listings/1d9afe12-f365-4528-b6ce-dda48045969b

**Shield archetype:** no dedicated riot-carrier character exists — use the Heavy body + the riot-shield prop (Weapons section), attached via socket.

**Sniper:** tint a base soldier, or buy a dedicated ghillie for instant legibility: **Modular Special Forces Ghillie Sniper** ~$199 `[L]`, Epic skeleton, woodland/desert/snow variants. https://www.fab.com/listings/14975bf8-4c1f-4ba3-bd0f-9cd495eb384c

### Premium character upgrade (optional, AAA tier)
- **Zhukov Modular Soldier Pack Vol.1 / Vol.2 / Vol.3** — $299.99 *each* `[V]`, Epic UE4+UE5, ~50k tris, 19-22 camo presets/vol, 5 LODs. The COD-team-grade pick; one volume alone covers 5/7 archetypes. Steep at $300/vol — Slayver gets ~80% of the way for $80. Vol.1: https://www.fab.com/listings/74fd2f65-1222-4afd-b330-68a4f0f82aca
- **Rapid Fire Military Collection** — $399.99 `[V]`, Assault/Heavy/Medic/Sniper classes, best dedicated Heavy/juggernaut found — but **custom skeleton = half-day IK Rig retarget**. https://www.fab.com/listings/70b5f78b-81fd-4292-9038-1502d2db64e3

---

## 2. Animation — Gaps (hit-react, suppression, takedown, melee, cover; locomotion exists for Grunt only)

| Need (delegate) | Pack | Price | Notes |
|---|---|---|---|
| Locomotion + aim offsets + melee + grenade-throw + death | **Kubold Rifle Animset Pro** | ~$60 `[V]` | 120+ mocap anims, UE4 skeleton, root-motion + in-place. The canonical TPP shooter set. |
| Cover: enter/exit/peek/lean/blind-fire (`OnHitReact` cover read) | **Kubold Cover Animset Pro** | ~$50 `[V]` | same vendor/skeleton — guaranteed interop. |
| Directional hit-reacts + light flinch (`OnHitReact(EHitRegion)`) | **JKMotion Hit Reaction Pack** | ~$40 `[L]` | 96 anims, UE4+UE5 native, directional + `_Light` flinch variants. |
| Takedown victim (`OnTakedownExecuted`, 0.8s) | **Frank Stealth Kill** | ~$35 `[L]` | paired killer+victim from-behind; Epic Launcher vault (not migrated to Fab). |

- Kubold Rifle: https://www.unrealengine.com/marketplace/en-US/product/rifle-animset-pro
- Kubold Cover: https://www.fab.com/listings/f9e9fbcb-0f07-49a0-8c06-80b18eba0e90
- JKMotion: https://www.fab.com/listings/effa9c6e-1571-4a53-b235-a5411ddf5401
- Frank Stealth Kill: https://www.unrealengine.com/marketplace/en-US/product/frank-stealth-kill
- Cover (anims-only, UE5 Manny-native, the decided pick): **VanillaLoop Cover Set**, 242 anims (root + in-place), ~$25 — https://www.fab.com/listings/94f67182-1c8d-4896-9dd0-9e580099cd4c · `bb06916a` is the same studio's Cover *System* (bundles a BP framework — skip it) · avoid Filmstorm's own cover pack (1.8/5, AI-generated)
- Alt takedown (explicit victim anims, $30, FBX-only): **RamsterZ Stealth Finishers** — https://www.ramsterzanimations.com/store-buy/p/stealth-finishers-knife-and-hand-fbx-only

**Free fillers (Mixamo, retarget to Manny ~45 min first-time):** "Head Hit" (head flinch gap), "Crouching"/"Scared" (suppression-cower approximation), extra death variants. Free UE5-native rifle locomotion: https://www.fab.com/listings/8de31c5d-93bc-4bd4-9606-ca789ce91b99

**Retarget effort:** Kubold/Frank/JKMotion are UE4 or UE5 mannequin → 15-30 min batch retarget for the whole Kubold suite via Epic's pre-built retargeter. JKMotion/Filmstorm are UE5-native = zero.

---

## 3. Weapons — Gaps 3 (sniper), heavy LMG, shield, grenade body

**Buy both:**
- **Gun Weapon Grenade (AGTRI)** — $25 `[V]`, 40 weapons (AR/AK/sniper/DMR/LMG/SMG/pistol/shotgun + grenades + projectile meshes), Lyra-sourced muzzle FX + audio, UE 4.25-5.7. One pack covers the AR, sniper (Gap 3), LMG (P1), and grenade body. https://www.fab.com/listings/b6e3d970-e841-4bc7-ad23-c4d07126eb1f
- **Military Police CQB Ballistic Riot Shield (Outworld)** — $14.99 `[V]`, 130k tris, FBX/PBR. The exact SWAT/military shield archetype; scale to ~120cm and add a LOD in-engine. **This closes the P0 shield-mesh blocker.** https://www.fab.com/listings/01b20fa7-7720-4dd4-9567-d27a151c763b

**Optional:**
- Named real-world weapons for cohesive AAA look: **Farom Modern Weapon Pack Vol.1** ~$50 `[L]` (M17, Vector, SCAR-H, FN FAL, **M249 SAW**, **Barrett M82**). https://www.fab.com/listings/46a4eb5b-1ce4-4ab5-957e-54e5589f765c
- Grenadier variety (M67/M61/MKII/flashbang/smoke): **Knives, Explosives & Ammunition Props Pack** $19.98 `[V]`. https://www.fab.com/listings/3a4982b4-80e0-4fb2-89f1-c5a3b4e69e12
- Premium FP-quality (COD/Far-Cry polycount, no LMG): **ChamferZone Ultimate FPS Weapons Pack** $280 `[V]`. https://chamferzone.com/store/wb7e6/ultimate-fps-weapons-pack-for-unreal-and-unity

These are third-person enemy weapons seen at distance — clean silhouette + PBR matters more than FP-grade detail, so Gun Weapon Grenade is the value play.

---

## 4. VFX — Gaps 5 (sniper laser), grenade ring, plate-break, explosion

| Need (binding) | Pack | Price | Notes |
|---|---|---|---|
| Sniper laser (`OnLaserChanged`) | **Niagara Laser Beam Weapon V3** | ~$30-40 `[?]` | Niagara + BP wrapper with exposed Start/End; charge→fire flow maps to the 2s telegraph. Red is a material param. https://www.fab.com/listings/b776d3d2-5d87-4f8b-8466-d7bbb17ab2a4 |
| Explosion + muzzle + tracers + impacts + decals + plate-break | **Shooter VFX Starter Pack** | $30 `[V]` | broadest single-pack Niagara coverage; UE 5.3-5.4 (test 5.7). https://www.fab.com/listings/c7bb6377-2162-4c56-8950-907ba44c22e3 |
| Premium explosions (mortar/shrapnel, COD-feel) | Military & Modern Warfare VFX Pack | ~$60-80 `[?]` | 35 Niagara FX, UE 4.27-5.7. https://www.fab.com/listings/a94b6538-9ea2-48c5-be49-baa7b245cfcd |
| Muzzle/tracers (dedicated, on sale) | Realistic Gun VFX (Hivemind) | $24.99 `[V]` | 5 weapon types, UE 5.4-5.7. https://www.fab.com/listings/21573c1a-e494-4194-9926-0c03f48a5563 |

**Free VFX:** Epic **Niagara Examples Pack (UE 5.7 official)** — 50+ systems incl. bullet impacts + sparks usable for plate-break: https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600 · Free realistic explosions: https://www.fab.com/listings/a48b3fa2-2ebf-42c2-8892-fa20a1eff289

---

## 5. Audio — all optional (subtitles are the shipped contract)

- **Military Voice Pack PRO (Cafofo)** — ~$19.99 `[V]`, **2,426 phrases, clean + radio-transmission variants** of each line, plus radio bleeps. Covers Contact/Grenade-out/Suppressing/Man-down/Falling-back/Flanking — the exact bark vocabulary. Epic Launcher vault (or Unity store; WAVs work in UE5). https://www.unrealengine.com/marketplace/en-US/product/military-voice-pack-pro
- Multi-character squad variety (8 voices, $149.99): **Military Soldiers Bundle** — https://www.fab.com/listings/30d1da0c-31e5-4041-999c-ab5f75cc7106
- Weapon/explosion/melee SFX one-stop (5,000+ files): **Advanced Shooter SFX All-in-One** — https://www.unrealengine.com/marketplace/en-US/product/advanced-shooter-sfx-all-in-one
- **Free SFX:** Sonniss GDC archive (160+ GB, no attribution) — https://sonniss.com/gameaudiogdc/
- Sniper aim-hum (no off-shelf pack): pull a "tension rise" from ZapSplat/Pixabay free tier, or generate in Audacity (sine + pitch ramp).

---

## 6. The two build-it-yourself gaps (not purchasable)

1. **Grenade warning ring** (`OnGrenadeTelegraph`, ~350 radius red ground ring): no pack ships this. Build a Niagara **Decal Renderer** emitter drawing an expanding ring onto a circle material — Epic tutorial: https://dev.epicgames.com/community/learning/tutorials/z0OW/unreal-engine-decal-renderer-in-niagara — ~1h.
2. **Suppression cower** (`OnSuppressedStateChanged` duck/pin pose): no military pack labels this. Blend additively into the Cover Animset low-cover crouch idle when `bSuppressed` is set, or use a Mixamo "Crouching/Scared" pose. ~30 min in the ABP.

---

## 7. Caveats before buying

- **UE 5.7 compatibility:** several VFX/anim packs list up to 5.4. UE assets usually forward-compat across minor versions, but test in a throwaway project first.
- **Verify `[?]`/`[L]` prices on the listing** — agents couldn't open some Fab pages (403); the price shown is a strong estimate, not confirmed.
- **Epic Launcher vault items** (Frank Stealth Kill, Military Voice PRO, Advanced Shooter SFX) weren't migrated to Fab — buy via the Epic Games Launcher, they still work in UE5.
- **Wiring after import:** once meshes/FX/anims exist in the project, the DA edits, BP delegate bindings, mesh/ABP/tint assignment, and shield offset are all autonomous NeoStack-loop work (the `[AGENT]` rows in `enemy_gaps_and_setup.md` §1). Buy + import is the only true human step.
