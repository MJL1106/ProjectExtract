# Enemy Anim Map — Grenadier

**Branch:** Enemies  
**Date:** 2026-06-18  
**Status:** Read-only mapping. No code changed.

---

## 1. Weapon & Rig

| Property | Value |
|---|---|
| Archetype | Grenadier (`bIsGrenadier=true`, DA: `DA_Enemy_Grenadier`) |
| Weapon | Infima Assault Rifle — `BP_EnemyAssaultRifle` / `DA_AssaultRifle` (same class as Grunt/Officer) |
| Pawn | `AEnemyCharacter` (shared by all 7 archetypes) |
| Anim BP | `ABP_Enemy_Grunt` (SK_Military_Character_Skeleton — Quantum rig) |
| Anim Instance class | `UEnemyAnimInstance` |
| Skeleton | `SK_Military_Character_Skeleton` (Quantum "Military Character") |
| Retargeters on disk | `RTG_RifleMannequin_to_Military` · `RTG_RifleAutoMannequin_to_Military` |

The Grenadier is a grunt with one bolt-on: `UEnemyGrenadierComponent` (created at possess, conditioned on `DA.bIsGrenadier`). Outside the throw window he fights identically to the Grunt — same rifle, same cover loop, same accuracy numbers (7°→1.5°, 0.5s reaction).

---

## 2. Current State — What Plays Today

### 2a. Standard combat set (all running)

All of these are already retargeted and assigned on `ABP_Enemy_Grunt`:

| Slot | Asset (Content/QuantumCharacter/Retarget/) | Plays via |
|---|---|---|
| Idle (stand) | `Idle/Mil_Rifle02_St_Idle00` | ABP blend |
| Locomotion stand | `Idle/Mil_Rifle02_St_{Walk,Run}_*_IPC` (8-dir blendspace) | `BS_Companion_Rifle02_Locomotion` |
| Locomotion crouch | `Crouch/Mil_Rifle_Cr_{Idle00,Walk_*_IPC}` (8-dir) | `BS_Companion_Rifle_Crouch` |
| Aim offset | `AO/Mil_Rifle02_St_Aim_*` (17-pose grid) | `AO_Companion_Rifle02` |
| Cover idles | `Idle/{AM_Crouch_Cover_Left,Right,Mil_anim_CoverDown_*}` | ABP state |
| Fire loop | `Combat/AM_Companion_Fire_Loop` (via `Rifle01_St_Shoot_Auto_Loop`) | `EnemyAnimInstance::PlayFireMontage` |
| Single fire | `Combat/AM_Companion_Fire_Single` | `HandleWeaponFired` |
| Reload | `Combat/AM_Companion_Reload` (via `Rifle01_St_Reload_Auto`) | `PlayReloadMontage` |
| Hit react | `Combat/AM_Companion_HitReact_Aim` / `AM_Companion_hit_Idle` | `HandleHitReact` |
| Death | (ABP state — ragdoll physics asset) | `PlayDeathMontage` |
| Melee | `MeleeMontage` slot (unassigned on `ABP_Enemy_Grunt` — not used by Grenadier) | `HandleMeleePerformed` |

### 2b. Grenade throw — today's state: **nothing plays**

`UEnemyGrenadierComponent::TryThrowAt` (`EnemyGrenadierComponent.cpp:57–115`):
1. Arc-solves the lob trajectory.
2. Sets `bTelegraphing = true`.
3. Broadcasts `OnGrenadeTelegraph(PendingLandingLocation, TimeToImpact)` — **delegate fires, no BP listener**.
4. Sets a timer for `GrenadeTelegraphTime` (DA default: **1.0s**).
5. Timer fires `SpawnGrenade()` — projectile appears at actor origin with `PendingLaunchVelocity`.

No montage is called anywhere in this path. The grenade teleports out of the enemy's feet with zero body animation. The 1.0s telegraph window exists as a timer, not as a visible anim.

`UEnemyAnimInstance` has no `GrenadeMontage` field and no `PlayGrenadeMontage()` method (`EnemyAnimInstance.h:111–133`). The struct defines: `FireMontage`, `ReloadMontage`, `HitReactMontage`, `DeathMontage`, `MeleeMontage`, `TakedownReactionMontage`, `SingleFireMontage` — grenade throw is absent.

`BTTask_GrenadierLob::TickTask` (`BTTask_GrenadierLob.cpp:53–109`) calls `GrenComp->TryThrowAt(LastKnown)` and then waits `IsTelegraphing()` to drop — it does not touch the anim instance at all.

---

## 3. Required Anim Set

### 3a. Shared combat set (already exists — no action needed)

Idle, locomotion (stand + crouch), aim-offset, fire loop, single fire, reload, hit-react, death — all covered by the existing Quantum-native retarget set described in §2a. No gaps here for a standard AR soldier.

### 3b. Grenade throw — the missing set

The throw must visually sell the 1.0s telegraph window and confirm the throw itself. A three-clip sequence is the standard model:

| Clip purpose | Content in clip | Timing relationship |
|---|---|---|
| **Pin-pull / wind-up** | Weapon lowered or slung single-hand; free hand reaches into webbing, pulls pin, cocks arm back | Must complete within ~0.5–0.7s so the throw fires before the DA `GrenadeTelegraphTime=1.0s` elapses |
| **Throw** | Overhand throw — arm extends, weight shift forward, weapon still one-handed or slung | The grenade projectile should spawn at the anim notify in this clip (replaces the current timer-only spawn) |
| **Recovery** | Arm comes down, weapon returns to two-hand grip | Blends back to aim-offset idle; ~0.4–0.6s |

**What the throw montage must do:**
- Play as a full-body or upper-body additive slot over standing locomotion/idle.
- Last no longer than `GrenadeTelegraphTime + recovery` — i.e., ~1.5–2.0s total for the montage (1.0s wind+throw + ~0.5–0.8s recovery).
- Contain an `AnimNotify` at the throw peak — this is where `SpawnGrenade()` should fire (currently a dumb timer; a notify-driven approach is more anim-accurate and doesn't require changing the timer if precision is acceptable).
- Work on `SK_Military_Character_Skeleton` — either natively or via retarget from SK_Mannequin.

---

## 4. What Exists Per Clip

### 4a. Kit Granade set (FP, SK_Mannequin)

All four clips live at `Content/ProceduralFPSKIT/Character/Animations/WeaponAnims/Granade/`:

| Asset | Description | Usability for TP Grenadier |
|---|---|---|
| `Anim_Arms_Granade_Pinpull` | Arms only — pin pull, grip shift | Upper-body source for retarget; FP skeleton (SK_Mannequin arms only, no legs) |
| `Anim_Arms_Granade_Throw` | Arms only — overhand lob | Same skeleton caveat as above; this is the money clip |
| `Anim_Arms_Granade_Recovery` | Arms only — arm drops, regrip | Same |
| `Anim_Character_Granade_BasePose` | **Full character** reference pose for the granade set | SK_Mannequin full body — most retargetable of the four |

`Weapon/Anim_Weapon_Granade_Pinpull` and `Weapon/Anim_Weapon_Granade_BasePose` are weapon-mesh animations (SKM_Granade skeleton) — not relevant to the character.

**Caveat — FP arms skeleton:** `Anim_Arms_Granade_Pinpull/Throw/Recovery` are authored on the first-person arms skeleton (chest-and-up, no pelvis/legs). `RTG_RifleMannequin_to_Military` maps SK_Mannequin → SK_Military_Character_Skeleton. If retargeted, the lower body will be in a neutral/T-pose in the retarget clip; for an upper-body montage slot (arms slot, blended over locomotion) this is fine — the locomotion layer provides legs. For a full-body additive it will look broken.

**`Anim_Character_Granade_BasePose`** is the only clip on the full SK_Mannequin rig (all bones present). It is a static reference pose, not an animation — useful as a retarget anchor, not a standalone throw clip.

### 4b. Quantum-native retarget set

`Content/QuantumCharacter/Retarget/` contains the full rifle soldier set. **No throw or grenade clips exist here.** The set is AR-only: idle, locomotion, aim-offset, fire, reload, hit-react, cover idles. Adding a throw montage means either retargeting from the kit or sourcing a new clip.

### 4c. Retargeters available

`RTG_RifleMannequin_to_Military` (IK-based, Quantum IK rig `IK_MilitaryCharacter`) — maps SK_Mannequin → SK_Military_Character_Skeleton. This is the tool used for the existing companion retargets. It will handle the full-body base pose clip and the arms clips (with the upper-body-only caveat noted above).

---

## 5. Gaps

### [HAVE-RETARGET] — kit clips exist, retarget needed

1. **Throw clip** — `Anim_Arms_Granade_Throw` is the primary source. Retarget via `RTG_RifleMannequin_to_Military` → montage asset for the upper-body slot. Upper-body-only origin is acceptable given the montage will layer over the locomotion blend. **This is the single highest-value clip for the archetype.**

2. **Pin-pull** — `Anim_Arms_Granade_Pinpull`. Same retarget path. Plays before the throw in the montage (wind-up section). Not strictly required if the throw clip already contains a wind-up; assess after viewing the retarget.

3. **Recovery** — `Anim_Arms_Granade_Recovery`. Same retarget path. Optional but cleans up the return to rifle guard.

### [MUST-SOURCE] — does not exist in any form

4. **Slung/transition pose for the throw window** — the kit throw clips lower the weapon into a one-handed carry so the throw arm is free. The Quantum retarget will produce this automatically if the kit clip has the IK-compatible weapon-hand pose. If the retarget produces a broken left arm or a weapon clipping through the body, a short manual pose-override or a masked slot (right arm only) will be needed. Cannot know until the retarget is run.

5. **Full-body TP throw (nice-to-have)** — if the upper-body-only retarget looks disconnected (weight shift absent, feet planted robotically), a full-body throw from an asset pack or Mixamo + Quantum retarget would read better. Not a hard blocker — the upper-body layer is acceptable for an initial pass.

---

## 6. Wiring Plan

### 6a. C++ additions needed (`EnemyAnimInstance.h/.cpp`)

Add one montage field and one play function — mirrors the existing `MeleeMontage` pattern exactly:

```cpp
// EnemyAnimInstance.h — Montages block
UPROPERTY(EditDefaultsOnly, Category = "Enemy|Animation|Montages")
TObjectPtr<UAnimMontage> GrenadeMontage;

// EnemyAnimInstance.h — public
UFUNCTION(BlueprintCallable, Category = "Enemy|Animation")
void PlayGrenadeMontage(float PlayRate = 1.f);
```

`PlayGrenadeMontage` implementation follows `PlayMeleeMontage` verbatim.

### 6b. Trigger point

`UEnemyGrenadierComponent::TryThrowAt` already calls `OnGrenadeTelegraph.Broadcast(...)` at the start of the telegraph window. The cleanest hook is to add a **second delegate** or extend the existing one to also fire on the `AEnemyCharacter`. Two options:

**Option A (preferred — no new delegate):** Bind in `UEnemyAnimInstance::NativeInitializeAnimation` to a new `OnGrenadeThrow` delegate on `AEnemyCharacter` (parallel to `OnMeleePerformed`). `AEnemyCharacter` listens to `GrenadierComponent->OnGrenadeTelegraph` and re-broadcasts `OnGrenadeThrow`. Anim instance calls `PlayGrenadeMontage` in response.

**Option B (simpler but less clean):** `UEnemyGrenadierComponent::TryThrowAt` casts the owner to `AEnemyCharacter`, gets the anim instance, and calls `PlayGrenadeMontage` directly. Works but couples the component to the anim instance type.

Option A is consistent with how `OnMeleePerformed` and `OnHitReact` are wired (`EnemyAnimInstance.cpp:30–36`) and keeps the component decoupled.

### 6c. Throw timing and AnimNotify

- DA `GrenadeTelegraphTime = 1.0s`. The montage should cover pin-pull + throw arm peak within ~0.8s, with recovery extending past the 1.0s mark (it's fine for recovery to play after the grenade has spawned).
- Current spawn is timer-driven: `TelegraphTimerHandle` fires `SpawnGrenade()` after exactly `GrenadeTelegraphTime`. This is independent of the montage and will remain accurate regardless of PlayRate. No AnimNotify is required to keep the current spawn path — the timer already handles it.
- If a more anim-locked spawn is desired later (grenade appears in the hand socket before release), add a `UFUNCTION` AnimNotify that calls `GrenadierComponent->SpawnGrenade()` and cancel the timer on montage start. That is an optional upgrade, not required for the initial pass.

### 6d. Slot / layering

The throw montage should play in the **UpperBody** slot (same slot as `FireMontage` uses on `ABP_Enemy_Grunt`) so it layers over locomotion. If the retargeted upper-body clips look broken (disconnected hip/foot), switch to a full-body slot for the throw duration and accept that the enemy stops moving during the throw — appropriate given the throw is a combat interrupt.

### 6e. `CancelThrow` and montage abort

`UEnemyGrenadierComponent::CancelThrow` is already called by `BTTask_GrenadierLob::AbortTask`. Add `StopGrenadeMontage(0.2f)` to `AEnemyCharacter::HandleGrenadeCancelled` (new listener on `OnGrenadeCancelled`) so the wind-up blends out cleanly if the BT task is aborted mid-telegraph.

### 6f. ABP assignment

After creating the montage asset (retarget + montage wrapper), assign it on `ABP_Enemy_Grunt` → Details → `Enemy|Animation|Montages` → `GrenadeMontage`. Applies to all 7 archetypes; non-grenadiers will never trigger it (the component only exists when `bIsGrenadier=true`).

---

## Summary of Action Items (priority order)

| Priority | Item | Tag |
|---|---|---|
| 1 | Retarget `Anim_Arms_Granade_Throw` → SK_Military via `RTG_RifleMannequin_to_Military`; create montage | [HAVE-RETARGET] |
| 2 | Add `GrenadeMontage` field + `PlayGrenadeMontage()` to `UEnemyAnimInstance` | C++ (trivial) |
| 3 | Add `OnGrenadeThrow` delegate to `AEnemyCharacter`; bind in `UEnemyAnimInstance::NativeInitializeAnimation` | C++ |
| 4 | Retarget `Anim_Arms_Granade_Pinpull` and `Anim_Arms_Granade_Recovery`; add as sections to the montage | [HAVE-RETARGET] |
| 5 | Assess retarget quality — if upper-body origin reads broken in TP, source a full-body throw clip | [MUST-SOURCE] |
| 6 | Assign montage on `ABP_Enemy_Grunt`; add `GrenadeOut` bark lines to `DA_Barks_Grunt` (2 lines) | In-editor |
| 7 | Bind `OnGrenadeTelegraph` to the landing indicator BP (separate visual task — already flagged in `enemy_gameplay_as_built.md §11.1 #3`) | In-editor |
