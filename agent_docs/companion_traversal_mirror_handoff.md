# In-Engine MCP Handoff — Companion Traversal Mirror

C++ side is in (branch `AI-Companion-Prototype`). The in-engine agent needs to wire up the assets so the new feature actually runs in PIE.

The four steps below must all be done before testing. Order matters: do steps 1 → 2 → 3 → 4.

---

## 1. Add four new keys to `BB_Companion`

Open `/Game/Core/Blueprints/AI/Companion/BB_Companion.uasset`.

Add these four blackboard keys (names must match exactly — they are referenced by name from C++):

| Key Name | Type | Notes |
|---|---|---|
| `PlayerTraversalActive` | `Bool` | Default false. Flipped true by the controller when the player starts a traversal within `MirrorTriggerRange`; flipped false when the player's traversal ends or the mirror task finishes. |
| `PlayerTraversalObstacle` | `Vector` | World location of the wall/ledge surface the player just touched. Used by the mirror task to compute the companion's approach point. |
| `PlayerTraversalLanding` | `Vector` | World location where the player lands after the traversal. Reserved — currently informational; the mirror task uses Obstacle, not Landing. |
| `PlayerTraversalType` | `Enum` (`ETraversalType`) | Enum value: 0 None, 1 Vault, 2 Climb, 3 Mantle. From the existing enum in `Movement/TraversalTypes.h`. |

---

## 2. Wire the mirror branch into `BT_Companion`

Open `/Game/Core/Blueprints/AI/Companion/BT_Companion.uasset`.

Insert a **new top-priority Sequence** above the existing Combat / Revive / Follow branches (so it sits as the first child of the root selector):

```
Selector (existing root)
├── Sequence "MirrorPlayerTraversal"   (NEW — top priority)
│   ├── Decorator: Blackboard
│   │     Key: PlayerTraversalActive
│   │     Notify Observer: On Result Change
│   │     Observer Aborts: Both (or "Lower Priority and Self" if Both is unavailable)
│   │     Key Query: Is Set        (or `== true`)
│   └── Task: BTTask_MirrorPlayerTraversal   (the new C++ task)
├── Sequence "Combat" / Service "Combat" subtree   (EXISTING)
├── Sequence "Revive"                               (EXISTING)
└── Task: BTTask_FollowPlayer                       (EXISTING — root-level fallback)
```

Key points:
- The decorator MUST abort lower-priority work. Without that, the companion will finish whatever task it was running before noticing the player traversed.
- The new task `BTTask_MirrorPlayerTraversal` is exposed by the Extraction module — it should appear in the BT task picker after a hot-reload / editor restart.
- Do not touch `BTService_TraversalProbe` — it stays on the Follow subtree and runs as before. The mirror branch supersedes it via priority.

---

## 3. Create `DA_CompanionTuning`

Right-click in `/Game/Core/Blueprints/AI/Companion/` → **Miscellaneous → Data Asset → CompanionTuningDataAsset** → name it `DA_CompanionTuning`.

Open it and confirm the default values match (these are the C++ defaults — only change if you want a different tuning):

| Category | Field | Default |
|---|---|---|
| Formation | FormationOffsetBack | 350 |
| Formation | FormationOffsetRight | 200 |
| Formation | AcceptableRadius | 250 |
| Formation | SprintDistanceThreshold | 1000 |
| Mirror | MirrorTriggerRange | 1500 |
| Mirror | MirrorEdgeApproachOffset | 120 |
| Mirror | MirrorReachToleranceXY | 90 |
| Mirror | CatchUpTimeout | 4.0 |
| Warp | WarpStuckTimeout | 6.0 |
| Warp | WarpMinDistance | 2500 |
| Warp | WarpBehindOffset | 300 |
| Warp | WarpNavProjectExtent | 500 |
| Warp | RecentlyRenderedTolerance | 0.5 |

Save the asset.

---

## 4. Assign the DA to `BP_CompanionAIController`

Open `/Game/Core/Blueprints/AI/Companion/BP_CompanionAIController.uasset` (or whatever the companion's AI controller Blueprint is named).

Class Defaults → category **Companion | Tuning** → **Tuning** → assign `DA_CompanionTuning`.

If the controller is missing this property after the C++ change, the editor needs a Live Coding rebuild or a full editor restart so the new `Tuning` property is visible.

The same Blueprint already has `CompanionBehaviorTree` set to `BT_Companion` from before — do not change that.

If you find a warning in the log on PIE start that reads:

> `ACompanionAIController: Tuning DA is not assigned. Mirror + Warp behaviour will be disabled.`

…that means step 4 was missed. Set the DA and re-launch PIE.

---

## Verification on the obstacle course

After 1–4 are done, launch PIE on the obstacle-course test level. Watch `LogCompanionAI`:

1. **Bind confirmation:** `TraversalStarted bound to player BP_PlayerCharacter_C_*` should appear once on possess.
2. **Vault test:** sprint into a 50–90 cm crate. Expect `[MirrorTraversal] Execute — Obstacle=...` then `[MirrorTraversal] Approach reached ... — TryStartTraversal` then `[MirrorTraversal] OnTaskFinished (result=2)` (Succeeded).
3. **Climb test:** 80–170 cm ledge — same expectation.
4. **Mantle test (was failing before):** 170–260 cm wall. The companion should now sprint to the wall, play its mantle montage, and end up next to the player on top.
5. **Out-of-range guard:** if the player traverses while the companion is > 1500 UU away, the BB flag stays false and the mirror branch never fires (companion path-follows on navmesh / falls through to warp safety net).
6. **Warp safety net:** wedge the companion behind a closed door. After ~6 seconds with the camera facing forward, the companion should teleport to a navmesh point ~3 m behind the player. Look directly at the companion to suppress the warp (the rendered-recently guard).
7. **Hot tuning:** with PIE running, open `DA_CompanionTuning` and change `FormationOffsetBack` to 600. The companion's standoff distance should change on the next BT tick — no recompile.

If any of (2)–(4) still fail to mantle, the most likely tuning fix is to lower `MirrorEdgeApproachOffset` (currently 120). Try 80 or 60.

---

## Out of scope (do NOT do these now)

- NavLinkProxy placement on obstacles — explicitly rejected for this iteration in the plan. Mirror via player delegate is the chosen approach.
- Motion warping — not used. The existing `VaultSnapTarget` interpolation in `UTraversalComponent` is sufficient.
- Multiplayer authority — single-player prototype only. The remote-client `OnRep_TraversalType` broadcast carries zero-vectors for obstacle/landing on remote machines (the controller binds to the locally-controlled player only). Revisit when MP is being tested.
