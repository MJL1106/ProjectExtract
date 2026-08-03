# EXTRACTION — Enemy Animation Asset Sourcing (Refreshed 2026-06-18)

**Branch:** `Enemies`. Supersedes the 2026-06-13 `enemy_asset_sourcing.md` for the animation section.
**Key change from old doc:** Skeleton is confirmed `SK_Military_Character_Skeleton` (Quantum Modular), MetaHuman-compatible, with working retargeters `RTG_RifleMannequin_to_Military` + `IK_MilitaryCharacter`. UE4-Mannequin packs retarget in ~15-30 min via these; custom skeletons cost a half-day IK Rig build. OLD DOC's "GATE: confirm skeleton" caveat is RESOLVED — it is Manny-compat, not stock Manny, which changes nothing for UE4/UE5 Manny packs.
**Characters/visuals:** Already handled (Military Mega Bundle + Quantum Modular Mega Bundle). This doc is ANIMATIONS ONLY plus the shield mesh and audio.

---

## Skeleton Compatibility Reference

| Pack skeleton | Retarget cost to Quantum | Notes |
|---|---|---|
| UE5 Manny (GASP, JKMotion) | Zero — existing retargeter | Native to the pipeline |
| UE4 Manny (Kubold, VanillaLoop, RamsterZ) | 15-30 min batch | Existing RTG_RifleMannequin_to_Military covers this |
| Custom (MoCap Online MotusMan_V55) | Half-day IK Rig build | Reject unless no alternative |

---

## Gap Status — What the On-Disk Kits Cover

**Infima Modern Guns Pack** (already imported): FP-authored Pistol/Rifle/SMG/Shotgun/Sniper sets on UE4 Mannequin skeleton. These are **first-person arm animations**. They retarget to Quantum, but they read as FP only — robot wrists in TP. The Rifle set retargets adequately for grunt/officer/grenadier at distance. The Sniper set is FP bolt-cycle only; usable as a fallback in TP but reads weak.

**ProceduralFPSKit** (already in project): Provides the shared ABP locomotion base.

---

## Gap 1 — SHIELD: One-Handed Pistol/Revolver + Shield-Carry Locomotion [BIGGEST BLOCKER]

**Problem:** The Shield archetype fires a revolver one-handed while carrying a riot shield left-arm-forward. Nothing on the Quantum rig has either half of this.

**Finding:** No single pack ships a riot-shield-carrier + pistol locomotion set on the UE4/UE5 Mannequin. This gap requires **two packs layered**:

### Part A — One-Handed Pistol Combat (idle/aim/fire/reload)

**RECOMMENDED: Kubold Pistol Animset Pro**
- Price: ~$50 [L — Fab lists "from $49.99"; confirm before buying]
- Skeleton: UE4 Mannequin — retargets via existing RTG in 15-30 min
- Content: 150 mocap animations (root-motion + in-place) covering TP pistol idle, aim offsets, fire, reload, locomotion-while-aiming in all directions, crouch variants
- Closes: Shield revolver fire/aim, could double as Officer sidearm if added later
- Fab: https://www.fab.com/listings/c5caff8c-6815-4e81-b825-aeb95967411e
- Alt: Kubold's pack is the canonical choice — MoCap Online Pistol Pro ($149.99) uses custom MotusMan_V55 skeleton, reject.

### Part B — Shield-Carry Locomotion (left arm forward)

**Finding: No dedicated firearms+riot-shield locomotion pack exists on Fab as of 2026-06-18.** Searched under "riot shield", "ballistic shield", "shield locomotion", "one-handed firearm shield". The only shield-locomotion packs are melee/fantasy (Kubold Sword&Shield, Essential Spear&Shield — both fantasy, HIK skeleton, no firearm integration).

**BUILD-IN-ENGINE — recommended path:**
Use Kubold Pistol Animset Pro as the base and build a dedicated shield-advance additive pose in the ABP:
1. Lock the left arm in a "shield brace" pose additively over the pistol locomotion tree.
2. The ShieldAdvance BT task already drives movement — the only new authored content is: shield-arm-forward idle (1 pose), shield-arm-forward walk (1 loop), shield-arm-forward jog (1 loop), sidearm peek-out (1 short montage). These are achievable in Control Rig or as a GASP-pose additive (~2-4h animator time or Mixamo "Zombie Walk" left-arm-forward as a rough approximation).

**Alt if budget allows:** Commission 3-4 shield-carry clips from a freelancer (Fiverr, ArtStation Marketplace). ~$50-150 for FBX-only on UE4 Mannequin.

---

## Gap 2 — RUSHER: CQC Melee (gun-butt / aggressive run-and-gun)

**Kubold Rifle Animset Pro already covers this** — confirmed list includes `Rifle_Melee_Hard`, `Rifle_Melee_Kick`, and continuous-fire locomotion. If the kit's rifle retarget reads acceptably in TP (it should at Rusher's fast, chaotic movement speed), **no additional purchase needed**.

If the Rusher reads too tame: buy **JKMotion Hit Reaction Pack** anyway (Gap 5) and use the knockback animations mirrored as a punch-out — or flag after first playtest.

- Decision: **DEFER until playtest** — Kubold Rifle covers it.

---

## Gap 3 — SNIPER: Bolt-Action TP (bolt-cycle, scoped idle, relocate)

**Infima Sniper set** retargets but is FP-authored — reads robot in TP. Whether it's "good enough at distance" is a playtest call.

**If playtest fails:** Kubold Rifle Animset Pro has no sniper animations. Options:

- **Shooter Rifle Animations** (Fab: https://www.fab.com/listings/42be3269-df20-4ea0-bcff-306fbcec5494) — handcrafted for full-body UE4 Mannequin; check listing for bolt-action variants before buying. Price unconfirmed [?].
- **GASP** (free) — pull the "aim idle" poses and use them with a bolt-cycle montage authored in Control Rig. ~1-2h.
- **BUILD** the bolt-cycle montage additively on top of the retargeted Infima idle. The bolt-cycle is a 0.8s right-arm-only motion — Control Rig or a $10 Mixamo approximation is viable.

- Decision: **DEFER until playtest confirms Infima TP reads weak.** If it does: BUILD bolt-cycle montage in Control Rig, ~1h. No purchase.

---

## Gap 4 — HEAVY LMG: Sustained-Fire Braced Grip + Belt Reload

**Finding:** No dedicated UE4/UE5 Mannequin LMG animation pack exists on Fab. Kubold has no LMG set. MoCap Online has no LMG-specific pack. The M249 LMG pack found (AB3DX) provides arm-only animations, no full-body TP character set.

**Recommended path — BUILD:**
The Heavy's Kubold Rifle Animset Pro retargeted rifle idle/fire/locomotion reads as "guy with a gun" — his differentiation at this stage is mechanical (250HP, armour, suppression resistance, sustained bursts), not visual. Visual LMG differentiation is a polish pass.

If LMG visual identity is prioritised:
- Use GASP's "heavy weapon carry" idle (if present in the 5.7 update's 400 new animations — check before buying anything).
- Author a "wide-grip braced idle" pose offset additively in the ABP. ~1h.
- Belt reload: Kubold Rifle reload retargeted + a 2s "pull belt" additive. ~1h.

- Decision: **BUILD** using GASP + additive poses. No purchase.

---

## Gap 5 — WEAPON-CORRECT RELOADS (Revolver, Sniper Box-Mag, LMG Belt)

- Revolver gate-load: covered by Kubold Pistol Animset Pro (Gap 1 Part A) — it includes revolver-style reload montages.
- Sniper box-mag: Infima sniper reload retargets acceptably; BUILD if it reads wrong (same Control Rig approach as Gap 3).
- LMG belt: BUILD (see Gap 4).

- Decision: **Covered by Gap 1 Part A purchase + BUILD for sniper/LMG.**

---

## Gap 6 — CARRIED FORWARD FROM OLD DOC (re-confirmed status)

| Item | Recommendation | Status |
|---|---|---|
| Riot shield MESH | **Outworld Military Police CQB Ballistic Riot Shield** ~$15 [L] — 130k tris, FBX/PBR, correct SWAT silhouette. Fab: https://www.fab.com/listings/01b20fa7-7720-4dd4-9567-d27a151c763b | BUY-NOW |
| Hit-react cluster | **JKMotion Hit Reaction Pack** — UE4+UE5 Manny native, 96 directional hit-reacts + light flinches. Price: ~$40 [L — listing exists, Fab 403'd, estimate from old doc]. Fab: https://www.fab.com/listings/effa9c6e-1571-4a53-b235-a5411ddf5401 | BUY-NOW |
| Suppression cower | BUILD — additive low-crouch hold blended in ABP when `bSuppressed`. Pull Mixamo "Crouching Idle" as the pose source. ~30 min. No purchase. | BUILD |
| Officer gestures | Mixamo free — "Pointing Forward", "Standing Arguing", "Acknowledge" — retarget to UE4 Manny first (~45 min one-time), then to Quantum. Zero cost. | FREE |
| Grenadier throw | **COVERED** — Kubold Rifle Animset Pro has `Rifle_Grenade_Throw_Single/_Far/_Close/_Cancel`. No additional purchase. | COVERED |
| Sniper laser Niagara | **Niagara Laser Beam Weapon V3** — ~$30-40 [?]. Fab: https://www.fab.com/listings/b776d3d2-5d87-4f8b-8466-d7bbb17ab2a4 | BUY-NOW |
| Grenade warning ring | BUILD — Niagara Decal Renderer emitter, expanding ring on circle material. ~1h. Not purchasable. | BUILD |
| Audio barks | **Cafofo Military Voice Pack PRO** — ~$20 [L], 2,426 phrases + radio variants. Epic Launcher vault (not on Fab). | BUY-NOW (optional) |

---

## Confirmed Still-Valid Picks from Old Doc

| Pack | Old decision | New status |
|---|---|---|
| GASP (Epic, free) | BUY (free) | CONFIRMED — updated for UE5.7 in March 2026 with 400 new locomotion anims. DO FIRST. |
| VanillaLoop Cover Set (~$25) | BUY | CONFIRMED — VanillaLoop Fab seller page still active. Anims-only set (not the Cover System bundle `bb06916a`). Skeleton unconfirmed from their site [?] — verify before buying that it's UE4/UE5 Manny. |
| Kubold Rifle Animset Pro (~$60) | BUY | CONFIRMED — covers grunt/rusher/heavy/grenadier combat + grenade throw + rifle melee. |
| Kubold Cover Animset Pro (~$50) | BUY (now superseded by VanillaLoop decision) | SUPERSEDED — VanillaLoop is the decided pick. Kubold Cover is the fallback. |
| RamsterZ Stealth Finishers ($29.99) | BUY | CONFIRMED — price verified live at $29.99. FBX-only, UE4 Manny retarget ~15 min. |
| RamsterZ URL | old doc had wrong path | CORRECTED: https://www.ramsterzanimations.com/store-buy/p/stealth-finishers-knife-and-hand-fbx-only |

---

## The Two Build-Yourself Gaps (unchanged from old doc)

1. **Grenade warning ring** (`OnGrenadeTelegraph`) — Niagara Decal Renderer expanding ring. ~1h. Not purchasable anywhere.
2. **Suppression cower** (`OnSuppressedStateChanged`) — additive ABP blend to low-cover crouch when `bSuppressed`. ~30 min. Mixamo "Crouching Idle" as source pose.
3. **Shield-carry locomotion** (new) — additive left-arm-forward poses over Kubold Pistol locomotion. ~2-4h. No pack exists.
4. **LMG braced idle + belt reload** (new) — GASP heavy-carry + additive poses. ~1-2h. No pack exists.

---

## Caveats Before Buying

- **VanillaLoop skeleton** — confirm UE4/UE5 Manny before purchasing. Their FAQ page returned 404; verify on the Fab listing directly.
- **JKMotion price** — old doc estimate ~$40; Fab 403 prevents live confirm. Check before checkout.
- **UE5.7 compatibility** — Kubold packs list up to 4.27/5.x; animation assets forward-compat across minor versions but test in a throwaway project. GASP is explicitly 5.7-updated.
- **Cafofo Military Voice PRO** — NOT on Fab; buy via Epic Games Launcher vault. Still works in UE5.

---

## Total Spend to Clear All BUY-NOW Items

| Item | Price |
|---|---|
| GASP | $0 |
| Kubold Rifle Animset Pro | ~$60 |
| Kubold Pistol Animset Pro (Shield gap Part A) | ~$50 |
| VanillaLoop Cover Set | ~$25 |
| RamsterZ Stealth Finishers | $29.99 |
| JKMotion Hit Reaction Pack | ~$40 |
| Outworld Riot Shield mesh | ~$15 |
| Niagara Laser Beam Weapon V3 | ~$35 |
| Cafofo Military Voice Pack PRO (optional) | ~$20 |
| **Total (without audio)** | **~$255** |
| **Total (with audio)** | **~$275** |

Build-only gaps (shield-carry loco, LMG braced, bolt-cycle montage, cower pose, grenade ring): ~6-8h animator time total, zero additional spend.
