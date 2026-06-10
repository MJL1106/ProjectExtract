# Companion Traversal — Status & Next Steps

Snapshot of where the `AI-Companion-Prototype` branch sits as of 2026-04-30. Pairs with `companion_traversal_mirror_handoff.md` (the asset-side wiring doc) and the original plan at `.claude/plans/i-need-to-improve-frolicking-forest.md`.

---

## What we built

### C++ infrastructure (all merged on this branch, all building cleanly)

- **`UCompanionTuningDataAsset`** — designer-tunable values for follow / mirror / warp behaviour. Asset: `DA_CompanionTuning`, assigned on `BP_CompanionAIController`.
- **Extended `FOnTraversalStarted` delegate** to FourParams: `(Type, PlayRate, ObstacleLocation, LandingLocation)`. Both broadcast sites in `UTraversalComponent` updated.
- **`ACompanionAIController`** binds to the player's `UTraversalComponent` on possess; populates blackboard keys when the player traverses; tears down cleanly in `EndPlay` / `OnUnPossess`.
- **`UBTTask_MirrorPlayerTraversal`** (new latent BT task) — gated by `BB_PlayerTraversalActive`. Approaches the player's obstacle, calls the companion's own `TryStartTraversal`, falls back to a per-obstacle teleport-to-landing if traversal can't start.
- **`UBTTask_FollowPlayer`** refactored to read all values from the tuning DA. Cached casts at ExecuteTask for tick-rate efficiency.
- **Two warp safety nets** on the controller:
  - **Soft warp** — > `WarpStuckTimeout` AND off-camera AND > `WarpMinDistance`.
  - **Hard warp** — 2× `WarpStuckTimeout` regardless of camera (last-resort guarantee).
  - **Z-mismatch warp** — companion Z differs from player Z by > 250 cm for > 3 s.
- **Comprehensive `LogTraversal` diagnostics** in `UTraversalComponent::PerformTraversalDetection` walk through every step (forward trace, facing dot, down trace, height calc, clearance check) so failures are unambiguous in the log.
- **Trace pawn-ignore** in all three traversal traces — fixes the bug where the companion's down trace hit the player's capsule when the player was standing on top of the wall, causing wrong-band detection (Mantle instead of Climb).

### Asset / BT wiring (done in editor)

- 3 new BB keys on `BB_Companion`: `PlayerTraversalActive`, `PlayerTraversalObstacle`, `PlayerTraversalLanding`. Enum key was skipped — the BT decorator doesn't need it.
- `Mirror When Player Traverses` Sequence wired as the leftmost child of the `Update Companion State` Selector with a Blackboard decorator (`PlayerTraversalActive Is Set`, Aborts: Both).
- `BP_CompanionAIController` Blueprint subclass created and assigned to `BP_Companion`, with `Tuning = DA_CompanionTuning` and `CompanionBehaviorTree = BT_Companion`.

---

## Where we are

### What works

- **Per-obstacle teleport fallback** is reliable. When `TryStartTraversal` fails or `CatchUpTimeout` fires, the companion physically appears on top of the obstacle next to the player.
- **Z-mismatch warp** kicks in when the companion gets stranded vertically.
- **Mirror branch wiring** is correct — BT decorator activates on player traversal, deactivates on completion. No more "split-second mirror then back to follow" bug.
- **Heights now match** between player and companion (post pawn-ignore fix). The companion picks the correct traversal type (Climb vs Mantle) when the trace succeeds.
- **Sprint stays on** through mirror task ticks (re-asserted each tick during the Approach phase).
- **Tuning is fully data-driven** — everything formation/mirror/warp-related is on the DA, no recompile loop.

### What still doesn't

- **Trace clearance fails on certain wall positions** — same obstacle, same companion, different Y → clearance OK on one spot, fails on another. Looks like wall-corner / capsule-fit interaction.
- **Companion doesn't face the obstacle squarely.** Approach angle is diagonal because the formation offset puts the companion to the player's right; the mirror task then computes the approach using the companion's offset position, so the companion grazes the wall at an angle. Trace can find the wall but clearance / down-trace start positions are sub-optimal.
- **No "drop down" behaviour** — companion can climb up but if it ends up on top of an obstacle and the player is on the floor, the only way down is the Z-mismatch warp.
- **Traversal animations are hit-and-miss** — a successful `TryStartTraversal` can still play a montage that slides off the wall (anim asset / root motion config), unrelated to the C++ logic.

---

## Strategic ideas (from the discussion just now)

### 1. Use the player's traversal direction, not the companion's offset

**One-line fix in `UBTTask_MirrorPlayerTraversal::ExecuteTask`.** Today the approach direction is computed from the companion's current (offset) position:

```cpp
ApproachDir = (PawnLoc - Obstacle).GetSafeNormal2D();
ApproachPoint = Obstacle + ApproachDir * 60;
```

Better: use the player's traversal direction, since `LandingLocation` is already on the BB:

```cpp
const FVector Landing = BB->GetValueAsVector(ACompanionAIController::BB_PlayerTraversalLanding);
const FVector PathDir = (Landing - Obstacle).GetSafeNormal2D();   // direction the player went
ApproachPoint = Obstacle - PathDir * Tuning->MirrorEdgeApproachOffset;
ApproachPoint.Z = PawnLoc.Z;
```

This puts the companion **on the same side the player came from, facing perpendicular to the wall along the same axis the player traversed**. Should fix most of the trace-fail / clearance-fail cases without any level work.

### 2. NavLinkProxy hybrid (Halo / Lyra pattern)

The original plan rejected `ANavLinkProxy` for the prototype — but it's the AAA-standard answer for production. Concerns about backtracking are unfounded:

- NavLinks are **bidirectional and hint-only**. The pathfinder uses them only when the optimal path requires one.
- Players exploring side rooms / backtracking simply path on the navmesh as normal; unrelated NavLinks are ignored.
- Designers drop one NavLink per authored obstacle. The AI uses them as part of normal pathfinding via `ReceiveSmartLinkReached` → call into `UTraversalComponent::TryStartTraversal`.

Long-term recommended layering:
1. **NavLinkProxy** on every authored climb/vault/mantle in shipped levels.
2. **Mirror task** as runtime fallback for whitebox, prototype, and unauthored geometry.
3. **Z-mismatch warp** as last-resort guarantee.

### 3. The traversal-detection edge cases

- The clearance failures we saw at certain Y positions are likely the wall edge / capsule-radius interaction. Could be debugged with the existing `bDrawDebugTraces=true` on the companion's `UTraversalComponent`. Worth a focused 30-min debug session.
- Capsule-size mismatch between player and companion is plausible but hasn't been confirmed. Compare `BP_ExtractionCharacter`'s and `BP_Companion`'s `CapsuleComponent` half-height + radius if the issue persists.

---

## Recommended order for tomorrow

1. **Apply the approach-direction fix** (idea #1). One-line change. Compare PIE results before/after.
2. **Drop a single `ANavLinkProxy`** on one obstacle as an A/B test — feel the difference between an authored link and the dynamic mirror task. Gives you a real basis for the production decision.
3. **If clearance still fails after #1**: short focused debug pass with `bDrawDebugTraces=true` on the companion's `UTraversalComponent` to see exactly what the clearance test is hitting.

## What's solid and shouldn't change

- The DA-driven tuning architecture
- The mirror BT branch + decorator wiring
- The pawn-ignore in all three traces
- The Z-mismatch warp safety net
- The teleport-to-landing fallback

These are the bones of a working AAA-style follower system. The remaining work is angle / geometry tuning, not architectural.
