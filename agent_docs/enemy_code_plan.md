# EXTRACTION — Enemy AI Code Plan

**Status:** Living implementation plan. Created 2026-06-09 on `AI-Companion-Prototype`.
**Source spec:** `agent_docs/enemy_design.md` (the gameplay design doc — read it first, it owns *why*; this doc owns *how in code*).
**Companion docs:** `agent_docs/companion_testing.md` (QA style to mirror), `agent_docs/enemy_test_levels.md` (per-phase in-engine test setups: the EnemyGym zones + the ExtractionSlice three-act level).

---

## Phase Status

| Phase | Scope | C++ | Editor wiring | Playtest |
|---|---|---|---|---|
| 1 | Skeleton — Grunt (character, controller, archetype DA, perception, base BT, teams, weapon decouple) | ✅ 2026-06-09 | ⏳ handed off | ❌ |
| 2 | Awareness ladder (suspicion, noise, global alert, bodies, takedown, barks v1) | ✅ 2026-06-09 | ⏳ handed off | ❌ |
| 3 | Roster (7 archetypes, bolt-on components, grenade, subtrees) | ✅ 2026-06-10 | ✅ 2026-06-10 (meshes manual) | ❌ |
| 4 | Morale & suppression (+ hit reacts, ragdoll) | ✅ 2026-06-10 | ✅ 2026-06-10 (montages manual) | ❌ |
| 5 | Squad baseline (coordinator, shared sightings, flanker, focus-fire, threat targeting) | ✅ 2026-06-10 | ✅ 2026-06-10 (SquadId placement = user) | ❌ |
| 6 | Director (tension sawtooth, spawn zones, escalation, cap) | ✅ 2026-06-10 | ✅ 2026-06-10 (zones = user) | ❌ |
| 7 | Bounding overwatch (officer-gated suppress-and-advance) | ✅ 2026-06-10 | ✅ 2026-06-10 | ❌ |

### How to resume (future chats)

1. Read `enemy_design.md` (design intent) and this doc top to bottom.
2. Check the status table above + `git log --oneline -15` for where work actually stopped.
3. Phases must land in order (each builds on the last). Editor wiring + playtest of phase N should be confirmed before phase N+1 C++ lands — ask the user if the table is ambiguous.
4. The phase contracts below are *contracts*, not line-level specs: the implementing session does its own fine-grained planning against the codebase as it exists then (re-verify file paths and APIs — they drift).
5. Per project workflow: implementer agents write the code, reviews are mandatory before "done", build must pass (`Result: Succeeded`) before reporting back, in-editor work is handed to the MCP agent as a checklist (C++ never references `/Game/` assets).
6. After landing a phase: update the status table, tick the contract's deliverables, append QA scenarios to the handoff, note deviations in the contract section.

---

## Architecture

### Class map (end state, all phases)

```
ACharacter
└── AEnemyCharacter                    one class for all 7 archetypes (Public/Enemy/)
      ├── UHealthComponent             existing, reused
      ├── USuppressionComponent        Phase 4 (shared with companion; Public/Components/)
      ├── UEnemyMoraleComponent        Phase 4
      └── conditional bolt-ons         Phase 3 (Public/Enemy/Components/): armour / shield /
                                       grenadier / officer aura / sniper telegraph

AAIController
└── AEnemyAIController                 perception (sight+hearing), team 1, BB+BT, LogEnemyAI
      └── UEnemyAwarenessComponent     awareness ladder: stimuli → suspicion → state → BB

UDataAsset
├── UEnemyArchetypeData                ALL per-archetype behaviour/tuning (grows every phase)
├── UDirectorConfigData                Phase 6
└── UBarkSetData                       Phase 2

UWorldSubsystem
├── UCoverRegistrySubsystem            existing, reused untouched (shared cover pool)
├── UEnemyDirectorSubsystem            Phase 2 (global alert only) → Phase 6 (full director)
└── UEnemySquadSubsystem               Phase 5 (owns UEnemySquad UObjects)

AActor
├── APatrolRoute                       Phase 1 (designer-placed patrol points)
├── AEnemySpawnZone                    Phase 6
└── AEnemyGrenadeProjectile            Phase 3

Interfaces
├── IAIShooterInterface                Phase 1 (weapon ↔ AI decoupling; companion + enemy)
└── IGenericTeamAgentInterface         Phase 1 (engine; controllers + pawns, teams 0/1)
```

### Data flow (steady state)

```
UAIPerceptionComponent (sight/hearing stimuli)
        ↓
UEnemyAwarenessComponent  (suspicion per target, state ladder, last-known)
        ↓ writes                                ↑ NotifyDamaged (TakeDamage)
Blackboard (AwarenessState, CombatTarget, LastKnownLocation, …)
        ↓ decorators (observer aborts)
BT_EnemyBase  =  Combat | Search | Suspicious | Patrol   (selector)
        ↓ Combat branch runs archetype subtree (SetDynamicSubtree at possess)
BT tasks → AEnemyCharacter API (SetAimTarget / StartFiring / crouch / move)
        ↓
AWeaponBase::PerformHitscan  (aim target + spread via IAIShooterInterface)
        ↓
Victim TakeDamage → EHitRegion multiplier → UHealthComponent
        ↘ Phase 4: near-miss → USuppressionComponent on nearby AI
        ↘ Phase 2+: fire noise → ReportNoiseEvent → other enemies' hearing
```

Squads (Phase 5) sit beside this: `UEnemySquad` relays sightings/orders into members' awareness components and BBs; it never puppets pawns directly. The director (Phase 6) sits above squads and only spawns/paces; the hard rule "local survival overrides squad orders" is enforced by BT priority order, not by the coordinator.

### Locked decisions (rationale in one line each)

1. **One `AEnemyCharacter` + `UEnemyArchetypeData` per archetype** — design pillar "variety is data, not code"; no enemy subclasses.
2. **Archetype combat subtrees injected at possess** via `RunBehaviorDynamic` slot tagged `TAG_BT_EnemyCombat` + `SetDynamicSubtree(Tag, DA->CombatSubtree)`.
3. **Teams done properly**: `IGenericTeamAgentInterface` on controllers AND pawns — player/companion = `FGenericTeamId(0)`, enemies = `FGenericTeamId(1)`. Perception affiliation then sorts friend/foe natively. Gameplay tags (`TAG_Character_Enemy/Companion/Player`) remain the *gameplay* identity layer (companion targeting, FF) — both coexist deliberately.
4. **`IAIShooterInterface`** (`GetAIAimTarget()`, `GetAIAimSpreadDegrees()`) replaces `WeaponBase.cpp`'s hard `Cast<ACompanionCharacter>` / `Cast<AEnemyBase>`; friendly-fire ignore lists become team-ID comparison.
5. **Awareness ladder is a C++ component** (`UEnemyAwarenessComponent` on the controller), not BT logic — fixed flow, state machine; the BT only *reads* `AwarenessState`. Phase 1 ships simplified transitions; Phase 2 deepens internals without re-architecture.
6. **Subsystems mirror `UCoverRegistrySubsystem`** (world subsystem + registration pattern) for director, squads, barks.
7. **Enemy accuracy model lives on `AEnemyCharacter`** (reaction delay → first-burst spread → settle → widen on target movement/suppression). These knobs are the thesis difficulty levers. Companion's model untouched.
8. **Shared cover pool**: enemies call the existing `UCoverRegistrySubsystem::FindBestCoverFor(...)` / `AAICoverSlot::TryClaim/Release` with the player/companion as *their* threat. No enemy-specific cover markup, no registry changes.
9. **Suppression is a shared component** (enemy + companion, never the player); **morale is enemy-only**.
10. **No enemy DBNO** — enemies die outright (design §8). DBNO stays player/companion.
11. **Authority-clean, SP-targeted**: all AI decisions authority-side; replicate only what clients would render. No new replication burden unless trivially cheap.
12. **No object pooling yet** — hitscan (no projectiles), director paces spawns under the pooling threshold (cap ~20 alive). Revisit if profiling demands.
13. **`AEnemyBase` placeholder survives** (implementing `IAIShooterInterface`) until the test level migrates to `AEnemyCharacter`; deleted in Phase 3.

### Reuse surface (verified APIs — re-check before use, they drift)

| Existing system | Where | What the enemy uses |
|---|---|---|
| `UCoverRegistrySubsystem` | `Public/AI/Cover/` | `FindBestCoverFor(QuerierLoc, Target, MaxRadius, OutScore, QuerierPawn, PostVacateCooldown)`; `GetSlotsInRadius`; static `ScoreSlotFor` |
| `AAICoverSlot` | `Public/AI/Cover/` | `TryClaim(AActor*)` / `Release(AActor*)` / `IsClaimed()`; height/peek-side config; line geometry (`GetLeftEdge/GetRightEdge/GetLocationAtAlpha/…`) for peek positions |
| `UHealthComponent` | `Public/Components/` | `TakeDamage(float)`, `IsDead()`, `OnDeath`/`OnHealthChanged` delegates; + new `InitializeHealth(MaxHealth, MaxShield)` (added Phase 1) |
| `AWeaponBase` | `Public/Weapon/` | Spawn from class, `InitializeAmmo()`, `StartFiring()/StopFiring()`, auto-reload flag; hitscan damage path with `FPointDamageEvent` |
| `EHitRegion` + `UExtractionDamageType` | `Public/Core/`, `Public/Data/` | `BoneToHitRegionMap` pattern from `AExtractionCharacter::TakeDamage` → head 2.0×, limbs 0.75× |
| AI Perception pattern | `CompanionAIController.cpp` | Sight + hearing config shape, `GetCurrentlyPerceivedActors`, max-age usage |
| `UEnvQueryContext_CombatTarget` | `Public/AI/EQS/` | Reusable in enemy EQS queries — enemy BB uses the same `CombatTarget` key name |
| `ATraversalNavLink` | `Public/AI/Navigation/` | Currently companion-gated (non-companions resume path immediately = cross without animation). Acceptable for enemies; enemy traversal animation is out of scope unless design demands it later. |

### Conventions

- **Folders:** enemy domain in `Public|Private/Enemy/` (flat); BT nodes in existing `AI/Tasks`, `AI/BTS`, `AI/EQS`. New subfolders (`Enemy/Components` P3, `Enemy/Squad` P5, `Enemy/Director` P6) must be registered in **both** include arrays in `Extraction.Build.cs` in the phase that introduces them. Phase 1–2: zero Build.cs changes.
- **Log category:** `LogEnemyAI` (declared `EnemyAIController.h`, defined in its `.cpp`). `LogEnemy` (on `EnemyBase.h`) dies with the placeholder.
- **Tags & teams:**

| Actor | Gameplay tag | Team |
|---|---|---|
| Player (`AExtractionCharacter` / `AExtractionPlayer`) | `TAG_Character_Player` (new) | 0 |
| Companion | `TAG_Character_Companion` | 0 |
| Enemy (`AEnemyCharacter`) | `TAG_Character_Enemy` | 1 |

- **Tuning:** every gameplay number an archetype could vary lives in `UEnemyArchetypeData`. System-level numbers (alert timings, director curves) live in their own DAs. Hardcoded defaults only as `UPROPERTY` initializers.
- **All state-bearing BT nodes use `NodeMemory`** (`GetInstanceMemorySize()`), never node member variables.
- **Timers over Tick** everywhere; stagger with random initial offsets; BT services at 0.25–0.5s + `RandomDeviation`.
- **Editor split:** C++ ships classes/nodes only. BT/BB assets, BP children, DA instances, meshes/ABPs/montages, Niagara, level placement → in-editor MCP agent, via a per-phase handoff checklist.
- **Perf budget (design §9 + skill):** ~20 concurrent AI, ~4ms AI frame budget. Staggered awareness timers, EQS every 3–5s not per tick, no per-frame `GetCurrentlyPerceivedActors`.

---

## Phase 1 — Skeleton (Grunt)

**Goal:** *Grunt patrols, spots you, takes cover, fires, dies.* Plus all plumbing the other six phases stand on (teams, weapon decoupling, archetype DA, base BT shape).

### New files (`Extraction/Source/Extraction/`, Public/Private pairs)

| File | Contents |
|---|---|
| `Enemy/EnemyTypes.h` | `EEnemyArchetype` (Grunt/Rusher/Heavy/Sniper/Officer/Grenadier/Shield), `EEnemyAwarenessState` (Unaware/Suspicious/Searching/Combat). Header-only. |
| `Enemy/EnemyArchetypeData.h/.cpp` | `UEnemyArchetypeData : UDataAsset`. Phase-1 fields — Identity: `Archetype`, `DisplayName`. Stats: `MaxHealth=100`, `MaxShield=0`, `PatrolSpeed=200`, `CombatSpeed=400`. Perception: `SightRadius=2500`, `LoseSightRadius=3000`, `PeripheralVisionDeg=110` (design §4), `SightMaxAge=5`, `HearingRange=2000`, `HearingMaxAge=3`. Combat: `EngageRangeMin=600`, `EngageRangeMax=1800`, `BurstDurationMin/Max`, `BurstPauseMin/Max`, `ReactionDelay=0.5`, `SpreadStartDeg=7`, `SpreadSettledDeg=1.5`, `SpreadSettleTime=2`, `SpreadWidenMovingTarget=3` (added when target speed > `MovingTargetSpeedThreshold=300`), `CoverSearchRadius=1200`, `SearchDuration=8`, `LostContactGrace=4`. Weapon: `TSubclassOf<AWeaponBase> WeaponClass`. BT: `TObjectPtr<UBehaviorTree> CombatSubtree`. Lifecycle: `DestroyDelay=3`. |
| `Enemy/EnemyCharacter.h/.cpp` | `AEnemyCharacter : ACharacter, IGameplayTagAssetInterface, IAIShooterInterface, IGenericTeamAgentInterface`. Skeletal mesh (BP assigns). `HealthComponent`; `BoneToHitRegionMap` (mannequin defaults, `EditDefaultsOnly`) + `TakeDamage` override → region multiplier → `HealthComponent->TakeDamage` → records `LastDamageInstigator`/time → `AwarenessComponent->NotifyDamaged()` via controller. `ArchetypeData` (`EditAnywhere`), `PatrolRoute` (`EditInstanceOnly`). Weapon spawned in `BeginPlay` (authority) from DA, attached to mesh socket if present, `bAutoReloadOnEmpty` left true for Phase 1. `ApplyArchetypeData()` — speeds, `InitializeHealth`. Aim API: `SetAimTarget(AActor*)` (resets settle + arms reaction delay on *new* acquisition), `GetAIAimTarget()`, `GetAIAimSpreadDegrees()` (reaction window → `SpreadStartDeg`; lerp to `SpreadSettledDeg` over `SpreadSettleTime`; + widen if target speed > threshold). `HandleDeath` — stop BT logic (`Controller->BrainComponent->StopLogic`), clear timers, capsule off, weapon `StopFiring`, destroy after `DestroyDelay` (Phase 2 replaces with corpse persistence). `AIControllerClass = AEnemyAIController::StaticClass()`, `AutoPossessAI = PlacedInWorldOrSpawned`. Tag `TAG_Character_Enemy`. Team: returns controller's team if possessed, else 1. |
| `Enemy/EnemyAIController.h/.cpp` | `LogEnemyAI`. Perception: sight + hearing configs (constructor defaults; overridden from DA at possess then `RequestStimuliListenerUpdate`). `IGenericTeamAgentInterface` → `FGenericTeamId(1)` (set in constructor via `SetGenericTeamId`). `OnPossess`: `UseBlackboard` + `RunBehaviorTree` (BT asset = `EditDefaultsOnly` property, BP-assigned), `ApplyArchetypeData` on pawn, `SetDynamicSubtree(TAG_BT_EnemyCombat, DA->CombatSubtree)`, write `PatrolRoute` BB key. Owns `UEnemyAwarenessComponent` (default subobject) and binds it to perception. `OnUnPossess`/`EndPlay`: release any claimed cover slot (mirror companion belt-and-braces), clear timers. BB key names as `static const FName` (companion convention): `CombatTarget`, `LastKnownLocation`, `InvestigateLocation`, `AwarenessState`, `HasLineOfSight`, `TargetInRange`, `CoverSlot`, `HasCover`, `PatrolRoute`. |
| `Enemy/EnemyAwarenessComponent.h/.cpp` | Phase-1 ladder on a staggered repeating timer (0.15s + random 0–0.15 initial offset, no Tick). Inputs: `OnTargetPerceptionUpdated` (sight only this phase; hearing wired Phase 2), `NotifyDamaged(AController*)`. Hostility check: team attitude via `FGenericTeamId`. Transitions: hostile sighted → Combat (instant, Phase 2 replaces with suspicion fill); damage taken → Combat toward instigator's pawn; sight lost → keep Combat, freeze `LastKnownLocation`, after `LostContactGrace` → Searching (`InvestigateLocation = LastKnown`); Searching for `SearchDuration` with no re-acquire → Unaware. Writes all awareness BB keys; broadcasts `OnAwarenessStateChanged(Old, New)`. Per-target bookkeeping in `TMap<TWeakObjectPtr<AActor>, …>`. Timer cleared in `EndPlay`. |
| `Enemy/PatrolRoute.h/.cpp` | `APatrolRoute : AActor`. `TArray<FVector> Points` (`EditAnywhere, meta=(MakeEditWidget)`, actor-relative), `bLoop=true` (false = ping-pong), `WaitAtPointSeconds=2`. `GetWorldPoint(i)`, `NumPoints()`. Billboard root for placement. |
| `Weapon/AIShooterInterface.h/.cpp` | `UINTERFACE` `UAIShooterInterface` / `IAIShooterInterface`: `virtual AActor* GetAIAimTarget() const = 0;` `virtual float GetAIAimSpreadDegrees() const = 0;` |
| `AI/BTS/BTService_EnemyCombat.h/.cpp` | Interval 0.25s + 0.05 deviation. Validates `CombatTarget` (alive via `UHealthComponent::IsDead`); LOS trace pawn-eyes → target (ECC_Visibility); writes `HasLineOfSight`, `TargetInRange` (inside `EngageRangeMax`). Does NOT pick targets (awareness owns that). |
| `AI/Tasks/BTTask_EnemyPatrol.h/.cpp` | Latent. Reads `PatrolRoute` BB object; walks points (loop/ping-pong) at `PatrolSpeed`, waits `WaitAtPointSeconds` at each. No route → succeed-and-idle (guard post; selector re-enters). NodeMemory: current index, direction, wait timer. |
| `AI/Tasks/BTTask_EnemyMoveToCover.h/.cpp` | `FindBestCoverFor(PawnLoc, CombatTarget, DA->CoverSearchRadius, nullptr, Pawn)` → `TryClaim` → `MoveToLocation` → on arrival crouch if slot height = Crouch → write `CoverSlot`/`HasCover`. Fail (no slot/claim race) → clear keys, return Failed (subtree falls to open-ground branch). Release on `AbortTask` and on failed move. NodeMemory for in-flight state. |
| `AI/Tasks/BTTask_EnemyCombatFire.h/.cpp` | Latent peek-fire loop, NodeMemory state machine: Acquire (`SetAimTarget`, wait `ReactionDelay` if newly acquired) → Expose (un-crouch / step to peek point from slot geometry) → Fire (`StartFiring`, hold `BurstDuration`) → Recover (`StopFiring`, return/crouch) → Pause (`BurstPause`) → repeat. With no cover (`HasCover` false): stand burst-pause in place. Aborts cleanly (stop fire, `SetAimTarget(nullptr)`) on target loss/abort. Target far outside `EngageRangeMax` + no LOS → returns Failed so the selector can re-seek cover closer. Keep it lean (~300 lines); richer peek variety stays companion-only. |
| `AI/Tasks/BTTask_EnemySearchLastKnown.h/.cpp` | MoveTo `InvestigateLocation` → yaw-sweep look-around (3 segments over ~3s) → Succeeded. Awareness component owns the Searching→Unaware timeout; task just performs the sweep. |

### Edits to existing files

| File | Change |
|---|---|
| `Core/ExtractionTypes.h/.cpp` | Add native tags `TAG_Character_Player`, `TAG_BT_EnemyCombat`. |
| `Weapon/WeaponBase.cpp` | The 3 hardcoded sites: aim/spread via `Cast<IAIShooterInterface>(OwnerChar)`; FF ignore list = iterate world pawns, ignore same `FGenericTeamId` as owner (resolves via pawn interface); debug log via interface. Semantics preserved: enemy ignores enemies, companion ignores player+companions. |
| `Components/HealthComponent.h/.cpp` | Add `InitializeHealth(float NewMaxHealth, float NewMaxShield)` — authority-only, sets max + current, safe before/after BeginPlay. |
| `AI/CompanionAIController.h/.cpp` | `IGenericTeamAgentInterface`, team 0. Perception config otherwise untouched (`bDetectNeutrals` stays true — regression insurance). |
| `Game/ExtractionPlayerController.h/.cpp` | `IGenericTeamAgentInterface`, team 0. |
| `Companion/CompanionCharacter.h/.cpp` | Implement `IAIShooterInterface` (forward to existing `GetAimTarget`/`GetCurrentInaccuracy`); `IGenericTeamAgentInterface` team 0. |
| `Character/ExtractionCharacter.h/.cpp` + `Character/ExtractionPlayer.h/.cpp` | `IGenericTeamAgentInterface` team 0; add `TAG_Character_Player` to owned tags (add the tag container/interface in the minimal way each class allows). |
| `Enemy/EnemyBase.h/.cpp` | Implement `IAIShooterInterface` (forward existing getters) so the weapon decouple keeps the placeholder alive until level migration. |

No `Extraction.Build.cs` changes (all target folders already registered). `AIModule`, `GameplayTasks`, `NavigationSystem`, `GameplayTags` already in deps.

### Blackboard (BB_Enemy — editor asset)

| Key | Type | Written by |
|---|---|---|
| `CombatTarget` | Object | AwarenessComponent |
| `LastKnownLocation` | Vector | AwarenessComponent |
| `InvestigateLocation` | Vector | AwarenessComponent |
| `AwarenessState` | Enum (`EEnemyAwarenessState`) | AwarenessComponent |
| `HasLineOfSight` | Bool | BTService_EnemyCombat |
| `TargetInRange` | Bool | BTService_EnemyCombat |
| `CoverSlot` | Object | BTTask_EnemyMoveToCover |
| `HasCover` | Bool | BTTask_EnemyMoveToCover |
| `PatrolRoute` | Object | Controller at possess |

### BT_EnemyBase (editor asset)

```
Root Selector
├── Combat    [Decorator: AwarenessState == Combat, Observer Aborts: Both]
│   └── Service: BTService_EnemyCombat
│       └── Run Behavior Dynamic [Injection tag: TAG_BT_EnemyCombat]   ← archetype subtree
├── Search    [Decorator: AwarenessState == Searching, Observer Aborts: Both]
│   └── BTTask_EnemySearchLastKnown
└── Patrol    [default]
    └── BTTask_EnemyPatrol
```

`BT_EnemyCombat_Grunt` (Phase-1 subtree): Selector → [Sequence: BTTask_EnemyMoveToCover → BTTask_EnemyCombatFire] → [BTTask_EnemyCombatFire (open-ground fallback)].
(Suspicious state folds into Searching for Phase 1; it gets its own branch + bark in Phase 2.)

### In-editor handoff (MCP agent)

BB_Enemy (keys above) · BT_EnemyBase (structure above — the dynamic-injection node MUST carry `TAG_BT_EnemyCombat`) · BT_EnemyCombat_Grunt · AIC_Enemy BP child (BT assigned) · BP_Enemy_Grunt (skeletal mesh + ABP — mannequin fallback fine; ArchetypeData assigned; weapon socket if mesh has one) · DA_Enemy_Grunt instance · place 1 `APatrolRoute` (3+ points) + 2 grunts in the test level · navmesh covers patrol + cover area · keep/remove old `AEnemyBase` dummies as the user prefers (both work during transition).

### Phase 1 landed (2026-06-09) — implementation notes

C++ complete, reviewed (2 fix rounds), `Result: Succeeded`. Deviations from contract: none material. Notes for later phases: enemy weapon keeps `bAutoReloadOnEmpty=true` (BT-driven reload deferred); stand-slot peek is a minimal lateral shuffle toward the peekable corner (companion-grade peeking not replicated); target adoption is sticky (new hostile adopted only when current target is unseen) pending Phase 5 threat scoring; `AEnemyBase` gained `IAIShooterInterface` + team 1 and coexists until Phase 3 deletion. MoveToCover acceptance radii (60/80/200) are literals — promote to UPROPERTYs if tuning is ever needed.

### Phase 1 QA acceptance

1. Grunt walks its patrol route, loops, waits at points.
2. Enter its view cone → after ~ReactionDelay it turns, moves to a cover slot, crouch-peeks, fires bursts.
3. First burst visibly sloppier than later bursts (settle).
4. Headshot does 2× (log line shows multiplier), kills faster than torso.
5. Break LOS and stay hidden → it holds last-known, then moves there and sweeps, then returns to patrol.
6. Shoot it from behind → instant Combat toward you.
7. Companion auto-engages the grunt (tag-based targeting unchanged).
8. Grunt will shoot the companion if companion is the nearest hostile.
9. Two grunts: never share one cover slot; never damage each other (team FF).
10. Death: firing stops, BT stops, body despawns after DestroyDelay; companion drops it as a target.
11. Old `AEnemyBase` dummy still functions (no weapon-path regression) if still placed.

---

## Phase 2 — Awareness ladder

**Goal:** *Sneak a dormant patrol, get made, watch it search and give up.* Detection becomes gradual; sound exists; the world has a global alert seam; bodies matter.

**C++ deliverables**
- **Suspicion meter** inside `UEnemyAwarenessComponent`: per-target 0–100; sight fill rate = base × distance falloff × angle-off-centre × target-speed factor × stance factor (native `bIsCrouched`; prone via `IExtractionPlayerInterface::GetIsProne` — returns false on kit player, documented degradation; lighting deferred per design §13). Decay when stimulus absent. Thresholds → Suspicious / Searching / Combat (DA fields: `SuspicionFillRate`, `SuspiciousThreshold`, `SearchingThreshold`, `SuspicionDecayRate`, per-factor multipliers). Damage and point-blank confirmed sight still fast-track to Combat.
- **Noise pipeline** (`UAISense_Hearing::ReportNoiseEvent`): weapon fire (new `UWeaponDataAsset` fields: `NoiseLoudness`, `NoiseRange`, `bSuppressed` — suppressed = short range/low loudness per design §4 table); reload (`StartReload`); player footsteps via new `UFootstepNoiseComponent` (`Public/Components/`, distance-accumulator emitting by speed/stance, attached to both player classes); traversal noise hooks (`UTraversalComponent` start event → vault/mantle thud).
- **Hearing → suspicion**: bump + `InvestigateLocation`, never instant Combat (design rule).
- **`UEnemyDirectorSubsystem` v1** (`Public/Enemy/`): `EGlobalAlertLevel` Calm/Searching/Loud, transition API (`ReportConfirmedSighting`, `ReportBodyDiscovered`, `TripAlarm` BP-callable), `OnGlobalAlertChanged` delegate. Loud floors every enemy's awareness at Searching. (No spawning yet.)
- **Corpse persistence + body discovery**: death stops destroying the actor — collision off, death pose/montage hook, capped corpse pool (~10, oldest recycled). Awareness component checks perceived actors for dead allies (team 1 + `IsDead`) → Searching + `ReportBodyDiscovered` (each body triggers once per enemy, marked discovered set).
- **Silent takedown (minimal)**: enemy-side `CanBeTakenDown(Instigator)` (state ≤ Suspicious + behind-arc check) + `ExecuteTakedown` (silent instant kill, no noise event); player-side input action + proximity check. Animation polish deferred.
- **Barks v1**: `UBarkSubsystem` (world subsystem) + `UBarkSetData` (DA: `EBarkType` → text lines + optional sound) + `UEnemyBarkComponent` (per-enemy cooldowns; squad dedup arrives Phase 5) + subtitle feed widget (delegate-driven like existing HUD widgets). Detection lines: "Did you hear that?" / "Search the area!" / "Contact!".

**Edits:** `WeaponDataAsset` (noise fields), `WeaponBase` (emit noise), `TraversalComponent` (noise hook), both player characters (footstep component), `EnemyCharacter` (corpse path), `ExtractionPlayerController` (bark widget).

**Editor handoff:** suppressed-weapon DA variant, bark widget BP + DA_Barks, dormant guard-post cluster + patrol layouts in test level, death pose/montage on enemy ABP.

### Phase 2 landed (2026-06-09) — implementation notes

C++ complete (written + reviewed in main chat per user instruction), `Result: Succeeded`. Notes: suspicion meter lives in `UEnemyAwarenessComponent` (per-source tracks, sight fill modifiers: distance/angle/speed/crouch/prone; lighting deferred per design §13); prone/stance factors silently inert on the kit player (`AExtractionPlayer` interface returns false). Noise: weapon fire + reload (`UWeaponDataAsset` noise fields, suppressed = low values + `bSuppressed`), player footsteps (`UFootstepNoiseComponent`, both player classes, teleport-guarded), traversal (`UTraversalComponent`). Global alert = `UEnemyDirectorSubsystem` v1, one-way ladder, Loud floors Unaware enemies to Searching. Corpses: enemy controllers self-destroy on pawn death (no orphan perception cost); corpses go team-neutral so enemy sight (neutrals now on) discovers them; capped registry (10) recycles oldest. Takedown: Unaware-only, rear-arc + range from DA, `OnTakedownExecuted` BP hook, input on both player classes (`TakedownAction`). Barks: `UBarkSubsystem` (per-speaker cooldowns + 1.5s global type dedup) + `UBarkSetData` per archetype + `UBarkFeedWidget` base (BP child owns visuals); `ExtractionPlayerController` spawns it via `FClassFinder` at `/Game/Core/UI/WBP_BarkFeed` — matches that file's existing widget pattern but note it's a `/Game/` path in C++ (pre-existing file convention). No enemy DBNO anywhere. Suspicious BT branch is new editor wiring (`BTTask_EnemyFaceSuspicion`).

**QA:** crouch-walk past a guard unseen at distance that standing-sprint gets you spotted; sprint near a guard → he investigates the sound point; unsuppressed shot alerts the camp / suppressed shot only near enemies; body found → search + global alert raised; takedown from behind on Unaware works and stays silent; meter decays (peek briefly, hide, he settles).

---

## Phase 3 — Roster

**Goal:** *Each of the 7 types fights distinctly with its own legible counter.*

**C++ deliverables**
- `UEnemyArchetypeData` gains per-archetype blocks: armour profile (frontal arc °, frontal multiplier, rear/weakpoint multiplier), shield config (HP, arc, sidearm spread), grenadier config (count, cooldown, fuse, telegraph time, min/max range), officer aura (radius, accuracy buff, morale floor — morale half lands Phase 4), melee (range, damage, cooldown), sniper telegraph (aim time, laser-on time, relocate-on-spotted), turn-rate clamp (heavy), per-archetype movement speeds.
- **Bolt-on components** (`Public/Enemy/Components/` — register in Build.cs): `UEnemyArmourComponent` (hooks `AEnemyCharacter::TakeDamage`: hit direction vs facing → directional multiplier; plate-break delegate to BP at thresholds), `UEnemyShieldComponent` (child static mesh, own HP, blocks frontal hits routed by hit-component check; break → hide/disable + delegate), `UEnemyGrenadierComponent` (supply/cooldown, `SuggestProjectileVelocity` arc solve, spawns projectile, telegraph delegate), `USquadAuraComponent` (officer: radius buff with tracked `TSet` — no stacking, clean removal on death), `UEnemySniperTelegraphComponent` (timing state machine exposing laser on/off + aim point to BP visual). Components added conditionally in `ApplyArchetypeData` based on DA config presence.
- `AEnemyGrenadeProjectile` (`Public/Enemy/`): frag, fuse, radial damage, bounce; telegraph hooks (predicted landing for indicator); "Grenade out!" bark on throw.
- **New BT tasks** (`AI/Tasks/`): `BTTask_RusherAdvance` (sprint at target, fire-on-move wide spread, melee in range; ignores cover), `BTTask_HeavySuppress` (anchored sustained fire at target/last-known, turn-rate clamped), `BTTask_SniperNest` (pick perch — EQS or tagged high-cover slots, telegraph sequence, single shot, relocate when spotted/suppressed), `BTTask_OfficerCommand` (Phase-3 minimal: hold behind nearest allies, aura on), `BTTask_GrenadierLob` (trigger: target LOS-blocked > N sec → lob at last-known/cover), `BTTask_EnemyMelee`, `BTTask_ShieldAdvance` (steady walk-down, periodic sidearm peek-shots).
- Delete `AEnemyBase` + its remaining references after the test level swaps (this closes decision 13).

**Editor handoff:** 7 DA instances, 7 combat subtree BTs, BP children per archetype (meshes, shield mesh, laser Niagara, grenade indicator), test level arena with all archetypes.

**QA (one line per archetype):** grunt = Phase-1 yardstick; rusher sprints in and melees if allowed to arrive; heavy shrugs frontal fire, dies fast from rear/head, turns slowly enough to flank; sniper never fires without visible laser warning; officer buffs nearby enemies and hides behind the line; grenadier lobs at your cover only when you camp it, limited supply; shield blocks frontal, pops to grenade/sustained fire, brittle once flanked.

**Batches:** 3a rusher/heavy/sniper, 3b officer/grenadier/shield (review between).

### Phase 3 landed (2026-06-10) — implementation notes

C++ complete via 4-slice parallel team (data/character spine, defensive bolt-ons, offensive bolt-ons, BT tasks), 3 reviewers + 2 fix rounds + focused re-verification, `Result: Succeeded`. Deviations/decisions:
- **`AEnemyBase` NOT deleted** — file deletion was permission-blocked in the autonomous run. Self-referenced only; delete `Public/Enemy/EnemyBase.h` + `Private/Enemy/EnemyBase.cpp` manually (and any placed `BP_EnemyBase`) to close decision 13.
- Single batch instead of 3a/3b (parallel slices + full review loop covered it).
- New beyond contract: `MaxAimYawDeg` DA field (facing-gated fire — heavy can't shoot outside his turn arc); `IAIShooterInterface::GetAIAimLocation` (non-pure) + `SetAimLocationOverride` so HeavySuppress fires at last-known without an actor; weapon FF ignore list now cached per-burst (was per-shot world scan); enemies force `bAutoReloadOnEmpty=true` regardless of weapon BP config; `HandleDeath` sweeps bolt-ons (cancel grenade telegraph, aura off, laser off, shield collision off).
- Rusher uses `Combat` speed mode (no separate Sprint enum) — speed tuned via DA `CombatSpeed`. Search-after-combat runs at combat speed (intended: "stays edgy").
- Grenade ignores pawns (bounces only on world geometry — arcs at cover, not bodies) and uses radial falloff.
- Officer aura re-applies unconditionally per scan (multi-officer safe); `TActorIterator` scans (aura 1s, officer command 3s) accepted until P5 squad subsystem provides member lists.
- Shield = `UStaticMeshComponent` with own HP; block is purely geometric (hit-component routing) — `ShieldBlockArcDeg` currently informational. Placeholder `/Engine/BasicShapes/Cube` assigned; **manual**: proper shield mesh + relative offset/scale (component attaches at mesh origin — cube currently blocks all directions until repositioned).

**Editor wiring (NeoStack, verified by read-back):** 6 subtree BTs (`BT_EnemyCombat_Rusher/Heavy/Sniper/Officer/Grenadier/Shield` — selector: archetype task → cover+fire fallbacks; grenadier branch gated by inverted `HasLineOfSight` decorator, abort Both), 6 DAs (`DA_Enemy_*` — tuning, bolt-on flags, subtree/weapon/bark refs), `BP_EnemyGrenade`, 6 BP children (`BP_Enemy_*` — AIC + DA wired). **Manual remainder:** per-archetype mesh/anim on the 6 BP children (NeoStack inherited-component gap, same as grunt), shield mesh/offset, laser Niagara + grenade indicator visuals (bind `OnLaserChanged`/`OnGrenadeTelegraph`/`OnGrenadeCancelled`), per-archetype bark sets (all share `DA_Barks_Grunt` for now), sniper ideally gets a dedicated weapon BP (shares BP_Rifle), level placement.

**QA (Phase 3):**
1. Grunt = Phase-1 yardstick (regression: still patrols/covers/fires).
2. Rusher sprints straight at you ignoring cover, fires on the move (sloppy), melees at point-blank; burst him down before he arrives.
3. Heavy shrugs frontal fire (plates), dies fast to rear/head shots, turns visibly slowly — circle-strafe beats him; he can't fire outside his facing arc.
4. Sniper never fires without the telegraph window first (laser delegate fires 2s before the shot), relocates after 2 shots or when you wing him.
5. Officer hangs back behind his allies; nearby enemies shoot tighter while he lives (kill him → spread visibly loosens).
6. Grenadier fights like a grunt while he can see you; camp behind cover ~4s → "Grenade out!" bark + lob at your cover; max 3 grenades, 12s apart.
7. Shield walks you down blocking frontal fire (cube placeholder), pops to sustained fire or a grenade, then fights like a brittle grunt; flank shots kill normally.
8. Kill a grenadier mid-telegraph → no grenade appears; kill an officer → buff gone; kill a shield-carrier → corpse doesn't block bullets.

---

## Phase 4 — Morale & suppression

**Goal:** *Suppress an enemy and its head goes down; kill an officer and the squad turtles.*

**C++ deliverables**
- `USuppressionComponent` (`Public/Components/` — shared enemy + companion, never player): 0–1 value, rises from near-misses, fast decay (~1–2s), `IsSuppressed()`, `GetSuppression01()`, thresholds from DA/profile. Near-miss source: `AWeaponBase::PerformHitscan` reports bullet-segment proximity to registered AI pawns (subsystem-kept list or cheap world iteration — ~20 pawns, per-shot fine). Effects in BT/character: can't peek (CombatFire checks), spread widen, flinch/duck montage hook delegate.
- `UEnemyMoraleComponent` (`Public/Enemy/`): 0–100 + `EMoraleState` Confident/Shaken/Broken (floor = fall-back, never rout — design §7). DA profile per archetype: floor, fearless flag (rusher/shield-up), suppression resistance (heavy), brittleness (sniper), event weights. Events: ally died nearby, officer died, flanked, sustained suppression, low HP; up: damaged target, target downed, rally. Radius-based event discovery Phase 4; squad relay replaces it Phase 5. BB key `MoraleState` gates subtree branches (aggressive / cover-hug / deep-turtle) + new `BTTask_EnemyFallback` (cover pick biased farther from threat).
- **Companion suppression guardrails** (design §7): companion gets `USuppressionComponent`; its existing `IsSuppressed(window)` consumers extended minimally (suppressed = cautious peek cadence, never blocks `BTTask_RevivePlayer` — revive branch outranks combat already).
- **Hit reactions + ragdoll**: death → `SetSimulatePhysics` ragdoll (replaces pose-death from Phase 2 corpses, corpse persistence retained); hit-react montage hooks by `EHitRegion` (headshot special), heavy plate-break visual already delegated (P3).
- Barks: "Man down!", "He's got us pinned!", "Falling back!".

**Editor handoff:** hit-react/flinch montages, ragdoll physics asset check, morale-tuned DA values per archetype.

**QA:** sustained fire near a grunt → ducks, stops peeking, returns fire wildly if at all; rusher keeps coming through the same fire; heavy barely reacts; sniper relocates after one near miss; kill the officer → nearby enemies fall back to deeper cover and turtle; suppressed companion keeps fighting cautiously and still revives you under fire.

### Phase 4 landed (2026-06-10) — implementation notes

C++ via 4-slice team, 3 reviewers + 2 verification rounds, `Result: Succeeded`. Deviations/decisions:
- **Suppression near-miss source is hitscan-only** (per contract); grenade detonations produce no suppression/morale events — design §7's "his nades dent enemy morale too" deferred (P5 candidate).
- **Morale up-events** (NotifyDamagedTarget/TargetDowned) wired shooter-side in `WeaponBase::PerformHitscan`, gated on the victim having been alive (walls/corpses excluded). Officer-death morale radius-gated at 2× ally radius until P5 squad relay.
- **Shield "brittle once broken"** simplified to static `bFearless=true` (no shield-break→morale hook) — deviation from §7's table, revisit if it reads wrong.
- **Companion**: gets `USuppressionComponent`; `IsSuppressed(Window)` OR-extended (damage-recency ∥ near-miss component). Zero other companion changes; revive priority verified untouched.
- **Corpse inertness**: `HandleDeath` calls `DeactivateForDeath()` on both components (component-owned timers/subscriptions are NOT cleared by the actor's `ClearAllTimersForObject`). Suppression targeting skips NoTeam (corpse) pawns.
- **Takedown ragdoll deferred** by `TakedownRagdollDelay` (0.8s) so the BP takedown anim isn't stomped; normal deaths ragdoll instantly.
- Weapon caches (FF ignore + suppression targets) rebuilt per-burst single-pass; player weapons also report near-misses (player suppressing enemies is core design).

**Editor wiring (verified by read-back):** BB_Enemy + `MoraleState` key (enum); 5 non-fearless subtrees (Grunt/Heavy/Sniper/Officer/Grenadier) REBUILT with `MoraleState==Broken → BTTask_EnemyFallback` (abort Both) as first branch — rebuild required because BT child order is insertion-based (delete needs a fresh execute_script call after clearing the DA ref: stale in-memory referencer blocks same-call delete); DA tuning: `MoraleLossOfficerDied=75` (non-fearless), heavy `SuppressionResistance=3`/`MoraleEventResistance=2`, sniper `0.5`/`0.5`, rusher+shield `bFearless=true`; DA_Barks_Grunt + ManDown/Pinned/FallingBack lines. **Manual remainder:** flinch/hit-react montages bound to `OnHitReact(EHitRegion)`, suppression flinch via `OnSuppressedStateChanged`, ragdoll physics asset check on the enemy mesh, takedown anim on `OnTakedownExecuted`.

**QA (Phase 4 additions to the table above):**
1. Fire repeatedly NEAR (not at) a covered grunt → head goes down, no peeking, return fire (if any) is visibly wild; stop firing ~2s → he resumes peeking.
2. Heavy under the same fire barely reacts (resistance 3×); sniper relocates off a single near-miss.
3. Kill the officer → nearby non-fearless enemies bark "Falling back!", move to farther cover, crouch, peek rarely; rusher/shield unaffected.
4. Broken enemy under suppression never peeks at all (turtle + head down stack).
5. Enemy deaths ragdoll; takedown plays its anim window before ragdoll; corpses don't bark, don't accumulate suppression, still trigger body discovery.
6. Suppress the companion (fire near it) → it ducks/repositions more cautiously but keeps fighting, and still completes a revive under fire.
7. Killing enemies near their squadmates triggers "Man down!" from the living, not the dead.

---

## Phase 5 — Squad baseline

**Goal:** *A squad spreads, shares sightings, and flanks — minus bounding overwatch.*

**C++ deliverables**
- `UEnemySquadSubsystem` + `UEnemySquad` (`Public/Enemy/Squad/` — register in Build.cs): membership via `SquadId` FName on placed enemies (auto-group at BeginPlay) or director-created groups; squads never coordinate with each other (design §6).
- **Shared sightings:** member reaches Combat → squad broadcasts target + last-known → members fast-track (Searching minimum, Combat if design-confirmed); squad-level last-known updated by any member with LOS.
- **Spacing:** squad exposes member positions; `BTTask_EnemyMoveToCover` penalizes/filters slots within `MinAllySpacing` of a squadmate (claim system already prevents exact sharing).
- **Single opportunistic flanker:** squad role token (`TryClaimRole(Flanker)` / release); flank EQS query (ring around target, prefer out-of-facing, reachable) + `BTTask_EnemyFlank`. Survival pre-empts: low HP/suppressed → release token (the BT priority encodes the design's hard rule).
- **Focus-fire:** squad `FocusTarget` (member call on damage/officer call); feeds target selection.
- **Threat-scored targeting** (replaces nearest-hostile from Phase 1, design §10): score = proximity + LOS + recent-damage-to-me/squad + exposure; officer focus-fire overrides within the squad. Lives in awareness component's target pick.
- **Officer proper:** assigns suppressor/flanker roles, rally (morale floor raise + un-pin), squad morale aggregation; officer-death squad event replaces Phase-4 radius hack.
- Barks: "Flanking left!" (side-aware), "Suppressing — move up!", officer designations; squad-level bark dedup.

**Editor handoff:** EQS flank query asset, SquadId set on placed groups, officer-led squad test layout.

**QA:** squad of 4 spreads across separate cover; spot one member → the rest converge on your position; exactly one enemy flanks at a time; wound the flanker → he aborts to cover; officer alive → coordinated roles + focus fire on the officer's pick; officer dead → squad degrades to baseline; companion drawing fire reads as threat-scoring working (lighting up an enemy pulls its aggro).

### Phase 5 landed (2026-06-10) — implementation notes

C++ via 4-slice team, 3 reviewers + 2 verification rounds, `Result: Succeeded`. Deviations/decisions:
- **Flank point picking = C++ ring solver** (12 samples, behind-target-facing + angular-displacement scoring, nav-projected, MoveToLocation result-checked) — NO EQS asset (same semantics, no editor dependency). Flank cooldown persists on the squad (`RecordFlankAttempt`), not NodeMemory.
- **Sighting propagation = Searching-minimum** (relay never forces Combat; members confirm via own perception). Relay-loop guarded. Unsighted target adoption no longer claims LOS (wallhack fix).
- **Threat scoring** = proximity + LOS + recent-damage with switch hysteresis; exposure term dropped (no cheap signal). Damage instigator force-tracked so back-shots always become candidates. Officer focus override requires the member to actually sight the focus target.
- **Focus-fire**: officer re-calls on cooldown with explicit `bOfficerCommand` override; members set-when-empty on damage (leaderless squads focus too); focus cleared on officer death.
- **Morale relay**: same-squad deaths via squad (FName SquadId comparison — subsystem lookup defeated by broadcast order); cross-squad/squadless deaths keep the P4 radius path (exactly-once both directions). Takedown stealth preserved: relay gated on any member ≥ Searching. Rally walks 0→50 (floor-before-boost + Shaken backstop).
- **Spacing**: penalty (0.2×) on slots near squadmates' claimed slots (pawn-position fallback) — replicates FindBestCoverFor's post-vacate/LOS/fire-arc filters in the squad-aware path.
- Deferred to P7: Suppressor role + "Suppressing — move up!" bark (token exists, unclaimed). Side-aware flank bark not implemented (single generic line).
- `UEnemySquadSubsystem` declares `InitializeDependency<UEnemyDirectorSubsystem>`; squads never cross-coordinate; `CreateSquadForGroup` ready for the P6 director.

**Editor wiring (verified by read-back):** BT_EnemyCombat_Grunt rebuilt — Selector: [Broken→Fallback] → [BTTask_EnemyFlank] → [MoveToCover+CombatFire] → [CombatFire]; DA_Barks_Grunt + Flanking/FocusTarget lines (10 types). **Manual remainder (user):** set `SquadId` on placed enemy groups (EditInstanceOnly per instance, e.g. "Alpha" on 4 enemies + 1 officer), officer-led squad test layout.

**QA (Phase 5 additions):**
1. Two squads ("Alpha"/"Bravo") never share sightings — alert one squad, the other keeps patrolling.
2. Suppressed-pistol a squad member from stealth: his squadmates converge on the kill zone only AFTER someone reaches Searching (body find/noise) — a clean takedown on a fully unaware squad stays silent.
3. Shoot the officer repeatedly — squad focus does NOT thrash onto you each hit (his command call is on a 10s cooldown).
4. Kill the squad's focus target — members immediately re-engage a visible second hostile (no squad-wide Searching dump).
5. Flanker picks routes behind your facing; wound or suppress him mid-flank → he aborts to cover and no new flank starts for ~8s.

---

## Phase 6 — Director

**Goal:** *Go loud, get adaptive pressure with relief beats; extraction repopulates cleared areas.*

**C++ deliverables**
- `UEnemyDirectorSubsystem` v2 (move to `Public/Enemy/Director/` if crowded — Build.cs): tension estimate (recent player damage taken/dealt, kills, enemy proximity, downs) on ~1s cadence; sawtooth state machine Build → Peak → Relief (design §9 — relief after big fights is the load-bearing beat); spawn queue honouring `MaxAliveCap` (~20), distance band, out-of-player-sightline checks, nav validity.
- `AEnemySpawnZone` (`Public/Enemy/Director/`): designer-placed, registers to subsystem (cover-registry pattern), tagged by area/phase.
- **Pre-formed squad spawns:** director composes weighted-random squads per mission phase (composition weights = data, light adaptation only — no perfect-counter conjuring, design §9) and registers them with the squad subsystem on spawn.
- `UDirectorConfigData` (DA): tension curve params, spawn cadence, intensity ceiling per mission phase, composition weights per phase, max-alive cap.
- **Mission phase API:** `EMissionPhase` (Infiltration/Objective/Extraction) BP-callable setter (level scripting/objective triggers own when); Extraction = crescendo + repopulation of cleared zones; heavies/officers unlock by phase.
- Dormant placed force untouched while Calm; director wakes on Loud (Phase-2 seam).

**Editor handoff:** spawn zones placed (barracks/stairwells/off-map ingress), DA_DirectorConfig, mission-phase trigger wiring, High-Rise extraction route test.

**QA:** stay Calm → zero spawns; go Loud → first wave arrives as a squad from out of sight at sane distance; after a heavy fight pressure visibly eases, then rebuilds; alive count never exceeds cap (trickle once the front thins); extraction phase escalates and repopulates cleared floors.

### Phase 6 landed (2026-06-10) — implementation notes

C++ via 2-slice team, 3 reviewers + verification round, `Result: Succeeded`. Deviations/decisions:
- **Director stays in `Public/Enemy/`** (no file move); new `Enemy/Director/` holds only SpawnZone + DirectorConfigData.
- **No cleared-zone tracking** — extraction "repopulation" = continued spawning at crescendo intensity; zones behind the player stay eligible (sightline check short-circuits behind-player as safe).
- **Config delivery**: zone-carried `DirectorConfig` adopted as fallback; explicit `SetDirectorConfig` (level BP) overrides; `Validate()` logs misconfigurations at adoption.
- **Arrival chain** (was the headline review catch): awareness polls `GetAlertLevel()` at init (catch-up Loud floor for post-Loud spawns) AND the director seeds each spawned squad with the player's location via the squad sighting relay — waves converge on the fight at Searching.
- **Cap = headroom-gated** (composition filtered to fit `Alive + Size <= MaxAlive`; squads never trimmed). Dormant placed force counts toward the cap — that IS the trickle; per-level tuning trap: Infiltration cap (8) must exceed the placed force where reinforcements are wanted.
- Tension inputs: player health delta (cached weak poll), kills, engaged-enemy count; single `TActorIterator` sweep at 1s, zero work while Calm. `RecentKills` zeroed at Loud wake (stealth kills don't lump). Phase escalation forces Build (no Relief dead-air at extraction start).
- Per-spawn-point nav projection + sightline sampling (centre + 3 points); spawn failures tolerated per member.

**Editor wiring (verified by read-back):** `DA_DirectorConfig` — per-phase compositions authored (Infiltration: grunt pair; Objective: grunts/flankers/officer-led/grenadier-flush; Extraction: heavy-push/rusher-rush/shieldwall/sniper-overwatch/officer-led), cadence/cap/ceiling per pinned defaults. **Manual remainder (user):** place `AEnemySpawnZone` actors (barracks/stairwells/off-map ingress; box extents on navmesh), set `DirectorConfig=DA_DirectorConfig` on ONE zone (or call SetDirectorConfig from level BP), wire mission-phase triggers (`SetMissionPhase` BlueprintCallable on the director), navmesh over zones.

**QA (Phase 6):**
1. Stay Calm (stealth) → zero spawns regardless of time elapsed.
2. Go Loud → within one cadence (45s Infiltration) a grunt pair spawns out of sight, then converges on your position.
3. Watch `LogEnemyAI`: tension climbs in fights; at Peak spawning stops; Relief gives ~25s of quiet; pressure rebuilds.
4. Alive count (placed + spawned) never exceeds the phase cap; kills open headroom → trickle.
5. Trigger Extraction (level BP) → faster waves, heavies/shields/snipers appear, spawning continues behind you on the way out.
6. Delete all spawn zones → one warning log, no spawn spam, no crash.

---

## Phase 7 — Bounding overwatch

**Goal:** *Officer-led squad pins-and-bounds; degrades cleanly when sync breaks.*

**C++ deliverables**
- Squad maneuver state machine on `UEnemySquad` (officer-gated): assign suppressor + flanker → suppressor runs `BTTask_EnemySuppressFire` (sustained area fire at target/cover without needing LOS, ammo-aware) → flanker advances **only while suppression is live** (`BTDecorator_SuppressionLive` reading squad state every tick of the move) → roles swap → repeat.
- Abort rules: suppressor reloading/dead, officer dead, flanker suppressed/low-HP → flanker holds or aborts to cover; maneuver degrades to Phase-5 baseline (diegetic absence in leaderless squads — design §6).
- Barks: "Covering — go!", swap calls.

**Editor handoff:** overwatch test arena (long sightline + flank routes).

**QA:** officer squad executes visible pin-and-bound toward the player; kill the suppressor mid-bound → the flanker stops advancing; kill the officer → no more bounding, squad reverts to baseline; design's "riskiest maneuver, cut-safe" property holds (removing it leaves Phase-5 behaviour intact).

### Phase 7 landed (2026-06-10) — implementation notes

C++ via 2-slice team, 3 reviewers + 2 verification rounds, `Result: Succeeded`. Deviations/decisions:
- **Maneuver roles restricted to Grunt-archetype members** (specialists keep signature behaviours); eligibility requires Combat awareness + valid combat target.
- **`IsSuppressionLive` = actual engagement** (task-owned flag, identity-aware writes so a post-swap stale CleanUp can't stomp the new suppressor) && suppressor alive && !reloading. Reload = `IsSuppressionHolding` — flanker's branch aborts but does NOT tear down the maneuver; the decorator re-activates on resume (tracks condition change in node memory, fires `ConditionalFlowAbort` both directions).
- **Zombie-proofing**: `IsBoundingActive` lazily validates target/role weak ptrs; both tasks notify-blocked on external aborts only when still holding their role (legit swaps excluded); officer cooldown squad-persisted (full cooldown on failed attempts — accepted simplification of half-cooldown).
- Swap rate-limited (2s) + re-entrancy-guarded; `AlreadyAtGoal`/partial paths = blocked, arrival distance-verified.
- Officer alive-but-Broken doesn't stop the maneuver (death only) — accepted; revisit if it reads wrong.
- Cut-safe verified: never-triggered StartBounding = zero P5/P6 delta.

**Editor wiring (verified by read-back):** BB_Enemy + `ManeuverRole` key; `BT_EnemyCombat_Grunt` rebuilt — Selector: [Broken→Fallback] → [ManeuverRole==Suppressor → SuppressFire] → [ManeuverRole==Flanker + SuppressionLive → BoundingAdvance] → [Flank] → [MoveToCover+Fire] → [Fire], all role decorators abort Both; `DA_Barks_Grunt` + CoveringGo (11 types). **Manual remainder (user):** overwatch test arena (long sightline + flank routes), officer-led squad placement (SquadId + officer + ≥2 grunts).

**QA (Phase 7):**
1. Officer + 3 grunts vs you in a long sightline: one grunt opens sustained suppressing fire ("Suppressing!"), another bounds forward in hops while it fires, "Covering — go!" on swaps — visible pin-and-bound.
2. Kill the suppressor mid-bound → the flanker stops advancing immediately, maneuver ends, squad reverts to P5 baseline.
3. Suppressor reloads mid-bound → flanker pauses in place, resumes when fire resumes (no teardown).
4. Kill the officer → no new bounding ever starts; current maneuver stops.
5. Suppress or wound the flanker mid-bound → he aborts to cover; maneuver ends cleanly.
6. Leaderless squad: never bounds (officer-gated); rusher/shield/sniper squads: never bound (grunt-only roles).

---

## Risk register

| Risk | Mitigation |
|---|---|
| Team IDs regress companion perception (worked via everyone-neutral + `bDetectNeutrals=true`) | Companion keeps `bDetectNeutrals=true`; enemies become Hostile with `bDetectEnemies=true` already set; companion gets player via `GetPlayerCharacter(0)` not perception. Re-verify in every phase-1 review + QA #7. |
| Weapon FF rewrite changes combat behaviour | Team-compare reproduces old semantics exactly (enemy⇄enemy, companion⇄player/companion); QA #9 + #11 cover both. |
| Kit player (`AExtractionPlayer`) hides sprint/prone from C++ | Awareness uses velocity + native `bIsCrouched`; prone factor silently inert on kit player. If stance fidelity matters later, expose via `IExtractionPlayerInterface` BP implementation. |
| `SetDynamicSubtree` silently no-ops if the base BT's dynamic node lacks `TAG_BT_EnemyCombat` | Called out in handoff checklist; QA #2 catches it (enemy would idle in Combat). |
| Corpse persistence (P2) vs perf | Hard cap + recycle oldest; corpses drop collision + tick. |
| Suppression near-miss checks per shot (P4) | ~20 registered pawns, segment-distance math only; profile with `stat AI` if heavies sustain fire. |
| Companion combat brain (2872-line task) accidentally entangled | Rule: enemy never reuses companion BT nodes or tuning DA; shared surface = registry, health, weapon, interfaces only. |
| BT assets drift from C++ key names | BB keys are `static const FName` on the controller; handoff lists exact names; mismatch shows as idle-in-state (QA scenarios catch). |

---

## Out of scope (per design §13)

Surrender/rout, player-side suppression, co-op, smoke/flash, throwback, full VO, body-dragging, exact tuning values (live in DA instances, set during balance).
