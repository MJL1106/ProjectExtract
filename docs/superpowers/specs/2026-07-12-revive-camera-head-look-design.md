# Player-to-Companion Revive Camera Design

## Goal

During a player-initiated companion revive, the camera follows the player's animated face in first person, bounded look input turns the visible head and upper torso with the view, and the player actor never performs the current 180-degree start/end rotation.

## Evidence

- `BP_ExtractionCharacter` currently attaches `SpringArm` beneath `CharacterMesh0` with no socket; the camera therefore does not follow the head or face.
- `AExtractionPlayer::CalcCamera` pins the revive view to the standing-eye position captured before the montage.
- `AExtractionPlayer::SetReviverLock` explicitly writes actor yaw to control yaw plus 180 degrees and restores actor yaw when the revive ends.
- `AM_Reviver_Player` has constant authored root orientation and identity extracted root-motion samples, so it does not generate the transient 180-degree world rotation.
- Runtime logs show procedural dynamic montages replacing `AM_Reviver_Player` during the first revive frames.

## Design

### Camera ownership

On local revive start, save the spring arm's existing parent, socket, relative transform, and pawn-control-rotation flag. Reattach it to the player mesh's head bone with `KeepWorldTransform` and retain the resulting runtime head-relative transform for the hold. The pre-revive camera is already at the intended eye position, so this follows the face without an entry snap or a hardcoded offset. The spring arm uses pawn control rotation during the hold, so control rotation remains the view authority while the head supplies the animated camera position.

On every exit path—completion, cancellation, player DBNO, or teardown—restore the saved attachment and settings exactly once. The camera must not remain head-attached after the revive.

### Look blending

The existing anim instance already exposes `AimYaw` and `AimPitch`, calculated from base aim rotation relative to actor rotation. The player AnimBP applies a revive-only final skeletal-control layer after the revive montage slot. Yaw and pitch are distributed across upper spine, neck, and head, with the head receiving the strongest share. The layer is active only while `IsRevivingTarget()` is true and uses the existing bounded revive look cone.

This makes the mesh and its shadow visibly follow the camera while avoiding unnatural full-body twist.

### Orientation and pair alignment

The player actor remains at its pre-revive yaw for the whole hold. Remove the `+180` seat rotation and all look-cone math that compensates for it. Correct the reviver montage's visual orientation locally in the animation graph/asset, then derive the companion's paired placement from the unchanged player actor frame.

No `SetActorRotation` call may run on the reviving player at revive entry or exit.

### Procedural montage arbitration

The procedural FPS kit must not start locomotion/action dynamic montages while `IsRevivingTarget()` is true. The revive montage owns the full-body slot for the hold. Remove the per-tick replay workaround once the source arbitration is gated; a montage that is unexpectedly interrupted should log once and cancel cleanly rather than being restarted every frame.

## Edge cases

- Releasing interact early restores the original camera hierarchy and actor controls.
- The player entering DBNO mid-hold performs the same cleanup before DBNO camera handling takes ownership.
- Revive completion does not grant the reviver post-revive damage immunity.
- Missing head bone, spring arm, or montage fails safely and restores the normal camera.
- Remote characters retain authoritative actor yaw; the change is cosmetic to the local view and skeletal aim presentation.
- The camera must not start inside the player's face; the existing revive near clip may remain as a safety measure after the face offset is validated.

## Verification

- Trigger `CompDown`, hold revive, and move the mouse across the permitted yaw/pitch range.
- Camera position follows the animated head throughout the kneel instead of remaining at standing height.
- The visible head and shadow turn with the camera without twisting beyond the bounded range.
- Actor yaw remains stable from the frame before revive through the frame after completion/cancellation.
- No procedural `AnimMontage_####` replaces `AM_Reviver_Player` during the hold.
- Completion, early cancellation, and reviver-DBNO paths restore the original camera hierarchy.
