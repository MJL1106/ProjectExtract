# Enemy Firing Recoil + Random Patrol Idle — Implementation Plan

> **For agentic workers:** In-engine UE5 work via MCP (VibeUE :8088 primary, NeoStack :9315 fallback) PLUS a thin C++ bridge (CLI only — never compile C++ through the editor MCP). Execute in-engine tasks via `ue5-inengine-agent`, one at a time (editor is serial). C++ task goes through `ue5-cpp-implementer` + safety/perf/edge reviewers + a green build + editor reboot. Verify by editor introspection + screenshot — NEVER run PIE; the user playtests. Steps use checkbox (`- [ ]`).

**Goal:** Replace the enemy fire-loop montage with KINEMATION's procedural recoil as a visuals-only additive layer, driven from the existing weapon fire edges the anim instance already computes, applied additively in `ABP_Enemy_Grunt`; plus random patrol-idle variation.

**Architecture:** `AC_RecoilAnimation` (pure-BP, stock `UActorComponent`, standalone) is added to the enemy actor BP. A thin C++ bridge in `UEnemyAnimInstance` fires four `BlueprintImplementableEvent`s (setup/play/stop/setaiming) at the equip + per-shot + stop + aim edges it already detects; the ABP implements them to drive the component, caches the component's output `FTransform` in the Update event (game thread → thread-safe), and applies it additively at the `MID-LBB → FINAL-LBB.BlendPoses_0` wire onto `hand_r`/`ik_hand_gun` + `spine_03/04`. The fire-loop montage is nulled so the C++ fire-align goes inert. Random patrol idle layers the present `Rifle_Patrol_Idle*` clips against the idle blendspace, gated on `bIsPatrolling`.

**Tech Stack:** UE5.7, KINEMATION Tactical Shooter Pack (content only), `Extraction` C++ module (thin anim bridge — no new module, no weapon-logic change), VibeUE/NeoStack MCP.

## Global Constraints

- **Visuals only** — weapon logic stays in `AWeaponBase` (fire/ammo/hitscan/fire-rate). Do NOT adopt KINEMATION weapon logic/input/fire-mode SM. The C++ added here is anim plumbing only.
- **Single-player only** — no replication of enemy recoil. Enemies fire server-side; the un-replicated transform is correct.
- **Out of scope:** `AC_IKMotionPlayer` breathing/idle sway; removing fire-align C++ (it self-neutralizes).
- Do NOT disturb the grip / locomotion / reload blend — recoil injects additively, downstream of all of them.
- Verify by screenshot/editor introspection. No PIE. No per-task git commits — the user manages commits on `Enemies`.
- Shared enemy ABP: `/Game/Core/Enemies/Animation/ABP_Enemy_Grunt`. Aim node `AO_Companion_Rifle02`. Enemy mesh offset 0,0,-86 / yaw-90.

## Recon-locked facts (from Task 1, `agent_docs/enemy_recoil_recon_notes.md`)

- `AC_RecoilAnimation` (`/Game/KINEMATION/Common/Recoil/AC_RecoilAnimation`): parent stock `UActorComponent`. API: `Init(NewRecoilData: UDA_RecoilData_C*, Firerate: double)`, `Play()`, `Stop()`, `SetAiming(IsAiming: bool)`, `SetFireMode(E_FireMode&)`. Output: `RecoilAnimation` (relative `Transform`). Self-ticks the pipeline. `ControllerRecoil` (Vector2D, camera) — ignore; its only owner-dependency is `Add Controller Pitch/Yaw Input`, a safe no-op on AI.
- **Standalone: yes** — no `BPI_TacticalShooter*` / kit-class dependency across all 26 graphs.
- ABP idle = `BS_Enemy_Rifle_Locomotion` blendspace (Speed/Direction), NOT a static idle clip → patrol clips layer against the blendspace.
- Injection wire: `MID-LBB (DB56).Pose → FINAL-LBB (BB89).BlendPoses_0` (~1632,144), downstream of aim + bone-masks, upstream of Output Pose.
- Bones: arm `clavicle_r→upperarm_r→lowerarm_r→hand_r`; spine `spine_01..05`; no `weapon_r` — weapon-attach is virtual bone `ik_hand_gun`. Sockets `WeaponSocket`/`WeaponSocket_Fire`/`WeaponSocket_Mk14`.
- Patrol clips on `SK_Military_Character_Skeleton` already. Present set is **5**: `Rifle_Patrol_Idle03/06/07/08/09` (00/01/02/04/05 deleted).
- Best recoil-data baseline: `RD_AK105` (`/Game/KINEMATION/TacticalShooterPack/Blueprints/Recoil/AK105/RD_AK105`) — real spread/pivots + 4 curves, SpaceRotation yaw already -90. (Base `DA_RecoilData` is an empty all-zeros template — do NOT use it.)
- Enemy actor BP: weapon spawned in C++ `AEnemyCharacter`; enemy BP EventGraph empty, no weapon component. `OnWeaponFired` is runtime-bound in `UEnemyAnimInstance` → bridge through the anim instance, not the actor BP.

---

### Task 1: In-engine recon — COMPLETE

Findings recorded above and in `agent_docs/enemy_recoil_recon_notes.md` + `agent_docs/recon_screens/ABP_Enemy_Grunt_AnimGraph.png`.

---

### Task 2: C++ bridge — anim-instance hooks + per-weapon recoil-data field

**Files:**
- Modify: `Extraction/Source/Extraction/Public/Data/WeaponDataAsset.h` (add one field)
- Modify: `Extraction/Source/Extraction/Public/Animation/EnemyAnimInstance.h` (4 BlueprintImplementableEvents + aim-edge tracking)
- Modify: `Extraction/Source/Extraction/Private/Animation/EnemyAnimInstance.cpp` (fire the events at existing edge sites)

**Interfaces produced (the ABP in Task 5 implements these):**
- `UWeaponDataAsset::EnemyRecoilData` — `TObjectPtr<UDataAsset>` (assign a `DA_RecoilData` per weapon; base type so C++ stays agnostic of the BP recoil-data class).
- `UEnemyAnimInstance` `BlueprintImplementableEvent`s: `BP_SetupRecoil(UDataAsset* RecoilData, float FireRate)`, `BP_PlayRecoil()`, `BP_StopRecoil()`, `BP_SetRecoilAiming(bool bAiming)`.

- [ ] **Step 1:** In `WeaponDataAsset.h`, under `// ---- Enemy Animation ----`, add:
  `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|Animation") TObjectPtr<UDataAsset> EnemyRecoilData = nullptr;` with a comment: visual-only recoil profile (a DA_RecoilData) the enemy anim bridge passes to AC_RecoilAnimation::Init. Null = no procedural recoil.
- [ ] **Step 2:** In `EnemyAnimInstance.h`, declare the four events:
  `UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Animation|Recoil") void BP_SetupRecoil(UDataAsset* RecoilData, float FireRate);` and likewise `BP_PlayRecoil()`, `BP_StopRecoil()`, `BP_SetRecoilAiming(bool bAiming)`. Add a private `bool bPrevIsAiming = false;`.
- [ ] **Step 3:** In `EnemyAnimInstance.cpp`, in the weapon-change block (the `if (Weapon != BoundFireWeapon.Get())` section, after `WeaponAnimType = DA->EnemyWeaponAnimType;`), call `BP_SetupRecoil(DA->EnemyRecoilData, DA->FireRate);` when `Weapon`/`DA` valid (and `BP_SetupRecoil(nullptr, 0.f)` when the weapon clears, to let the BP reset).
- [ ] **Step 4:** In `HandleWeaponFired()` add `BP_PlayRecoil();` (guarded by `if (!bIsAlive) return;` which already exists). Do NOT gate it on the loop montage — that guard is for the legacy single-fire montage path only.
- [ ] **Step 5:** At the `bIsFiring` falling edge (`else if (!bIsFiring && bPrevIsFiring)`, currently `StopFireMontage()`), add `BP_StopRecoil();`. On death (the `if (!bIsAlive)` early block) also call `BP_StopRecoil();` once so recoil resets before ragdoll.
- [ ] **Step 6:** After `bIsAiming` is finalised each frame, add `if (bIsAiming != bPrevIsAiming) { BP_SetRecoilAiming(bIsAiming); bPrevIsAiming = bIsAiming; }`.
- [ ] **Step 7 (review):** Dispatch `ue5-safety-reviewer` + `ue5-performance-reviewer` + `ue5-edge-case-reviewer` (brief edge-case with the goal + these files) in one message. Fix any CRITICAL/WARNING via `ue5-cpp-implementer`, re-review.
- [ ] **Step 8 (build):** Close ONLY this project's editor (scope by `Extraction.uproject`), build `ExtractionEditor Win64 Development` with `-WaitMutex`, confirm `Result: Succeeded` in the log, then reboot via `boot-engine`.
- [ ] **Verify:** Build green; the four events appear as implementable on the ABP; `EnemyRecoilData` appears on the weapon DA.

---

### Task 3: Rifle recoil data asset (shared profile)

**Assets:** Create `/Game/Core/Enemies/Animation/Recoil/RD_EnemyRifle` (duplicate of `RD_AK105`).

- [ ] **Step 1:** Duplicate `/Game/KINEMATION/TacticalShooterPack/Blueprints/Recoil/AK105/RD_AK105` → `/Game/Core/Enemies/Animation/Recoil/RD_EnemyRifle`.
- [ ] **Step 2:** Keep its curves/values as the starting baseline (tuned in Task 8).
- [ ] **Step 3:** On the rifle enemy weapon's `UWeaponDataAsset`, set `EnemyRecoilData = RD_EnemyRifle`. Save both.
- [ ] **Verify:** `RD_EnemyRifle` opens with the 4 AK curves; the weapon DA references it. Screenshot.

---

### Task 4: Add `AC_RecoilAnimation` to the enemy actor BP

**Assets:** Modify the `AEnemyCharacter` child BP the archetypes use (path confirmed via recon notes; under `/Game/Core/Enemies/Blueprints/`).

**Produces:** an `AC_RecoilAnimation` component on the enemy (self-ticking), reachable from the ABP via the owning pawn.

- [ ] **Step 1:** Add an `AC_RecoilAnimation` component to the enemy BP (component tick stays enabled — it runs its own pipeline).
- [ ] **Step 2:** Do NOT wire Init/Play here — that is driven by the ABP bridge events (Task 5). Save.
- [ ] **Verify:** Component present on the enemy BP; resolvable via GetComponentByClass from the ABP. Screenshot.

---

### Task 5: ABP implements the bridge events + caches the recoil transform

**Assets:** Modify `ABP_Enemy_Grunt` (Event Graph).

**Consumes:** Task 2 events, Task 4 component, Task 3 data.
**Produces:** BP var `CachedRecoilTransform` (`Transform`) set each Update — read by Task 6.

- [ ] **Step 1:** Implement `BP_SetupRecoil(RecoilData, FireRate)`: get the enemy's `AC_RecoilAnimation`, cast `RecoilData` to the recoil-data type, call `Init(castedData, FireRate)`. If `RecoilData` is null, skip (recoil disabled for that weapon).
- [ ] **Step 2:** Implement `BP_PlayRecoil` → component `Play()`; `BP_StopRecoil` → component `Stop()`; `BP_SetRecoilAiming(bAiming)` → component `SetAiming(bAiming)`.
- [ ] **Step 3:** In **Event Blueprint Update Animation** (game thread), read the component's `RecoilAnimation` and store into a new `Transform` var `CachedRecoilTransform`. Default it to Identity when the component is absent.
- [ ] **Step 4:** Save.
- [ ] **Verify:** Confirm the four events are implemented and `CachedRecoilTransform` is written each Update (drive `Play()` manually in preview and watch the var move). Screenshot the Event Graph.

---

### Task 6: Apply the recoil additively at the injection wire

**Assets:** Modify `ABP_Enemy_Grunt` (AnimGraph).

**Consumes:** Task 5 (`CachedRecoilTransform`), recon bones.

- [ ] **Step 1:** Break the `MID-LBB (DB56).Pose → FINAL-LBB (BB89).BlendPoses_0` link; insert a stock **Transform (Modify) Bone** (or **ApplyAdditive**) chain between them.
- [ ] **Step 2:** Apply `CachedRecoilTransform` (via PropertyAccess) to `hand_r` (rotation+location, **Add to Existing**, **Bone Space** or **Component Space** as the recon transform dictates) so the gripped weapon kicks; add a smaller proportional rotation to `spine_03`/`spine_04` for the upper-body push. Re-target from KINEMATION's `ik_hand_gun`/`VB recoil_hand_r` chain to these Military bones.
- [ ] **Step 3:** Confirm grip blend (`Blend Poses by bool` AK105/Mk14), locomotion, and montage slots remain upstream and unaffected.
- [ ] **Step 4:** Save.
- [ ] **Verify:** Drive a non-zero `CachedRecoilTransform` in preview → upper body/weapon kicks, hands stay on grip, legs/locomotion unchanged. Screenshot graph + posed preview.

---

### Task 7: Remove the fire-loop montage path

**Assets:** Modify the rifle enemy weapon `UWeaponDataAsset` (`EnemyAnimSet.FireLoop`) + `ABP_Enemy_Grunt` `FireMontage` fallback.

- [ ] **Step 1:** Null `EnemyAnimSet.FireLoop` on the rifle weapon DA and the ABP-level `FireMontage` → `GetEffectiveFireLoopMontage()` returns null, `PlayFireMontage` early-returns.
- [ ] **Step 2:** Confirm `FireAlignAlpha` interpolates to 0 (no loop montage) — inert, no code change.
- [ ] **Step 3:** Leave `WeaponFire` (gun-mesh bolt cycle) + `WeaponReload` untouched. Save the DA.
- [ ] **Verify:** FireLoop null; recoil additive is the only firing body motion. Screenshot the DA field.

---

### Task 8: Tune `RD_EnemyRifle` for the third-person body

**Assets:** Modify `RD_EnemyRifle`.

- [ ] **Step 1:** Drive recoil in preview; judge against the enemy mesh scale/offset — `RD_AK105` is FP-Operator-tuned and will likely be too large.
- [ ] **Step 2:** Scale pivots/spread/curve magnitude until it reads as a believable shoulder-fire kick, not a whole-body lurch; hands stay on grip, gun stays shouldered.
- [ ] **Step 3:** Save.
- [ ] **Verify:** Screenshot peak-recoil pose.

---

### Task 9: Random patrol idle (present 5 clips, layered on the idle blendspace)

**Assets:** Modify `ABP_Enemy_Grunt` (locomotion idle state).

**Consumes:** `Rifle_Patrol_Idle03/06/07/08/09`, existing `bIsPatrolling`.

- [ ] **Step 1:** In the idle state (where `BS_Enemy_Rifle_Locomotion` plays at Speed 0), add a patrol-idle overlay: an `int PatrolIdleIndex` chosen via `RandomIntegerInRange(0, N-1)` mapping to the present clips, feeding a Blend-Poses-by-int (or a small sub-state-machine, one state per clip) that plays the selected `Rifle_Patrol_Idle*`. Drive it as the idle pose when stationary; fall back to the blendspace as speed rises.
- [ ] **Step 2:** Re-roll `PatrolIdleIndex` on each loop completion; gate the whole patrol-idle path on `bIsPatrolling` so combat/alert idles are unaffected. Use a short blend between variants.
- [ ] **Step 3:** Index maps to the **present** clips only — picker auto-scales if the missing idles (00/01/02/04/05) are restored later. Save.
- [ ] **Verify:** Toggle `bIsPatrolling` + step the selector in preview → different variants play, blend cleanly, exit to walk/combat without snapping. Screenshot.

---

### Task 10: User playtest checklist

Hand the user these (the user runs PIE):

- [ ] Sustained burst → upper body + weapon recoil additively; hands on grip; legs/locomotion unaffected; no per-shot pop/cant.
- [ ] Fire while strafing → recoil layers over locomotion; no foot-slide/pose snap.
- [ ] Reload → mag-drop reload still correct; no recoil during reload.
- [ ] Die mid-fire → recoil resets (no frozen offset) before ragdoll.
- [ ] On patrol, standing → cycles through patrol-idle variants, clean blends; transitions to walk/combat without snap.
- [ ] New-weapon sanity: set a different `EnemyRecoilData` on a weapon DA → recoil changes with no other wiring.

---

## Self-review notes

- Recon's 3 flags folded in: Task 3 baseline → `RD_AK105` (not empty `DA_RecoilData`); Task 9 → present 5 clips, picker auto-scales; trigger path → C++ anim-instance bridge (Task 2) instead of pure-BP, since `OnWeaponFired` is runtime-bound and the enemy BP has no weapon component.
- C++ is anim plumbing only (one data field + 4 BlueprintImplementableEvents + calls at existing edges) — no weapon-logic change, honoring "visuals only."
- Spec coverage: recoil component (T4), trigger from existing edges (T2/T5), additive injection downstream of aim (T6), fire-loop removal + fire-align inert (T7), per-weapon drop-in data (T3/T8, verified T10), random patrol idle (T9), MP/out-of-scope honored in constraints.
