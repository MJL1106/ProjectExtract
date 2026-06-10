# UE5 Cover System Research — Official Patterns

*Researched 2026-05-15 for ProjectExtract companion AI (UE5.7, multiplayer FPS)*

---

## TL;DR — Recommendations for This Project

- **Lyra has no cover AI.** `BT_Lyra_Shooter_Bot` uses a shallow BT; cover is unimplemented. Do not look to Lyra for reference here.
- **Pure procedural EQS cover (SimpleGrid + Trace) reliably fails** because the trace origin is at item-floor-level, not at character eye/weapon height. A single trace test cannot distinguish "crouching behind a knee-wall (good)" from "standing in the open (bad)". The EQS Trace test must be supplemented with a height check.
- **The production pattern for cover is hybrid: authored cover point actors + EQS ActorsOfClass query.** Authored points store per-slot metadata (cover height, lean directions, fire offset); EQS does the spatial selection and scoring against that authored data.
- **Smart Objects are the modern UE5 replacement for authored cover point actors**, but they are not production-stable in the companion AI workflow (BT + non-Mass flow) until UE5.4+. In UE5.7 they are stable. The migration is worthwhile for new cover slots but is non-trivial to retrofit.
- **BT + EQS is still the correct architecture for a single companion.** Mass/StateTree is for crowd-scale (50+ agents). Keep BT; fix the EQS query configuration and add a post-selection height validation step.

---

## 1. Lyra Cover — What Lyra Actually Has

**Lyra has no cover system.**

`B_AI_Controller_LyraShooter` (C++ base: `ALyraPlayerBotController`) runs `BT_Lyra_Shooter_Bot` on BeginPlay after Experience Ready. The behavior tree is a minimal ShooterCore bot: it handles respawn routing and basic enemy pursuit. No cover-seeking tasks, no EQS cover queries, no crouch/peek poses.

Lyra's AI-relevant components on the pawn:
- `UAIPerceptionStimuliSourceComponent` — registers `AISense_Sight` and `AISense_Hearing` stimuli
- Navigation capsule sizing (so other AI path around the character)
- `AISense_Damage` reporting on hit

Lyra is the wrong reference for tactical cover AI. Its value is in GAS/ability authoring and modular game features, not AI combat behavior.

Sources: [x157 ShooterMannequin notes](https://x157.github.io/UE5/LyraStarterGame/ShooterMannequin), [x157 ShooterCore](https://x157.github.io/UE5/LyraStarterGame/ShooterCore/), [Lyra docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/lyra-sample-game-in-unreal-engine)

---

## 2. Smart Objects as Cover Slots — Concrete Patterns

### What Smart Objects Are

`USmartObjectComponent` placed on any actor registers a set of interaction *slots* into the `USmartObjectSubsystem` (a world subsystem). Each slot has:
- A world transform (the position/orientation an agent occupies when using the slot)
- State: Free / Claimed / Used
- Selection conditions (`FWorldConditionBase` subclasses — prereqs for an agent to be eligible)
- A `USmartObjectBehaviorDefinition` — the logic executed when the slot is in use
- Optional `FSmartObjectSlotDefinitionData` custom data (this is where you store cover metadata: height, lean offset, fire arc)

### Behavior Definition Types

Two shipped definition types:
- `UGameplayBehavior_BehaviorTree` — runs a sub-BT while the slot is occupied. Familiar to existing BT authors.
- StateTree task inheriting `UGameplayInteractionStateTreeTask` — the modern path; requires the Gameplay Interaction plugin and a StateTree schema set to `AIControllerStateTreeSchema` or `SmartObjectActorStateTreeSchema`.

### Claim / Use / Release Lifecycle (C++ API)

```
USmartObjectSubsystem::FindSmartObjects(...)   // spatial query → FSmartObjectRequestResult[]
USmartObjectSubsystem::Claim(Handle, UserData) // returns FSmartObjectClaimHandle — slot goes Yellow
  → agent navigates to slot transform
USmartObjectSubsystem::Use(ClaimHandle)        // slot goes Red, behavior definition starts
  → behavior executes (BT subtree or StateTree task)
USmartObjectSubsystem::Release(ClaimHandle)    // slot goes Green, behavior stops
```

Note: `Claim()` is deprecated as of UE5.3 in favour of `MarkSmartObjectSlotAsClaimed()`. The handle type `FSmartObjectClaimHandle` is unchanged.

The Mass integration uses `FSmartObjectMassEntityUserData` (not `FSmartObjectActorUserData`) — these are incompatible user data types. For a non-Mass BT companion, use `FSmartObjectActorUserData`.

### Cover Slot Authoring Pattern

1. Place a `ASmartObjectActor` (or add `USmartObjectComponent` to an existing wall/crate actor).
2. In the `USmartObjectDefinition` asset, add slots at the cover positions (crouching slot at 60cm height, standing lean-left slot at 180cm, etc.).
3. Attach `FSmartObjectSlotDefinitionData` custom struct per slot: `ECoverHeight` (Crouch/Stand), `FVector PeekOffset`, `bool bCanFireLeft`, `bool bCanFireRight`.
4. Assign a `UGameplayBehavior_BehaviorTree` pointing to `BT_CoverBehavior` (the sub-tree that handles the peek/fire/wait loop).
5. In the AI's outer BT: run `BTTask_FindSmartObject` (built-in) → move to claimed slot transform → `BTTask_UseSmartObject` → on flank trigger, `BTTask_ReleaseSmartObject` and re-query.

### EQS Integration with Smart Objects

Use the `ActorsOfClass` generator targeting `ASmartObjectActor` (or a tag-filtered subclass) rather than a SimpleGrid. This means the candidate set is only authored cover positions, eliminating the "random floor point passes the trace test" failure mode. Apply a standard Trace filter (`BoolMatch=false`, from enemy context) as a secondary filter on top of the authored set.

Sources: [Smart Objects Overview UE5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/smart-objects-in-unreal-engine---overview), [Smart Objects Quick Start](https://dev.epicgames.com/documentation/en-us/unreal-engine/smart-objects-in-unreal-engine---quick-start), [USmartObjectSubsystem API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/SmartObjectsModule/USmartObjectSubsystem/GetSmartObjectComponent), [FSmartObjectClaimHandle](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/SmartObjectsModule/FSmartObjectClaimHandle), [Gameplay Behaviors reference](https://www.zomgmoz.tv/unreal/Smart-Objects/Gameplay-Behaviors), [Smart Objects terminology](https://zomgmoz.tv/unreal/Smart-Objects/Smart-Objects-terminology)

---

## 3. Modern UE5 AI Architecture — BT vs StateTree + Mass

### Where Each Belongs

| System | Scale | Cover Fit |
|---|---|---|
| BT + EQS | 1–10 agents | Single companion: yes, correct tool |
| StateTree (solo) | 1–10 agents | Alternative to BT; better data binding, more designer-friendly. No intrinsic cover advantage over BT. |
| Mass + StateTree | 50–500+ agents | Crowd NPCs; over-engineered for one companion |
| Mass + Smart Objects | 50–500+ agents | Crowd NPCs claiming cover slots at scale |

### StateTree Production Status

StateTree shipped as experimental in UE5.0. Community consensus (backed by Epic's own roadmap card "Production-Ready StateTree") is that **UE5.4 is the first version stable enough for production use**. UE5.5 added breakpoints, state enable/disable toggles, and improved transition debugging. UE5.7 (this project) is fully stable.

StateTree does **not** replace BT for tactical companion AI. It is a better tool when:
- Designers need to iterate on states without C++ recompile
- You want data-binding between evaluators, conditions, and tasks without blackboard boilerplate
- You are integrating Smart Objects (the Gameplay Interaction plugin's tasks are StateTree-native)

### Epic's Stated Direction (Unreal Fest 2023, 2024)

Epic's Unreal Fest 2023 talk "State Trees and Smart Objects: Data-Driven State Machine Workflows for Open World AI Designs" positions StateTree + Smart Objects as the preferred path for open-world AI interactions going forward. BT is not deprecated — it remains the default for straightforward combat AI where the task graph is stable. The Smart Ant demo (Unreal Fest Bali 2025) shows StateTree + Smart Objects enabling parallel iteration by multiple designers on the same AI behavior without conflicts.

For a single companion in a multiplayer FPS, the practical recommendation from Epic's talks is: **stay on BT for combat logic; use Smart Objects (with either BT subtrees or StateTree tasks as the behavior definition) for authored interaction slots like cover positions**.

Sources: [Unreal Fest 2023 StateTree talk](https://dev.epicgames.com/community/learning/talks-and-demos/mox7/unreal-engine-state-trees-and-smart-objects-data-driven-state-machine-workflows-for-open-world-ai-designs-unreal-fest-2023), [Smart Ant Unreal Fest Bali 2025](https://forums.unrealengine.com/t/talks-and-demos-smart-ant-building-ai-behavior-with-state-tree-and-smart-objects/2705288), [StateTree Deep Dive Unreal Fest 2024](https://forums.unrealengine.com/t/talks-and-demos-statetree-deep-dive-unreal-fest-2024/2286214), [StateTree Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-state-tree-in-unreal-engine)

---

## 4. The Five Cover Building Blocks — Epic / Industry Decomposition

These are the canonical decomposition from Game AI Pro (Matthew Jack, Chapter 26: Tactical Position Selection) and the level design / AI community. UE5 does not ship a cover framework that names these explicitly, but every real cover system implements all five.

### (a) Cover Slot Authoring — Authored vs Procedural

**Authored (preferred for tactical shooters):** Level designers place cover actors/slots. Each slot stores: world position, cover height enum (Crouch/Stand), lean directions (Left/Right/None), fire offset vector (where the muzzle exits cover). This is what Smart Objects, Kythera, and games like Gears of War use.

**Procedural (EQS SimpleGrid/PathingGrid + Trace):** The navmesh is sampled at runtime and a trace from the enemy determines occlusion. Fast to author (zero manual work), but fails because:
- The trace origin is at item ground level, not eye/weapon height — a knee-wall passes the trace test even though the AI's torso is exposed
- No lean metadata — the AI doesn't know which side of the wall to use
- No height metadata — the AI cannot determine whether to crouch or stand
- Stale slots — a valid cover position in the last query may be outflanked by the time the AI arrives

**Hybrid (production recommendation):** Authored slots for fixed geometry; EQS `ActorsOfClass` to spatially select among them; procedural fallback (SimpleGrid) only in open areas with no authored slots.

### (b) Hide Test — Is This Position Behind Cover From the Enemy?

The critical test. A naive EQS Trace test (`BoolMatch=false`, trace from enemy to item) fails for the reason above — it tests floor position, not body position.

The correct hide test uses **two traces**:
1. Trace from enemy eye height to item position + `FVector(0, 0, CrouchEyeHeight)` — if blocked, the slot is valid for crouching cover
2. Trace from enemy eye height to item position + `FVector(0, 0, StandingEyeHeight)` — if blocked, valid for standing cover; if not blocked but trace 1 is, this is crouch-only cover

This two-trace test cannot be expressed natively in a single EQS Trace node. Options:
- Run the EQS query to get candidates, then validate in C++ post-selection (current project pattern via `IsCoverTooTallToFireOver` is close to this, but inverted — it rejects tall cover rather than classifying cover height)
- Use a custom `UEnvQueryTest` C++ subclass that performs the dual-height trace
- Use authored Smart Object slots where the height classification is baked by the designer

### (c) Fire Test — Can I Shoot From This Position?

Determines peek/lean capability. Two variants:
- **Standing lean:** Can the AI step 60–80cm laterally left or right and have LoS to the enemy? Precomputed as `bCanFireLeft`/`bCanFireRight` on the slot, or computed at claim time with a lateral sphere trace.
- **Over-top:** Can the AI stand from a crouching cover slot and have LoS? Precomputed as `bCanFireOverTop` or tested with a trace from `StandingEyeHeight` at the slot position to the enemy.

The existing `IsCoverTooTallToFireOver` check in `BTTask_CompanionCombat` is the fire test — it is correct in concept but applied as a hard rejection rather than a slot classification. A slot classified as "tall cover, can fire left only" is more useful than one rejected entirely.

### (d) Movement to Slot

Standard `UAIBlueprintHelperLibrary::SimpleMoveToLocation` or `AAIController::MoveToLocation`. The slot transform (from authored cover point or Smart Object slot) provides the destination. Key details:
- Move to the slot's entry point, not the cover object's origin
- Set `bStopOnOverlap=true` and `AcceptanceRadius` matching the slot radius (typically 40–60cm)
- Cancel in-flight EQS query if the AI is interrupted mid-move (the existing `RequestID` pattern in `BTTask_MoveToCover` handles this correctly)

For Smart Object slots: move to `USmartObjectSubsystem::GetSlotTransform(ClaimHandle)` — the subsystem returns the authored slot transform directly.

### (e) Enter Pose / Peek / Leave

Three sub-states once the slot is reached:
1. **Enter pose:** Play montage (CrouchEnter or StandCover), set stance enum on AnimInstance
2. **Peek/fire loop:** Lateral or vertical offset applied to pawn transform for the lean; fire from offset; return to cover
3. **Leave:** Play montage (CrouchExit or UnCover), clear slot claim before moving

In UE5 BT terms this maps to:
- `BTTask_MoveToCover` → arrives → sets BB key `CoverSlot`
- `BTTask_CompanionCombat` → reads `CoverSlot`, calls `EnterCoverPose()` on `CompanionAnimInstance`, runs peek/fire loop
- On abandon trigger (flank/timer): `BTTask_CompanionCombat` exits → release claim → re-run cover EQS

The peek offset must be applied in world space from the slot's authored lean direction, not from the AI's current facing. This is the cause of the "fires into the wall" bug when the AI arrives at cover from an unexpected angle.

---

## 5. Concrete Recommendations for ProjectExtract

### Problem Diagnosis

The current system (`BTTask_MoveToCover` with EQS donut → `BTTask_CompanionCombat` with `EnterCoverPose`) has three compounding issues:

1. **EQS generator is wrong for cover.** A ring/donut around the querier samples floor points. The Trace test from the enemy passes floor-level occlusion, not body-level. Short walls accept because the floor behind them is occluded; tall walls reject because the floor is still exposed on the far side (or vice versa depending on exact trace offset). Neither result is reliable.

2. **No slot metadata.** Without authored lean direction, the fire offset is a guess. The AI fires from wherever it happens to be facing, which is often into cover geometry.

3. **No re-query debounce on arrival.** If the selected slot is invalid on arrival (enemy moved, slot flanked), the loop restarts immediately, causing the oscillation described.

### Recommended Fix — Phased

**Phase 1 (minimal change — fix the EQS query, keep BT structure):**
- Replace the donut EQS generator with `SimpleGrid` (`GridHalfSize=1200`, `SpaceBetween=150`) around the querier
- Keep the Trace test (`BoolMatch=false`, from enemy) as the primary filter
- Add a second EQS Trace test at eye height (custom `UEnvQueryContext` that offsets the trace origin by `FVector(0, 0, 120)`) to filter floor-only occlusion
- Add a `PathfindingLength` filter (`FloatValueMax=2500`) to discard unreachable points
- Post-selection: in `BTTask_MoveToCover::ExecuteTask`, after EQS completes, run `IsCoverTooTallToFireOver` (already exists) — classify as crouch or stand, store in BB, pass to `CompanionAnimInstance`
- Add a 3-second cooldown BB key before re-querying the same cover position

**Phase 2 (authored slots — correct architecture):**
- Place `USmartObjectComponent` on cover geometry actors (crates, walls, barriers) in the level
- Define `USmartObjectDefinition` per cover type (crate = 2 crouch slots, wall = 1 stand-left + 1 stand-right)
- Add `FSmartObjectSlotDefinitionData` custom struct: `ECoverHeight`, `FVector PeekOffset` (world-relative lean exit point), `bool bCanFireLeft`, `bool bCanFireRight`
- Change `BTTask_MoveToCover` EQS to use `ActorsOfClass` generator targeting `ASmartObjectActor`, claim via `USmartObjectSubsystem`, move to slot transform
- `BTTask_CompanionCombat` reads slot metadata for peek direction — eliminates fire-into-wall bug

**Phase 3 (optional — StateTree behavior definition):**
- Replace the BT subtree inside `BTTask_CompanionCombat`'s cover loop with a `UGameplayBehavior_BehaviorTree` or StateTree task attached to the Smart Object slot definition
- Allows designers to iterate on per-cover-type behavior (a sandbag crates crouches and peeks left; a pillar stands and leans right) without C++ changes

### Do NOT Switch to Mass/StateTree for the Companion

The companion is a single actor. Mass is designed for 50–500 agents. Migrating to Mass adds `FMassEntityHandle`, processor registration, fragment setup, and `FSmartObjectMassEntityUserData` — all for one AI. The cost does not justify the benefit. BT + Smart Objects (Phase 2 above) achieves the same cover quality without Mass.

### EQS Query Configuration — Corrected Cover Query

```
Generator: SimpleGrid
  GridHalfSize = 1200
  SpaceBetween = 150
  GenerateAround = EnvQueryContext_Querier

Test 1: Distance (Filter — discard points too close to self)
  DistanceTo = EnvQueryContext_Querier
  Purpose = Filter
  FloatValueMin = 200

Test 2: Trace (Filter — body-level occlusion from enemy)
  TraceFrom = EnemyContext
  BoolMatch = false
  TraceChannel = Visibility
  -- Configure trace to originate at enemy eye height via custom context --

Test 3: Pathfinding Length (Filter — reachability)
  Purpose = Filter
  FloatValueMax = 2500

Test 4: Distance from Enemy (Score — prefer closer cover for faster response)
  DistanceTo = EnemyContext
  ScoringEquation = InverseLinear
  ScoringFactor = 1.0
```

The custom context (`EnvQueryContext_CurrentCover` already exists in this project) should be extended or a second context added to offset the trace origin to eye height.

---

## Source Index

- [Smart Objects Overview (UE5.7)](https://dev.epicgames.com/documentation/en-us/unreal-engine/smart-objects-in-unreal-engine---overview)
- [Smart Objects Quick Start (UE5.7)](https://dev.epicgames.com/documentation/en-us/unreal-engine/smart-objects-in-unreal-engine---quick-start)
- [USmartObjectSubsystem API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/SmartObjectsModule/USmartObjectSubsystem/GetSmartObjectComponent)
- [FSmartObjectClaimHandle API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/SmartObjectsModule/FSmartObjectClaimHandle)
- [Gameplay Behaviors (zomgmoz)](https://www.zomgmoz.tv/unreal/Smart-Objects/Gameplay-Behaviors)
- [Smart Object terminology (zomgmoz)](https://zomgmoz.tv/unreal/Smart-Objects/Smart-Objects-terminology)
- [Unreal Fest 2023 — StateTree + Smart Objects](https://dev.epicgames.com/community/learning/talks-and-demos/mox7/unreal-engine-state-trees-and-smart-objects-data-driven-state-machine-workflows-for-open-world-ai-designs-unreal-fest-2023)
- [Smart Ant — Unreal Fest Bali 2025](https://forums.unrealengine.com/t/talks-and-demos-smart-ant-building-ai-behavior-with-state-tree-and-smart-objects/2705288)
- [StateTree Deep Dive — Unreal Fest 2024](https://forums.unrealengine.com/t/talks-and-demos-statetree-deep-dive-unreal-fest-2024/2286214)
- [StateTree Overview (UE5.7)](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-state-tree-in-unreal-engine)
- [EQS vs Cover Points forum debate](https://forums.unrealengine.com/t/eqs-vs-selective-cover-points/261453)
- [CoverGenerator-UE5 plugin (community, proof-of-concept)](https://github.com/teella/CoverGenerator-UE5)
- [Smart Objects and You — Medium Part 1](https://bigm227.medium.com/smart-objects-and-you-in-ue5-pt-1-what-is-smart-object-a9d3e579a077)
- [StateTree + EQS hide/flee tutorial](https://lilys.ai/en/notes/state-trees-in-ue5-20251021/ue5-state-tree-eqs-hide-flee)
- [Lyra Sample Game docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/lyra-sample-game-in-unreal-engine)
- [Game AI Pro Ch.26 — Tactical Position Selection (Matthew Jack)](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter26_Tactical_Position_Selection.pdf)
- [Level Design Book — Cover](https://book.leveldesignbook.com/process/combat/cover)
