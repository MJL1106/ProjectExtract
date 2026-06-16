# EXTRACTION — Enemy Detection / Awareness System

**Status:** Detection-tuning reference. Written 2026-06-16 on `Enemies`, immediately after the `PeripheralVisionAngleDegrees` half-angle fix.
**Scope:** Everything that decides "does this enemy detect the player, at what range, and from what angle." Two layers: (1) UE's `UAIPerceptionComponent` (sight + hearing stimuli), (2) the custom `UEnemyAwarenessComponent` suspicion meter layered on top.
**Companions:** `enemy_gameplay_as_built.md` (full roster numbers — note its §2 cone table predates the half-angle fix, see Known Issues), `enemy_design.md` (intent).

---

## Purpose

A placed enemy is `AEnemyCharacter` possessed by `AEnemyAIController`. Perception stimuli (sight/hearing) drive a per-target suspicion meter in `UEnemyAwarenessComponent`, which climbs `Unaware → Suspicious → Searching → Combat`. The radial awareness ring above each enemy reads `GetAwarenessMeter01()`. Detection has **three independent rear-detection paths** (below); the engine sight cone is only one of them.

---

## Key Files

- `Public/Enemy/EnemyAIController.h` / `Private/Enemy/EnemyAIController.cpp` — perception config (sight + hearing), team 1, BT/BB, awareness wiring.
- `Public/Enemy/EnemyAwarenessComponent.h` / `Private/Enemy/EnemyAwarenessComponent.cpp` — the suspicion meter, state ladder, all fill/decay math.
- `Public/Enemy/EnemyArchetypeData.h` — every designer-editable tunable (`UDataAsset`). The `.cpp` is empty defaults; values live in the `.uasset`s.
- `Public/AI/AITargetingStatics.h` / `Private/AI/AITargetingStatics.cpp` — `GetSightLocation` (head-first, for aim) and `GetVisibleBodyPoint` (head-EXCLUDED, for detection/fire-gate).
- `Private/Character/ExtractionPlayer.cpp:92` — `AExtractionPlayer::CanBeSeenFrom` override: routes player visibility through `GetVisibleBodyPoint` (head excluded).
- `Private/Weapon/WeaponBase.cpp:314` — `ReportNearMisses` → `NotifyShotAt` (the shot-at rear-alert path).
- `Private/Components/FootstepNoiseComponent.cpp:74`, `Private/Movement/TraversalComponent.cpp:751`, `WeaponBase.cpp:563/619` — noise emitters feeding hearing.

Archetype DataAssets (where designers tune): `Extraction/Content/Enemy/AI/DA_Enemy_{Grunt,Rusher,Heavy,Sniper,Officer,Grenadier,Shield}.uasset`.

---

## 1. Engine perception config (`AEnemyAIController`)

Two places set these: the **ctor** (hardcoded fallbacks) and **OnPossess** (DA override if a DA is set). OnPossess wins for every value it touches.

### Sight (`UAISenseConfig_Sight`)

| Field | Ctor value | OnPossess source | Notes |
|---|---|---|---|
| `SightRadius` | `2500` (`EnemyAIController.cpp:45`) | `DA->SightRadius` (`:85`) | Max range a stimulus can begin. Effective detection range. |
| `LoseSightRadius` | `3000` (`:46`) | `DA->LoseSightRadius` (`:86`) | Drop-sight range; also the far end of the suspicion DistFactor ramp. |
| `PeripheralVisionAngleDegrees` | `110` (`:47`) | `DA->PeripheralVisionDeg * 0.5f` (`:89`) | **HALF-angle from forward.** 110 ÷ 2 = 55 → **110° total FOV**. THE half-angle fix. |
| `AutoSuccessRangeFromLastSeenLocation` | `500` (`:48`) | hardcoded `500` (`:90`, NOT from DA) | **360° auto-sight within 500 cm of the last-seen point — a key rear-detection path.** |
| `SetMaxAge` | `5` (`:49`) | `DA->SightMaxAge` (`:91`) | Stimulus memory seconds. |
| `DetectionByAffiliation` | enemies+neutrals, not friendlies (`:50-53`) | — | Neutrals on so corpses (NoTeam) are discoverable. |

`SetDominantSense(UAISense_Sight)` at `:64`. Stimuli routed to `UEnemyAwarenessComponent::OnTargetPerceptionUpdated` via `OnTargetPerceptionUpdated.AddUniqueDynamic` (`:131`).

### Hearing (`UAISenseConfig_Hearing`)

| Field | Ctor value | OnPossess source |
|---|---|---|
| `HearingRange` | `2000` (`:56`) | `DA->HearingRange` (`:92`) |
| `SetMaxAge` | `3` (`:57`) | `DA->HearingMaxAge` (`:93`) |
| `DetectionByAffiliation` | enemies+neutrals, not friendlies (`:58-60`) | — |

**Important:** the ctor value `PeripheralVisionAngleDegrees = 110` (`:47`) is a *pre-fix* literal — it is NOT halved, so any enemy possessed with **no DA** runs a 220° total cone. Every placed enemy has a DA, so OnPossess (`:89`) corrects it; the ctor literal is dormant unless a DA is missing.

---

## 2. Custom suspicion meter (`UEnemyAwarenessComponent`)

Polled timer, not Tick: `UpdateInterval = 0.15s` (`EnemyAwarenessComponent.h:137`), started in `Initialize` (`.cpp:80`) with a random initial delay to de-sync the herd. Per-target bookkeeping in `FSuspicionTrack { Suspicion, bSighted, LastStimulusLocation, LastShotAtTime }` (`.h:78`).

### Compile-time constants (`EnemyAwarenessComponent.h`)

| Constant | Value | Line | Meaning |
|---|---|---|---|
| `UpdateInterval` | `0.15s` | `:137` | Meter tick cadence. |
| `SuspicionMax` | `100` | `:140` | Combat confirmation ceiling. |
| `NoiseSuspicionCap` | `99` | `:142` | **Hearing alone can never confirm Combat** (caps 1 below max). |
| `StillSpeedThreshold` | `25 cm/s` | `:144` | Below = "still" for fill. |
| `ShotAtRateLimit` | `0.4s` | `:139` | Per-instigator near-miss throttle. |
| `RecentDamageWindow` | `4s` | `:170` | Damage counts for threat weight / contact-hold. |

### Fill math — `ComputeSightFillRate` (`.cpp:635-672`)

Per second, for a **sighted** track:
```
fill/s = SuspicionFillRate * DistFactor * AngleFactor * SpeedFactor * StanceFactor
```
applied as `Track.Suspicion += ComputeSightFillRate * UpdateInterval` in `UpdateSuspicion` (`.cpp:575`).

- **DistFactor** (`.cpp:642-649`): `1.0` within `FullFillRange`; linear ramp `1.0 → DistFloor` from `FullFillRange` to `LoseSightRadius`. `DistFloor = 0.15` (compile-time `constexpr`, `.cpp:644`).
- **AngleFactor** (`.cpp:651-656`): `Dot = forward · toTarget`; `CosHalfFOV = cos(PeripheralVisionDeg * 0.5)`. `AngleAlpha = clamp((Dot - CosHalfFOV)/(1 - CosHalfFOV), 0, 1)`; `AngleFactor = Lerp(AngleEdgeFillFactor, 1, AngleAlpha)`. **Floors at `AngleEdgeFillFactor` (never 0)** — so once `bSighted` is true the meter keeps filling regardless of angle; the engine sight cone is what gates whether `bSighted` becomes true at all.
- **SpeedFactor** (`.cpp:658-661`): `SprintFillFactor` if `Speed ≥ SprintSpeedThreshold`; `StillFillFactor` if `Speed ≤ StillSpeedThreshold` (25); else `1.0`.
- **StanceFactor** (`.cpp:663-669`): `ProneFillFactor` if `IExtractionPlayerInterface::GetIsProne()`; else `CrouchFillFactor` if `bIsCrouched`; else `1.0`.

### Decay (`.cpp:587`)

Unsighted track: `Suspicion -= SuspicionDecayRate * UpdateInterval`; track removed at ≤ 0.

### Noise gain — `HandleHearingStimulus` (`.cpp:170-183`)

`Gain = Stimulus.Strength * NoiseSuspicionGain`, clamped to `NoiseSuspicionCap` (99). Omnidirectional — no angle/LOS test. Below Combat only.

### State ladder (`ApplySuspicionState` `.cpp:601`; enum `EnemyTypes.h:21`)

`Unaware(0) → Suspicious(1) → Searching(2) → Combat(3)`. Per 0.15 s tick, on the highest-suspicion track:

| Transition | Condition | Code |
|---|---|---|
| → Suspicious | `MaxSuspicion ≥ SuspiciousThreshold` (30) | `.cpp:622` |
| → Searching | `MaxSuspicion ≥ SearchingThreshold` (65) | `.cpp:614` (also moves InvestigateLocation) |
| → Combat (meter full) | sighted track reaches `SuspicionMax` (100) | `UpdateSuspicion` `.cpp:579` |
| → Combat (point-blank) | sighted ∧ `Dist ≤ AutoCombatRange` (350) | `.cpp:578-582` — **instant, bypasses meter** |
| → Combat (re-acquire) | in Searching ∧ clean own sight | `HandleSightStimulus` `.cpp:143-146` |
| Searching → Unaware | `TimeSpentSearching ≥ SearchDuration` (8 s) | `UpdateAwareness` `.cpp:301` |
| Combat → Searching | `TimeSinceLOSLost ≥ LostContactGrace` (8 s) ∧ no contact-hold | `UpdateCombat` `.cpp:551` |
| Suspicious → Unaware | `MaxSuspicion < SuspiciousThreshold` | `.cpp:629` |

On `EnterCombat`, all tracks reset to `Suspicion = 0` (`.cpp:682`) — so in Combat the meter widget returns a hardcoded `1.0` (`GetAwarenessMeter01` `.cpp:805`).

Other Combat-entry shortcuts (bypass the meter entirely): `NotifyDamaged` (`.cpp:204`, any hostile that damages you), `NotifyShotAt` with clear LOS (`.cpp:261`), global-alert wake-up to Searching (`.cpp:723`), body discovery → Searching (`.cpp:114`), squad relay → Searching (`.cpp:957`).

---

## 3. The three rear-detection paths

An enemy can register the player well off its forward vector through three distinct, independently-owned mechanisms:

| # | Path | Owner / file:line | Gate | What it does |
|---|---|---|---|---|
| **A** | **Hearing fills the meter omnidirectionally** | `HandleHearingStimulus` `.cpp:170-183`; emitters `FootstepNoiseComponent.cpp:74`, `WeaponBase.cpp:563`, `TraversalComponent.cpp:751` | None (no angle, no LOS) within `HearingRange` (2000) | Footsteps/gunfire/traversal add `Strength * NoiseSuspicionGain` (30/event) up to 99. Drives Suspicious/Searching from any direction; never alone confirms Combat. |
| **B** | **`AutoSuccessRangeFromLastSeenLocation` — 360° auto-sight** | `EnemyAIController.cpp:90` (hardcoded `500`) | Within 500 cm of the last-seen point | Engine grants a successful sight stimulus with **no cone test** near where the target was last seen. After one glimpse, the player can move behind the enemy and still be "seen" inside 500 cm. |
| **C** | **Engine sight cone width** | `EnemyAIController.cpp:89` `DA->PeripheralVisionDeg * 0.5f`; suspicion-fill mirror `EnemyAwarenessComponent.cpp:654` | Half-angle `PeripheralVisionDeg/2` from forward + LOS to a body point | The forward-arc sight. After the fix this is 110° total (was 220°). Only this path is narrowed by the half-angle fix. |

**Plus two shot-driven rear alerts** (not "detection" but rear-reactive): `NotifyShotAt` (near-miss within `NearMissRadius`, `WeaponBase.cpp:342-355`) and `NotifyDamaged` — both ignore the cone entirely and snap to Combat/Searching from any angle.

---

## 4. The cone / angle math (every DotProduct gate)

Three places compute a forward-vs-target dot against `cos(PeripheralVisionDeg * 0.5)`:

1. **Suspicion fill AngleFactor** — `ComputeSightFillRate` `EnemyAwarenessComponent.cpp:653-655`. Floors at `AngleEdgeFillFactor`, never gates to zero.
2. **Combat contact-hold FOV gate** — `UpdateCombat` `.cpp:493-495`. When perception drops LOS, Combat is held only if the body-point trace is clear AND `Dot ≥ CosHalfFOV` (same half-angle). Mirrored again in the debug log branch `.cpp:540-544`.
3. **Debug cone draw** — `UpdateAwareness` `.cpp:375-382` draws `PeripheralVisionDeg * 0.5` as the `DrawDebugCone` half-angle.

All three already use `* 0.5f`, so they were correct before the fix. **Only the engine `UAISenseConfig_Sight` (`EnemyAIController.cpp:89`) was the doubled one.** Pre-fix, the engine cone (220°) was twice as wide as the suspicion-fill cone (110°) — so a target in the 110–220° band got `bSighted = true` from the engine but received the floored `AngleEdgeFillFactor` fill rate. That mismatch is now gone.

---

## 5. Body-point / head-safe targeting (`AI/AITargetingStatics`)

- `GetVisibleBodyPoint(Target, ObserverEye, IgnoreActor, OutPoint)` (`AITargetingStatics.cpp:39-90`) — traces `ECC_Visibility` against a low→high ladder **pelvis → spine_03 (chest) → neck_01**, **head intentionally excluded** (`:50`, comment). Returns the lowest visible point. Used by:
  - player detection — `AExtractionPlayer::CanBeSeenFrom` (`ExtractionPlayer.cpp:105`),
  - enemy aim — `AEnemyCharacter::GetAimPointForTarget` (`EnemyCharacter.cpp:399`),
  - combat contact-hold — `UpdateCombat` (`EnemyAwarenessComponent.cpp:489`).
- `GetSightLocation(Target)` (`.cpp:16-37`) — head bone first, then `GetPawnViewLocation`, then actor centre. Used for *aim point preference*, NOT the detection gate.

Consequence: a head-only crouch-peek over cover is NOT detectable (the head isn't a candidate) and is never aimed at — confirmed by the `CanBeSeenFrom` override routing through `GetVisibleBodyPoint`.

---

## 6. Distance / range factors

- **Effective detection range = `SightRadius`** (2500 default). A stimulus cannot start beyond it.
- **`LoseSightRadius`** (3000) — drop range AND the far anchor of the fill ramp.
- **DistFactor** (`ComputeSightFillRate` `.cpp:642-649`): full `1.0` inside `FullFillRange` (1500), then linear down to `DistFloor 0.15` at `LoseSightRadius`. So a sighted target at the edge still fills at 15 % rate — detection at long range is *slow*, not *impossible*.
- **`AutoCombatRange`** (350) — sighted target inside this snaps straight to Combat (`.cpp:578`).
- **`AutoSuccessRangeFromLastSeenLocation`** (500, hardcoded) — 360° re-sight radius around the last-seen point (path B above).

Worked example (Grunt defaults, centred, walking, standing): fill = `40 * 1.0 * 1.0 * 1.0 * 1.0 = 40/s` → Suspicious (30) in ~0.75 s, Searching (65) in ~1.6 s, Combat (100) in ~2.5 s. At the cone edge: `40 * 0.35 = 14/s` → Combat in ~7 s. Inside 350 cm: instant.

---

## 7. Tunable inventory ("detects too easily / too far / cone too wide / 360° rear")

DataAsset rows are designer-editable per archetype in `Extraction/Content/Enemy/AI/DA_Enemy_*.uasset`. C++ rows need a recompile.

| Knob | Current (default) | Location | Editability | Raise → / Lower → |
|---|---|---|---|---|
| `PeripheralVisionDeg` | 110 (total FOV) | `EnemyArchetypeData.h:58` (DA) → `EnemyAIController.cpp:89` ×0.5 | **DataAsset** | **Cone width.** Lower = narrower forward arc, harder to see from the side. |
| `SightRadius` | 2500 | `EnemyArchetypeData.h:48` (DA) | **DataAsset** | **"Too far."** Lower = shorter detection range. |
| `LoseSightRadius` | 3000 | `EnemyArchetypeData.h:51` (DA) | **DataAsset** | Drop range + fill-ramp tail. Lower shortens the slow long-range band. |
| `FullFillRange` | 1500 | `EnemyArchetypeData.h:55` (DA) | **DataAsset** | Distance held at full fill. Lower = fill slows sooner with range. |
| `AutoSuccessRangeFromLastSeenLocation` | **500 (hardcoded, not from DA)** | `EnemyAIController.cpp:90` | **C++** | **360° rear re-sight radius (path B).** Lower (or 0) kills near-range rear auto-detection. |
| `AutoCombatRange` | 350 | `EnemyArchetypeData.h:154` (DA) | **DataAsset** | Instant-Combat point-blank radius. Lower = no free instant kills up close. |
| `SuspicionFillRate` | 40/s | `EnemyArchetypeData.h:138` (DA) | **DataAsset** | **"Too easily."** Master fill speed. Lower = slower to detect. |
| `SuspicionDecayRate` | 15/s | `EnemyArchetypeData.h:142` (DA) | **DataAsset** | Cooldown speed. Raise = forgets faster. |
| `SuspiciousThreshold` | 30 | `EnemyArchetypeData.h:146` (DA) | **DataAsset** | Turn-to-face point. |
| `SearchingThreshold` | 65 | `EnemyArchetypeData.h:150` (DA) | **DataAsset** | Investigate point. |
| `AngleEdgeFillFactor` | 0.35 | `EnemyArchetypeData.h:162` (DA) | **DataAsset** | Edge-of-cone fill floor. Lower = side detection much slower (but never 0 once sighted). |
| `SprintFillFactor` | 1.6 | `EnemyArchetypeData.h:170` (DA) | **DataAsset** | Sprint visibility penalty. |
| `StillFillFactor` | 0.85 | `EnemyArchetypeData.h:166` (DA) | **DataAsset** | Standing-still slowdown. |
| `CrouchFillFactor` | 0.5 | `EnemyArchetypeData.h:178` (DA) | **DataAsset** | Crouch stealth bonus. Lower = stealthier. |
| `ProneFillFactor` | 0.35 | `EnemyArchetypeData.h:182` (DA) | **DataAsset** | Prone stealth bonus. |
| `SprintSpeedThreshold` | 500 cm/s | `EnemyArchetypeData.h:174` (DA) | **DataAsset** | Speed counted as sprinting. |
| `HearingRange` | 2000 | `EnemyArchetypeData.h:64` (DA) | **DataAsset** | **Omnidirectional rear path A radius.** Lower = quieter rear awareness. |
| `NoiseSuspicionGain` | 30/event | `EnemyArchetypeData.h:158` (DA) | **DataAsset** | Per-noise suspicion. Lower = noise alerts less. |
| `NoiseSuspicionCap` | 99 | `EnemyAwarenessComponent.h:142` | **C++** | Hearing ceiling (never confirms Combat). |
| `SightMaxAge` | 5 s | `EnemyArchetypeData.h:61` (DA) | **DataAsset** | Sight stimulus memory. |
| `DistFloor` | 0.15 | `EnemyAwarenessComponent.cpp:644` | **C++** | Long-range fill floor. |
| `UpdateInterval` | 0.15 s | `EnemyAwarenessComponent.h:137` | **C++** | Meter cadence (perf, not balance). |

---

## Patterns

- **DA-override-on-possess:** ctor sets safe fallbacks, `OnPossess` overwrites from `UEnemyArchetypeData` then calls `RequestStimuliListenerUpdate()` (`EnemyAIController.cpp:94`). New perception knobs must be set in *both* places or DA-less enemies misbehave.
- **Half-angle convention:** `PeripheralVisionDeg` is authored as a FULL FOV everywhere it is read; every consumer applies `* 0.5f` (engine config, suspicion fill, contact-hold, debug draw). Any new consumer MUST halve it.
- **Meter ≠ engine sight:** the engine cone decides `bSighted`; the suspicion meter decides *how fast* a sighted target confirms. They share the same half-angle but serve different gates.
- **Head-safe single source of truth:** all LOS/aim go through `AITargeting::GetVisibleBodyPoint` / `GetSightLocation` — never trace the head for detection.

---

## Known Issues / Gotchas

- **`AutoSuccessRangeFromLastSeenLocation` is hardcoded 500 in both ctor and OnPossess** (`EnemyAIController.cpp:48,90`) — it ignores the DA, so designers can't tune the 360° rear-resight radius without a C++ change. This is the highest-leverage uncovered rear-detection knob.
- **Ctor `PeripheralVisionAngleDegrees = 110`** (`:47`) is an un-halved literal — a DA-less enemy runs a 220° cone. Harmless while every enemy has a DA, but a trap if a child BP ships without one.
- **`enemy_gameplay_as_built.md` §2** lists "Cone 110°" — pre-fix that meant 110° *half-angle* (220° total) at the engine. Post-fix it is 110° *total*. Treat that doc's cone column as superseded by this one.
- **Hearing fill is uncapped by direction and only soft-capped at 99** — a player sprinting behind an enemy will reliably push it to Searching (65) from the rear. That is path A, not the cone; narrowing `PeripheralVisionDeg` does nothing to it.
