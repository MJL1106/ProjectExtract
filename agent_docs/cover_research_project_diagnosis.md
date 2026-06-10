# Companion Cover System — Fresh-Eyes Diagnosis

Status: outside read, no code changes. Goal: tell the truth about why the cover system keeps failing in practice, even though each individual commit looks reasonable.

Files in scope:
- `Public/AI/Tasks/BTTask_MoveToCover.h` + `Private/AI/Tasks/BTTask_MoveToCover.cpp`
- `Public/AI/Tasks/BTTask_CompanionCombat.h` + `Private/AI/Tasks/BTTask_CompanionCombat.cpp`
- `Private/AI/BTS/BTService_UpdateCompanionState.cpp`
- `Public/AI/CompanionAIController.h` + `Private/AI/CompanionAIController.cpp`
- `Public/Companion/CompanionTypes.h` (EPeekSide, ECompanionPosture)
- `Public/Animation/CompanionAnimInstance.h` + `Private/Animation/CompanionAnimInstance.cpp`
- `Private/AI/EQS/EnvQueryContext_CombatTarget.cpp`, `EnvQueryContext_CurrentCover.cpp`, `EnvQueryContext_Player.cpp`
- `Public/Companion/CompanionCharacter.h`

---

## 1. The actual state model

There is no single "cover state". Cover is two blackboard cells (`HasCoverPosition` bool + `CoverLocation` FVector) that anything in the tree can read or write, plus a parallel `bInCover` flag on the AnimInstance, plus three per-instance scratch variables on the combat task (`ResolvedPeekSide`, `LastPeekResolveCoverLoc`, `LastPeekResolveTargetLoc`). None of these are wrapped in a struct; none have an owner.

### Cover lifecycle, in calling order

1. **`BTService_UpdateCompanionState::TickNode` (every 250 ms)** — runs the perception pass. If the current `CombatTarget` becomes invalid, dead, or LoS-blocked, it `ClearValue`s `CombatTarget` and sets `HasCoverPosition=false`. If the target *changes* to a different actor, it also sets `HasCoverPosition=false`. So the service is a passive invalidator of cover, gated on target identity / liveness / LoS. It never writes `CoverLocation`.

2. **`BTTask_MoveToCover::ExecuteTask`** — entry point. Two branches:
   - If `HasCoverPosition` is already true (from a previous cycle): trust the existing `CoverLocation`, fire a `MoveToLocation`, wait for path-follower.
   - Else: run the EQS `CoverQuery` asset (designer-assigned), wait for `OnQueryFinished` callback. Callback writes `CoverLocation` and sets `HasCoverPosition=true`, then starts the move. If query returns zero items, sets `HasCoverPosition=false` and the next tick succeeds the task so the tree falls through to combat.
   - **No `AbortTask` override.** When the BT aborts this task (target lost, higher-priority branch wins), `Controller->MoveToLocation` keeps running and the `OnQueryFinished` callback can still fire — both will continue writing to BB after the task is dead. This is the two CRITICAL findings, and they are real.

3. **`BTTask_CompanionCombat::ExecuteTask`** — entered after MoveToCover succeeds. Reads `HasCoverPosition` + `CoverLocation`. Two paths:
   - If `IsCoverTooTallToFireOver` says yes → flips `HasCoverPosition=false` immediately and enters open-engage.
   - Else → computes `ResolvedPeekSide` via `ResolvePeekSide(CoverLoc, TargetLoc, CompanionLoc)` and tells the AnimInstance `EnterCoverPose(side)`. This is the moment the anim layer learns it is "in cover."

4. **`BTTask_CompanionCombat::TickTask`** — runs the three-branch state machine: CoverIdle / StandUpFireBurst / OpenEngage. Branch is implicit, computed each tick from `(bHasCover, bIsFiringBurst)`. Inside CoverIdle there is a **second** cover-validity check on a 1 s timer (`CoverValidityCheckTimer` / `MinCoverDwellBeforeReEval`) that runs two more traces: a crouch trace ("is target's eye visible to cover at crouch height") and a stand trace (same as `IsCoverTooTallToFireOver`). Either failure sets `HasCoverPosition=false` and `FinishLatentTask(Succeeded)` so the root selector re-enters MoveToCover.

5. **`OnTaskFinished` (combat)** — on every exit (Succeeded/Failed/Aborted) calls `Anim->ExitCoverPose()`. This is the only deterministic place the cover pose gets cleared.

6. **`UCompanionAnimInstance::EnterCoverPose / ExitCoverPose`** — plays/stops the configured montage and toggles its own private `bInCover` bool. There is **also** an unsolicited `Anim->ExitCoverPose()` call inside the OpenEngage branch of the combat tick guarded by `Anim->IsInCover() && !bHasCover` — i.e. the anim layer is asked "are you in cover?" by the BT to decide whether to call exit. This is the AnimInstance acting as a second source of truth.

### Owners of cover-related state

| State | Owner | Writers | Readers |
|---|---|---|---|
| `BB:HasCoverPosition` | Blackboard | MoveToCover (callback + tick-fail), CompanionCombat (Exec, tick-validity, both invalidation paths), UpdateCompanionState (target changed / target gone / LoS lost) | MoveToCover, CompanionCombat, EnvQueryContext_CurrentCover |
| `BB:CoverLocation` | Blackboard | MoveToCover (only) | MoveToCover, CompanionCombat, EnvQueryContext_CurrentCover |
| `bInCover` | AnimInstance (private) | AnimInstance::EnterCoverPose/ExitCoverPose | CompanionCombat tick (queries it to decide whether to call exit) |
| `ResolvedPeekSide` | CompanionCombat (per-instance member) | CompanionCombat (3 sites) | CompanionCombat → AnimInstance via EnterCoverPose arg |
| `ActivePeekSide` | AnimInstance (private) | AnimInstance::EnterCoverPose | AnimInstance only |
| `CoverValidityCheckTimer`, `TimeAtCurrentCover`, `LastPeekResolveCoverLoc`, etc. | CompanionCombat | CompanionCombat | CompanionCombat |

Five owners, three of which write to the same canonical cover state (`HasCoverPosition`). That is the structural problem.

---

## 2. Where the model is split or duplicated

**"Is this cover valid?" is computed in five places with three different definitions.**

1. **EQS asset (not in C++) — `CoverQuery`** — picks the cell. Whatever generator + tests the designer wired up, presumably based on `Trace`-style cover tests against `CombatTarget`. The C++ side doesn't know what rules picked the slot.
2. **`BTTask_CompanionCombat::ExecuteTask` → `IsCoverTooTallToFireOver(StandFireHeightOffset, TargetEyeHeightOffset)`** — single trace from cover+StandOffset to target+EyeOffset. Failure ⇒ cover rejected on entry.
3. **`BTTask_CompanionCombat::TickTask` periodic re-check** — **two** traces: a crouch trace from cover+`CrouchHideHeightOffset` to target+EyeOffset (must be blocked for cover to be valid) and a stand trace identical to #2 (must be clear). Different semantics, more strict than #2.
4. **`BTTask_CompanionCombat::TickTask` "LoS-from-cover gate before standing up"** — `HasLineOfSight(CoverLoc, Target, ignore=Companion+weapon)`. This is yet another trace; it ignores neither the target's allies nor any tall cover. The companion will sit in cover and never peek if this fails.
5. **`BTService_UpdateCompanionState`** — invalidates cover when target identity changes, when target dies, when the service's own LoS trace from `Companion->GetActorLocation()` (not cover!) to target is blocked. So a brief LoS blip on the *companion's* current position, even while standing at cover, can blow `HasCoverPosition=false` from underneath the combat task.

### What disagrees

- **Heights are inconsistent.** `IsCoverTooTallToFireOver` uses `StandFireHeightOffset` (60–250 cm clamp, default unknown — header was truncated, but tooltip says it's a Z-offset above the cover slot Z, not above the floor). The periodic re-check reuses the same offset, plus a `CrouchHideHeightOffset` that is *not* the inverse of stand. The EQS cover test (whatever the designer wired) has no shared constants with C++. So the EQS picks slots that pass *its* trace, and `IsCoverTooTallToFireOver` rejects them on a different rule.
- **The "cover" cell is a single FVector.** There is no wall normal, no orientation, no peek side stored. `ResolvePeekSide` infers a side every time from `(CoverLoc - CompanionLoc)` cross product. As soon as the companion moves a few centimetres past the slot during stand-up-fire, the inferred "wall right" vector flips and the peek side can swap mid-engagement. The `PeekResolveDistThresholdSq` cache reduces churn but does not fix the fundamental problem: peek side is not a property of the cover, it is a stale function of where the companion happened to be standing when it was computed.
- **`CrouchHideHeightOffset` vs target eye.** The periodic re-check asks "does the target's eye see the cover at crouch height?" That assumes the *target's eye* is the right test point. If the target ducks, crouches, or is a different actor class with a different head bone height, this becomes randomly wrong.
- **`UpdateCompanionState` writes `HasCoverPosition=false` on companion-side LoS loss, but never on cover-side LoS loss.** It's invalidating cover based on a property unrelated to whether the cover is still cover.

### Why the "loop" the user describes

The combat task's periodic re-check (every 1 s after a 2 s dwell) re-evaluates the same cover the EQS just picked, using stricter rules. When the cover passes EQS but fails the re-check, the task succeeds, the root selector re-runs MoveToCover, EQS picks the **same** slot again (it was the best match), MoveToCover succeeds, CompanionCombat enters, dwells 2 s, re-validates, fails, repeat. This is the parameter-tweaking loop the user is stuck in.

---

## 3. Load-bearing vs vestigial

**Load-bearing (don't delete):**
- `BTService_UpdateCompanionState`'s target acquisition / target liveness / target LoS gating. This is the perception spine and works.
- `CompanionAIController` perception + warp/teleport fallback. Independent system, fine.
- `BTTask_MoveToCover` as a concept (find a slot, walk to it). The skeleton is right; the implementation has the abort gap.
- `CompanionCombat`'s OpenEngage branch. Without the cover paths, this is a reasonable burst-fire engagement.
- `CompanionAnimInstance` montage playback. Anim assets and the play/stop API are fine.

**Pulling weight but fragile:**
- `IsCoverTooTallToFireOver` — correct *idea*, wrong *layer*. This belongs in EQS as a test, not as a post-hoc rejection in the consumer task.
- `ResolvePeekSide` — works while stationary, broken while moving.
- The CoverIdle → StandUpFire transition. The "stand up to fire" model itself is sensible; the implementation works as long as cover doesn't get invalidated during the burst.

**Vestigial / actively harmful:**
- The periodic in-combat cover re-check (`CoverValidityCheckTimer`). It does the *same job* `IsCoverTooTallToFireOver` did on entry, plus an unsymmetric crouch trace. Removing it would not regress the stated goal (the entry check + service-driven target invalidation are sufficient) and would kill the thrash loop.
- The AnimInstance `bInCover` flag as a *gate* read by the BT (`Anim->IsInCover()` in OpenEngage). The BT should know whether it is in cover from its own state, not by asking the anim layer. Right now this exists because cover pose entry and exit happen at non-symmetric points (Exec vs OnTaskFinished + emergency exit on cover-flipped-mid-burst).
- The "Cover stays true across cycles" optimisation in `MoveToCover::ExecuteTask` (early branch that skips EQS if `HasCoverPosition` is already set). It saves one query but the abort gap means a stale cover position from a prior aborted task can survive into a new engagement.
- `EnvQueryContext_CurrentCover` exists but I see only one consumer (it provides a vector to whichever EQS asset wants it — for "cover near my current cover" queries, presumably). If no EQS query uses this context, it is dead code; if one does, it perpetuates the FVector-only model.
- `LastPeekResolveCoverLoc` / `LastPeekResolveTargetLoc` thresholding is a band-aid for `ResolvePeekSide` instability. With structured cover slots, both go away.

---

## 4. Design-level smells

In rough order of severity:

**a. Cover is an FVector, not a slot.** No facing, no peek-side, no associated actor or wall normal. Every consumer has to *infer* the missing fields, and each does so slightly differently. This is the root cause of about half the visible bugs:
- peek side flipping when the companion drifts off the spot
- "crouches in the open" — the task enters cover pose based on `HasCoverPosition`, not on "I am actually adjacent to and facing a piece of cover"
- the re-check and the EQS disagreeing about height (each is reconstructing a notion of "the wall" from a point)

**b. Cover-pose entry happens in `CompanionCombat::ExecuteTask`, not at MoveTo arrival.** ExecuteTask runs the instant the parent sequence advances to the combat node, which is when MoveToCover returned Succeeded. That happens at "within `AcceptableRadius` (100 cm) of CoverLoc," which is a soft arrival. In practice the companion can be 99 cm away, with the EQS-picked point still in front of it, and EnterCoverPose triggers the montage anyway. The "cover pose triggered before arrival" symptom the user mentions is exactly this: arrival is the path-follower returning non-Moving, not "I am at the slot."

**c. Three places run cover-validity traces; none share constants.** EQS asset, `IsCoverTooTallToFireOver`, periodic re-check. They disagree about (target eye height, stand height, what counts as "blocked"). With designer values changing in two locations (EQS test on the asset + UPROPERTY on the task), drift is guaranteed.

**d. The periodic re-check is racing the EQS.** EQS picks slot S because S maximises its score. 2 seconds later, the re-check (with stricter rules) rejects S. Selector re-runs, EQS scores all slots again — S still wins — repeat. The loop only breaks if the target moves enough that another slot scores higher, or if `MinCoverDwellBeforeReEval` is long enough to be useless (defeats the purpose of a re-check).

**e. AnimInstance has its own cover state machine that the BT queries.** `Anim->IsInCover()` being read inside the combat tick is upside-down. The animation layer should be a *consumer* of state owned by the BT, not a peer source of truth. The current design exists because the cover pose can persist across task boundaries (e.g. when the engagement ends Succeeded but the task didn't see fit to call ExitCoverPose, or when the cover flips mid-burst) — but the fix is to centralise the state, not to ask Anim what it thinks.

**f. `BTTask_MoveToCover` has no `AbortTask`.** When the BT aborts the task because perception lost the target mid-walk:
- `MoveToLocation` keeps running. The companion walks all the way to a slot for an enemy that no longer matters, briefly enters cover pose when CompanionCombat is then re-entered for a *different* target, etc.
- `OnQueryFinished` can fire on the dead task. The task instance is still alive (UObject GC), so the callback runs, writes to BB. If a new task instance has already started a query, two callbacks race; whoever wins last writes the BB. This corrupts state in a way that is hard to reproduce because it depends on EQS latency.

**g. `bCreateNodeInstance = true` plus member-variable scratch.** Both BT tasks use `bCreateNodeInstance = true` and store state in members. That is correct UE5 idiom, but it means `CachedOwnerComp` (raw `TObjectPtr`) holds a reference that survives between Execute and the EQS callback. If `OwnerComp` is destroyed (level unload, possession change), the callback dereferences it.

**h. Cadence fights itself.**
- BT service: 250 ms target invalidation
- Periodic cover re-check: 1 s (after 2 s dwell)
- EQS run: on-demand, latency 1–2 frames
- MoveTo `AcceptableRadius`: 100 cm
- Peek cooldown: 0.6–1.4 s random
- `MinCoverIdleDwell`: 0.4 s
These overlap in awkward ways: a service tick can invalidate cover mid-peek-cooldown, a re-check can abort cover 2 s after entry just as the first peek would have fired, an EQS callback can land after MoveToCover has been aborted.

**i. There's no "cover handle." Re-entering MoveToCover after invalidation doesn't *exclude* the slot that just failed.** The same EQS run picks the same slot, because nothing remembers "this one is bad."

---

## 5. Three redesign options

Constraint: minimum change to get a working stand-up-fire cover companion. All three are doable; pick based on how much designer-authoring you want.

### Option A — Stay procedural EQS, unify the model

**Idea:** keep EQS as the slot picker. Replace the FVector blackboard pair with a single struct, move all "is this cover valid" logic into one function shared by EQS test and runtime, kill the periodic re-check.

**Stays:**
- EQS query asset, all perception/service plumbing, MoveToCover skeleton, CompanionCombat state machine, AnimInstance montage interface.

**Gets deleted:**
- `IsCoverTooTallToFireOver` as a standalone helper.
- Periodic in-combat cover-validity re-check (entire `CoverValidityCheckTimer` block).
- `BTService_UpdateCompanionState`'s `HasCoverPosition=false` on companion-side LoS loss.
- `LastPeekResolveCoverLoc/Target` caching.
- `AnimInstance::IsInCover()` as a BT-readable gate.

**Gets added:**
- `FCompanionCoverSlot { FVector Location; FVector Facing; EPeekSide PeekSide; TWeakObjectPtr<AActor> AssociatedActor; double ValidUntilSeconds; }` stored in a single BB key (object or vector-array-of-doubles via a tiny UObject wrapper, or four BB keys).
- A single `bool IsCoverValid(const FCompanionCoverSlot&, AActor* Target, const FCompanionCoverHeights&)` function called by both the EQS test (as a CustomCoverTest, native) and the entry check in `CompanionCombat::ExecuteTask`. No periodic re-check; entry-only.
- `BTTask_MoveToCover::AbortTask` that stops movement, cancels the EQS request, and clears `HasCoverPosition`.
- EQS computes the facing and the peek side at pick time, writes the slot struct.

**Effort:** ~2 days. One implementer. Mostly mechanical.

**Risk:** EQS test authoring complexity goes up. If the designer can't easily express "valid cover" as an EQS test, you end up writing a native CustomCoverTest, which is the right thing anyway.

---

### Option B — Authored cover slots (AActor_CoverSlot)

**Idea:** designer places cover-slot actors in the level by hand. Each actor's transform is the slot position + facing, with a per-actor `PreferredPeekSide` and optional `LeftPeekAllowed` / `RightPeekAllowed` flags. Cover discovery is "find the nearest slot actor where the LoS rules hold." No procedural trace heuristics, no EQS-vs-runtime drift.

**Stays:**
- CompanionCombat state machine, AnimInstance montage interface, service-driven target update.

**Gets deleted:**
- The cover-finding EQS asset (or it becomes trivial: "pick from `TActorRange<ACoverSlot>` filtered by LoS").
- `IsCoverTooTallToFireOver` (the slot actor declares its own height; if you walk to a designer-placed slot, you trust it).
- Periodic re-check (replaced by a single trace: target → slot, blocked = still valid; clear = leave and refind).
- `ResolvePeekSide` and all peek-side resolution. Side is on the actor.

**Gets added:**
- `AAICoverSlot : public AActor` with `USceneComponent` root, `UArrowComponent` for editor visibility, `EPeekSide PreferredPeekSide`, `float Width`, `float Height`, `bool bRequiresCrouch`. Probably 80 lines of code.
- `UCoverSlotComponent` on the slot or a global `UCoverSubsystem : public UWorldSubsystem` that owns a list and answers `FindNearestValidSlot(Querier, Target)` in O(N) (or O(log N) with a 2D grid).
- A simple `BTTask_FindCoverSlot` that calls the subsystem and writes the slot's ref (UObject) + transform to BB.
- Editor-time UX: artist drops actors in cover positions. Cheap with snapping.

**Effort:** ~3 days. One implementer + half-day of designer level-pass to place initial slots. Higher because of editor UX.

**Risk:** authored cover doesn't scale to procedural levels, and any level the designer hasn't passed through has zero cover. Acceptable for a hand-built game; not for proc-gen.

---

### Option C — Smart Objects

**Idea:** use UE5's `SmartObject` plugin. Define a "TakeCoverDefinition" with slot definitions per cover variant. AI claims a slot, gets the transform + tags, runs a state tree task to occupy. This is what the engine team intends people to use.

**Stays:**
- CompanionCombat *could* stay, but it would naturally migrate to a StateTreeTask. AnimInstance interface stays.

**Gets deleted:**
- All of `BTTask_MoveToCover`, `BTService_UpdateCompanionState`'s cover invalidation, the EQS cover asset, `IsCoverTooTallToFireOver`, the periodic re-check, `ResolvePeekSide`. Probably 80% of the C++ cover code.

**Gets added:**
- `USmartObjectDefinition` data asset(s) for cover variants (full, half-left, half-right).
- `ASmartObjectActor` or `USmartObjectComponent` placed in the level.
- A `UGameplayBehavior_TakeCover` that drives the cover sequence (claim slot → move → enter pose → fire → release).
- A StateTree (or BT bridge) that calls into the SmartObject subsystem.

**Effort:** ~1 week, plus learning curve if not already familiar. The plugin is stable but not trivial to wire end-to-end the first time.

**Risk:** the heaviest lift. Pays back if you intend to add other slot-based behaviours (ambient interaction, vault points, sniper perches), since the same system handles them. Pure cost if cover is the only slot system you'll ever need.

---

### Honest verdict (informational only — not picking for you)

The bug pattern the user is describing — "fix one parameter, break another, never converge" — is what you get when **the system is solving the same question in three places with three different definitions.** A is the smallest cut that fixes that without changing how cover is discovered. B is the smallest cut that fixes it *and* removes the discovery heuristic entirely. C is the largest cut and the most future-proof.

If the goal is "stand-up-fire cover companion that works in PIE this week," A is the shortest path. If the project will live with hand-built combat arenas anyway, B is cheaper to debug long-term because there is literally no heuristic to disagree with. C is only worth it if cover is one of several slot systems on the roadmap.
