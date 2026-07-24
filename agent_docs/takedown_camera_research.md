# Takedown Camera Clipping — Implementation Research

## Problem Statement

During the neck-stab finisher, the player's camera (attached at the head bone via a zero-length SpringArm) physically enters the enemy's skull and torso mesh at point-blank range (~90 cm victim snap distance). The near clip plane sits inside the victim's geometry, rendering inverted backfaces and interior mesh — "seeing through" the enemy. The fix must preserve the head-bone camera attachment; detaching the SpringArm from the head or switching to control rotation is prohibited, as a prior attempt to do so broke the rig entirely.

---

## Correction (verified against UE 5.7 headers after research)

The research below names `UCameraComponent::CustomNearClippingPlane` /
`bOverride_CustomNearClippingPlane` as a per-camera BP property. **That is wrong** —
those members exist only on `USceneCaptureComponent2D`, not on the player camera.
The "Primary" near-clip step as originally written (set the property on
`FirstPersonCamera` in BP) is not possible.

Correct per-view near-clip control: `FMinimalViewInfo::PerspectiveNearClipPlane`
(`Camera/CameraTypes.h`; negative → use the global `GNearClippingPlane`, positive →
override for that view).

**Implemented as:** an override of `AExtractionPlayer::CalcCamera` (a virtual on
`AActor`; neither `APawn` nor `ACharacter` overrides it). It calls `Super::CalcCamera`
(which populates `OutResult` from `FirstPersonCamera`), then while
`bTakedownMontageActive` sets `OutResult.PerspectiveNearClipPlane = TakedownNearClipPlane`
(a tunable `UPROPERTY`, default 2 cm; `<= 0` disables). This is **pure C++ on the
player** — no camera subclass and no BP camera reparent, so the near-clip layer does
NOT touch the camera rig. "Do NOT Do #3" still holds: avoid the global
`r.SetNearClipPlane` CVar; the scoped path is the `CalcCamera` override.

The control-rotation reseed in "Edge case 3" is intentionally NOT implemented in C++ —
post-montage head drift is an AnimBP/montage blend concern on this project, not a C++
fix (see `pitfall_fp_camera_head_bone`).

---

## Rig Reality (verified in source)

**Live pawn:** `BP_ExtractionCharacter` is a Blueprint child of `AExtractionPlayer` (`ACharacter` subclass). `AExtractionPlayer` does NOT create a SpringArm or camera in C++. The SpringArm and `FirstPersonCamera` are Blueprint-side components authored in the kit BP, not C++ — they are not in `AExtractionPlayer`'s constructor.

**`AExtractionCharacter`** (the legacy sibling class, `ExtractionCharacter.cpp:74-83`) does create a SpringArm with:

```
SpringArm->SetupAttachment(GetMesh(), FName("head"))
SpringArm->TargetArmLength = 0.0f
SpringArm->bDoCollisionTest = false
SpringArm->bUsePawnControlRotation = true
SpringArm->bInheritPitch = false
SpringArm->bInheritYaw = false
SpringArm->bInheritRoll = false
```

**Confirmed via editor MCP 2026-06-24 (the live BP does NOT mirror the legacy class — it is the opposite):** `FirstPersonCamera` → `SpringArm` → `CharacterMesh0`, with `SpringArm.bUsePawnControlRotation=FALSE`, `bInheritPitch=TRUE`, `bInheritYaw=TRUE`, `bInheritRoll=FALSE`, `TargetArmLength=0`, `bDoCollisionTest=FALSE`, and `FirstPersonCamera.bUsePawnControlRotation=FALSE`. So the camera's world ORIENTATION = the head bone's orientation (inherit), and the head bone is driven by the AnimBP aim-offset (fed by control-rotation delta) in normal play, and by the takedown montage during the finisher (the procedural aim-offset layer is gated off by `IsInTakedown()`). This matches `pitfall_fp_camera_head_bone`.

**Implication for technique selection:** both the camera ORIGIN and its ORIENTATION ride the head bone. During the finisher the montage drives the head, so the visible camera follows the montage. The look-input lock (`DoAim` early-return) freezes the control-rotation that feeds the aim-offset (so nothing the player does steers the camera into the victim); the near-clip reduction (`CalcCamera`) is orientation-agnostic; and the victim head bone-hide removes the geometry the camera origin passes through. These three are what the implemented fix uses.

**Takedown seams (`ExtractionPlayer.cpp`):**

- `StartMontageDeferred` (line 651): snaps victim to `PlayerLoc + PlayerFwd * TakedownVictimForwardOffset` (default 90 cm), plays `TakedownMontage` on the player mesh's `AnimInstance`, binds `OnTakedownMontageEnded` as end delegate, then calls `OnTakedownStarted(Victim)`.
- `OnTakedownMontageEnded` (line 730): always calls `OnTakedownFinished()` first (camera/gun/knife restore), then fires `FinishPendingTakedown()` if the kill notify hadn't already fired.
- `UAnimNotify_TakedownKill` fires at ~1.65 s; calls `FinishPendingTakedown()` from the notify.
- `EndPlay` (line 167): sets `bTakedownMontageActive = false` and kills the victim — guards against pawn-destroy leaving a frozen enemy.
- `IsInTakedown()` (line 171): `BlueprintPure`, returns `bTakedownMontageActive`. AnimBP reads this to gate the procedural arm layer.

---

## How AAA FPS Games Handle This

Three distinct approaches appear across the genre:

**1. Keep first-person, author the animation around the clip geometry**
Dishonored and Riddick remain fully first-person throughout stealth kills; the art team authors the finisher so the hands/blade occupy foreground space while the victim stays at a non-clipping distance or the view angle is titled away. This is craft-side avoidance, not a runtime fix. Far Cry 5/6 also default to first-person takedowns. None of these titles have published the runtime technique, but the community mod work (Immersive First Person View for Skyrim, which hides the player head mesh by skeletal partition to prevent the head-bone camera sitting inside the player's own geometry) confirms the "hide the problematic geometry" approach is a known shipped-adjacent pattern.

**2. Cut to a cinematic or third-person camera for the kill window**
The majority of AAA melee-finisher games (Skyrim kill-cams, God of War, GTA, The Last of Us) cut to a scripted external camera for the execution. `SetViewTargetWithBlend` is the UE5 API for this. It is bulletproof against clipping by construction but breaks the first-person fantasy.

**3. Baked camera offset authored offline against the victim mesh**
Confirmed as the canonical UE community pattern for head-bone FP rigs: Froyok's "True First Person Camera in UE4" (republished on GameDeveloper.com) documents the exact scenario — for animations/montages, blend controller pitch toward 0 and/or apply a baked local offset, so the authored animation drives the head while the procedural aim-offset doesn't fight it. Camera curves baked in Sequencer and applied as additive offsets on top of the base camera post-process step (CameraShakeBase, UCameraModifier, or CameraAnimationSequence) are the Epic-documented delivery mechanism.

---

## Ranked Recommendations for This Project

### Primary — Aim-offset input lock + per-camera near-clip reduction

**What it does:** Suppresses look input during the montage so the player cannot steer the camera into the victim; separately reduces the near clip plane so residual authored-angle geometry renders instead of punching a hole.

**Why primary:** It is the lowest-code, highest-reversibility path. The `IsInTakedown()` gate already exists and is purpose-built for this (per the inline comment in `ExtractionPlayer.h:170`). The near-clip change is three lines in BP.

**C++ change (minimal):**

In `AExtractionPlayer::DoAim` (`ExtractionPlayer.cpp:348`):

```cpp
void AExtractionPlayer::DoAim(float Yaw, float Pitch)
{
    if (!IsValid(GetController())) return;
    if (bTakedownMontageActive) return;   // ADD THIS LINE

    AddControllerYawInput(Yaw);
    AddControllerPitchInput(Pitch);
}
```

`bTakedownMontageActive` is already set before `OnTakedownStarted` fires (line 693) and cleared before `OnTakedownFinished` fires (line 724 via `FinishPendingTakedown`). No new state needed.

**BP wiring (in `BP_ExtractionCharacter` event graph):**

`OnTakedownStarted`:
1. Get `FirstPersonCamera` component (or whatever the kit BP names it).
2. Set `bOverride_CustomNearClippingPlane = true`.
3. Set `CustomNearClippingPlane = 1.5` (cm).
4. (Optional) drive a Timeline 0→1 over 0.15 s → `SetSocketOffset(Lerp(Zero, FVector(-35, 0, 8), Alpha))` on the SpringArm — pulls the camera endpoint 35 cm back/8 cm up during the finisher.

`OnTakedownFinished`:
1. Set `bOverride_CustomNearClippingPlane = false` (restores to global default).
2. (Optional) play the Timeline in reverse back to zero SocketOffset.

**APIs:**
- `UCameraComponent::bOverride_CustomNearClippingPlane` (bool) + `UCameraComponent::CustomNearClippingPlane` (float) — UE5.7 confirmed, per-camera, does not affect other viewports or scene captures.
- `USpringArmComponent::SocketOffset` (FVector) — applied in the arm's rotated frame; `-X` is always view-forward regardless of whether rotation comes from controller or inherit.
- `bTakedownMontageActive` (private bool in `AExtractionPlayer`) — must be exposed via a getter or the BP guard must be implemented via `IsInTakedown()` + `DoAim` override in BP.

**Side effects:** Dropping near clip from ~10 cm to 1.5 cm compresses Z-buffer precision toward the far plane during the ~1.65 s window. Acceptable given the duration; restore immediately in `OnTakedownFinished`. The SocketOffset pullback moves the camera endpoint backward with `bDoCollisionTest=false`, so a player standing against a wall during the finisher will have the pulled-back camera endpoint clip through the wall behind them — keep the offset small (≤40 cm) and/or skip it.

**Reversibility:** Total. Both changes are gated behind `bTakedownMontageActive` (C++) and `OnTakedownFinished` (BP). `EndPlay` already clears `bTakedownMontageActive = false` (line 189), so a mid-finisher pawn-destroy cannot leave the near clip stuck.

**Multiplayer note:** `bOverride_CustomNearClippingPlane` is a per-camera instance property — it affects only the local client's camera, never the server or remote clients. `DoAim` is already gated to `IsLocallyControlled()` implicitly (input only fires locally). No replication needed.

---

### Secondary A — Per-section victim mesh hide during the kill window

**What it does:** Hides the victim's head and upper-torso mesh sections that the camera enters — the body remains visible from the waist down, so the stab still reads as landing on something.

**Why secondary, not primary:** It leaves the camera physically inside the victim's empty space; near-plane geometry from the victim's collar/shoulder that isn't section-hidden can still clip. Use alongside the primary.

**BP wiring (no C++ change):**

`OnTakedownStarted(Victim)`:
1. Cast Victim to the enemy base BP class; get its `SkeletalMesh` component.
2. `HideBoneByName(FName("head"), PBO_Term)` — hides the head bone and its children.
3. `HideBoneByName(FName("neck_01"), PBO_Term)`.
4. Cache the victim ref in a BP variable for restore.

`OnTakedownFinished`:
1. `UnHideBoneByName(FName("head"))`.
2. `UnHideBoneByName(FName("neck_01"))`.

`HideBoneByName` / `UnHideBoneByName` are USkeletalMeshComponent Blueprint functions, UE5.7 confirmed. `PBO_Term` terminates physics simulation on children (appropriate for a held victim). Cheaper and more targeted than `SetVisibility(false)` on the whole mesh.

**Correctness note:** `UAnimNotify_TakedownKill` fires from the player's `TakedownMontage`, not the victim's montage, and `SetVisibility`/`HideBoneByName` does not stop the victim's `AnimInstance` ticking — so the paired victim montage timing and the kill at 1.65 s are unaffected.

**Multiplayer note:** Call only under `IsLocallyControlled()` guard or on the cosmetic client path — bone-hiding is a render-only operation, do not replicate it.

---

### Secondary B — Authored montage camera orientation (animation choreography)

**What it does:** Authors the player finisher montage so the head bone (and therefore the camera origin) pitches slightly downward and/or to the side rather than directly into the victim's neck — the blade approach is angled, not dead-on, so the near-clip plane geometry is narrowed from "full face" to "collar edge."

**Why secondary:** It requires per-montage authoring discipline and breaks if the victim snap or the enemy archetype heights change. It is not a guaranteed fix — a steep enough pitch into a tall enemy's neck can still clip. Use as a refinement layer on top of the geometric fixes.

**Authoring constraint:** The snap places the victim at `TakedownVictimForwardOffset = 90 cm` directly on the camera forward axis (`ExtractionPlayer.h:238`). To reduce clip area the head pose should pitch ~15–25° downward toward the victim's shoulder, not into the forehead. The AnimBP's aim-offset fights this unless the montage slot fully overrides the spine/head in the blend graph — verify the montage slot mask covers at least `spine_03` through `head`.

**Per-archetype note:** Grunt, Officer, Sniper, Shotgun, Rusher, Grenadier all have different heights. An offset validated on the Grunt clips on the Sniper's taller frame. A per-archetype `TakedownVictimForwardOffset` DataAsset (or per-archetype montage variant) is the correct data-driven answer per the project's "no hardcoded tuning" rule.

---

### Safety-net — Fade-to-black on the snap frame

**What it does:** Calls `PlayerCameraManager->StartCameraFade(0, 1, 0.05, Black)` immediately before `BeginTakedownHold` snaps the victim (i.e., before `OnTakedownStarted`), then fades back in over 0.1 s once the victim is snapped and the camera is stable. Hides the discrete pop of the snap.

**Why safety-net only:** It cannot cover the full ~1.65 s held-stab window. A held black screen is not a finisher, it is a loading screen. Use only to mask the 1–2 snap frames.

**Wiring note:** The C++ `StartMontageDeferred` (line 651) calls `BeginTakedownHold` (line 690) and then `OnTakedownStarted` (line 718). To mask the snap frame, the fade call must happen before `BeginTakedownHold` — this requires a small C++ change (call the fade in `StartMontageDeferred` before line 690) or a pre-event BP hook that doesn't currently exist. The existing `OnTakedownStarted` fires *after* the snap, so a BP-only fade placed there misses the snap entirely.

---

## Do NOT Do

**1. Detach the SpringArm from the head bone** (or re-socket it to a different bone at runtime). A prior attempt to detach the camera from the head broke the rig entirely. `FAttachmentTransformRules` socket-switching may be valid API, but it is forbidden on this project.

**2. Switch to `bUsePawnControlRotation=false` or enable `bInheritPitch/Yaw`** on the SpringArm at runtime. The kit BP's procedural animation system depends on the current control-rotation / arm-inherit state. Toggling these mid-game is untested and can decouple the aim-offset from the camera origin in ways that do not cleanly reverse.

**3. Use `r.SetNearClipPlane` (global CVar)** instead of `UCameraComponent::CustomNearClippingPlane`. The CVar mutates the near plane for all views on the local machine including scene captures and a secondary split-screen viewport if one exists. The per-camera property is scoped and reversible.

**4. Apply the UE5 "First Person Primitive" rendering feature** (`FirstPersonPrimitiveType` / `FirstPersonScale` on `UCameraComponent`) to solve this. That feature prevents the player's FP-tagged geometry (arms, knife) from clipping into world surfaces — it does not move the camera origin, does not change the near clip, and does nothing to the enemy mesh. The clip artifact is a camera-origin-inside-enemy problem; this feature is a viewmodel-depth problem. They are different problems.

**5. Use `FOV` adjustment as a clip-prevention mechanism.** Changing FOV changes frustum width at a given depth; it does not move the near clip plane or the camera origin. At a fixed 90 cm victim distance, narrowing the FOV cannot un-embed a near plane already inside the enemy body. (Locking FOV during the finisher for artistic consistency is fine; do not frame it as a fix.)

---

## Edge Cases to Resolve Before Shipping

### Edge case 1 — Victim ragdoll at the kill notify (1.65 s)

`UAnimNotify_TakedownKill` calls `FinishPendingTakedown()` which calls `Victim->FinishTakedownKill(this)`. If `FinishTakedownKill` enables ragdoll physics on the victim, the victim's torso can fling toward the camera — potentially re-introducing the clip the fixes suppressed. The `HideBoneByName` safety-net helps here; if the head/neck are hidden they cannot clip even while ragdolling. Confirm whether `FinishTakedownKill` enables ragdoll immediately (clip risk) or defers to the montage end (safer). If immediate ragdoll: keep bone-hide active until `OnTakedownFinished`, not until the kill notify.

### Edge case 2 — Interrupted finisher leaving state stranded

`OnTakedownMontageEnded` (line 730) always calls `OnTakedownFinished()` before `FinishPendingTakedown()`. This covers natural end and montage interruption. Confirm the end delegate fires on blend-out (`OnMontageBlendingOut`) vs. full end (`OnMontageEnded`) semantics: UE5 `Montage_SetEndDelegate` binds to `OnMontageEnded` (fires after blend-out completes), not `OnMontageBlendingOut`. A 0.2 s blend-out means the camera near-clip and bone-hide stay locked for 0.2 s after the stab animation visually finishes — acceptable, but document the expectation.

If the player pawn is destroyed mid-finisher (e.g., dies to a grenade while executing the takedown), `EndPlay` (line 167) clears `bTakedownMontageActive = false` and kills the victim. However, `OnTakedownFinished` is a `BlueprintImplementableEvent` — it will NOT be called from `EndPlay`. Any state the BP set in `OnTakedownStarted` (near-clip override, bone-hide, knife visibility) will be left active on a destroyed pawn, which may be harmless (pawn is gone) or may persist on the camera manager if the player respawns. Add an explicit `RestoreFinisherCamera()` call from the BP's `Event End Play` that mirrors `OnTakedownFinished` exactly, as an idempotent guard.

### Edge case 3 — Aim-offset re-assertion pop at handback

When `bTakedownMontageActive` clears and `DoAim` resumes accepting input, the controller rotation reflects the direction it was locked at when the montage started. If the player twitched the mouse during the locked window (input was discarded, not consumed), the controller rotation is clean. However, the aim-offset's procedural layer (which `IsInTakedown()` gates off) re-asserts over one interp frame (~0.1 s at speed 10). If the montage's final head-bone orientation differs significantly from the aim-offset's neutral position, there is a 0.1 s view wobble. Mitigate: at the same tick `OnTakedownFinished` fires, reset the controller pitch to match the camera's current facing via `SetControlRotation(GetControlRotation())` — this seeds the controller at the camera's actual final position before the aim-offset starts re-applying.

### Edge case 4 — SocketOffset pullback behind a wall

With `bDoCollisionTest=false` on the SpringArm (confirmed in the C++ CDO), a SocketOffset of `FVector(-35, 0, 8)` moves the camera endpoint backward with no collision probe. A stealth takedown executed with the player's back against a wall will push the camera endpoint through the wall, briefly showing world interior/backfaces behind the player. Either: (a) skip the SocketOffset pullback entirely if the near-clip reduction alone suffices, or (b) add a manual trace in `OnTakedownStarted` from the SpringArm socket to the pullback target and clamp the offset to the trace hit distance.

---

## Implementation Checklist

### C++ (`ExtractionPlayer.cpp` — single change)

- [ ] Add `if (bTakedownMontageActive) return;` to `DoAim` before `AddControllerYawInput/PitchInput`. This is the look-input discard during the finisher.

### BP (`BP_ExtractionCharacter` event graph)

- [ ] `OnTakedownStarted`:
  - Set `FirstPersonCamera.bOverride_CustomNearClippingPlane = true`.
  - Set `FirstPersonCamera.CustomNearClippingPlane = 1.5`.
  - Call `HideBoneByName("head", PBO_Term)` on the victim's SkeletalMeshComponent.
  - Call `HideBoneByName("neck_01", PBO_Term)` on the victim's SkeletalMeshComponent.
  - Cache victim ref in a BP variable.
  - (Optional) Start a 0.15 s Timeline to lerp SpringArm SocketOffset to `(-35, 0, 8)`.
- [ ] `OnTakedownFinished`:
  - Set `FirstPersonCamera.bOverride_CustomNearClippingPlane = false`.
  - Call `UnHideBoneByName("head")` + `UnHideBoneByName("neck_01")` on cached victim ref (null-check: victim may already be destroyed by ragdoll; use `IsValid`).
  - (Optional) Play Timeline in reverse back to zero SocketOffset.
- [ ] `Event End Play`:
  - Mirror `OnTakedownFinished` logic (restore near-clip, un-hide bones on cached victim if still valid) — idempotent guard for mid-finisher pawn-destroy.

### Montage / asset work

- [ ] Verify the takedown montage slot mask covers `spine_03` → `head` so the montage fully overrides the aim-offset on those bones.
- [ ] Author the montage's head-bone pitch to approach the neck from slightly above/side (~15–20° off forward) rather than dead center. Validate per-archetype: Grunt first, then re-check on the Sniper (tallest) and Rusher (closest approach distance).
- [ ] Verify `UAnimNotify_TakedownKill` is placed at the correct frame in the montage (the debug log at `ExtractionPlayer.cpp:684` will warn if missing).
- [ ] Confirm whether `FinishTakedownKill` enables ragdoll immediately; if yes, extend `HideBoneByName` duration to `OnTakedownFinished`, not the kill notify frame.

### Data / config

- [ ] If takedown offset needs per-archetype tuning, add `TakedownVictimForwardOffset` to the enemy archetype `DataAsset` (or a `UTakedownConfigDataAsset` referenced by `AExtractionPlayer`) rather than per-montage hardcoding.

---

## Key Files

- `Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h` — `OnTakedownStarted`, `OnTakedownFinished`, `IsInTakedown()`, `bTakedownMontageActive`, `TakedownVictimForwardOffset`
- `Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp` — `StartMontageDeferred`, `FinishPendingTakedown`, `OnTakedownMontageEnded`, `DoAim`
- `Extraction/Source/Extraction/Public/Character/ExtractionCharacter.h` — legacy/sibling class; SpringArm/camera CDO defaults live here (reference for BP override expectations)
- `Extraction/Source/Extraction/Private/Character/ExtractionCharacter.cpp:74-88` — SpringArm constructor settings (head socket, zero-length, `bUsePawnControlRotation=true`, all `bInherit*=false`)
- `Extraction/Content/ProceduralFPSKIT/Blueprints/BP_ExtractionCharacter.uasset` — live pawn BP; `OnTakedownStarted`/`OnTakedownFinished` BP implementation lives here
