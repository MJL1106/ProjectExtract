---
name: ue5-ai-systems
description: UE5 AI architecture guide for companion and enemy systems in a multiplayer FPS. Use when the user works on AI behavior, enemies, companions, behavior trees, EQS, perception, cover, squad coordination, DBNO/revive, or any AI decision-making. Also use when the user describes wanting enemies or NPCs that do something specific.
---

# UE5 AI Systems — Companion & Enemy

## Purpose
Architecture guide for the AI systems in a multiplayer FPS dissertation project. Covers companion AI (follow, fight, revive), enemy archetypes (grunt, rusher, heavy, sniper, officer), and shared systems (weapons, health, faction tags, EQS, nav mesh).

## Architecture Overview

### Class Hierarchy
```
ACharacter
├── APlayerCharacter          (player-controlled)
├── ACompanionCharacter       (companion pawn)
└── AEnemyCharacter           (single class — behavior driven by UEnemyArchetypeData)

AAIController
├── ACompanionAIController    (companion BT + perception)
└── AEnemyAIController        (enemy BT + perception, loads archetype DataAsset)

UActorComponent
├── UHealthComponent          (shared — player, companion, enemies)
├── UWeaponComponent          (shared — manages AWeaponBase)
├── UAIFormationComponent     (companion-only — formation offset logic)
└── USquadBuffComponent       (officer archetype — accuracy aura)

AActor
└── AWeaponBase               (shared weapon actor)

UPrimaryDataAsset
└── UEnemyArchetypeData       (one asset per archetype — all tuning lives here)
```

**Why one enemy class?** Five subclasses (AEnemyGrunt, AEnemyRusher, etc.) violate composition-over-inheritance. All behavioral differences are driven by `UEnemyArchetypeData` + archetype-specific BT subtrees injected at runtime. The single `AEnemyCharacter` class reads its DataAsset in `BeginPlay` and configures movement speed, health, perception radius, and weapon accordingly. Officer-specific components (like `USquadBuffComponent`) are added conditionally based on archetype type.

### Shared Systems
Both companion and enemies use:
- **UHealthComponent** — damage, death, DBNO state. Fires delegates: `OnHealthChanged`, `OnDeath`, `OnDBNO`
- **AWeaponBase** — firing, ammo, reload, simulated accuracy. Equipped via `UWeaponComponent`
- **IGenericTeamAgentInterface** — faction system on controllers. Team 0 = player/companion, Team 1 = enemies. Perception uses this automatically for affiliation detection
- **Nav Mesh** for pathfinding
- **AI Perception** — sight + hearing stimuli

### State Flow (Both AI Types)
```
[Idle/Patrol] → detect stimulus → [Alert] → confirm threat → [Combat]
                                                                  ↓
                                                          target lost → [Search] → timeout → [Patrol]
```
Companion adds: `[Follow]` as default, `[Revive]` as highest priority override.

## Companion AI

### Behavior Tree Priority (highest first)
```
Root (Selector)
├── [1] Revive Sequence         ← highest priority, interrupts everything
│   ├── Decorator: HasDBNOAlly? (Observer Aborts: Both — re-evaluates while lower branches run)
│   ├── BTTask: FindClosestDBNOAlly → BB_ReviveTarget
│   ├── BTTask: MoveTo(BB_ReviveTarget, AcceptanceRadius=100)
│   └── BTTask: PerformRevive(BB_ReviveTarget, Duration=3.0)
│
├── [2] Combat Sequence
│   ├── Decorator: HasCombatTarget? (Observer Aborts: Both — re-evaluates so revive can interrupt)
│   ├── BTTask: RunEQS_FindCover → BB_CoverLocation
│   ├── BTTask: MoveTo(BB_CoverLocation)
│   ├── BTTask: AimAtTarget(BB_CombatTarget)
│   └── BTTask: FireWeapon (latent — fires for duration, then re-evaluates cover)
│
└── [3] Follow Sequence         ← default fallback
    ├── BTTask: CalculateFormationPoint → BB_FollowLocation
    ├── Decorator: IsOutOfFormationRange?(Threshold=300)
    └── BTTask: MoveTo(BB_FollowLocation, AcceptanceRadius=150)
```

**Decorator Abort Types:**
- `Observer Aborts: Both` on Revive decorator = if a DBNO ally appears while in Combat or Follow, the BT immediately interrupts and switches to Revive
- `Observer Aborts: Both` on Combat decorator = if a combat target appears while Following, BT switches to Combat. If combat target disappears, BT falls through to Follow
- Without proper abort types, the companion won't interrupt lower-priority branches when higher-priority conditions become true

**Cover Re-evaluation:** After firing for a duration, the combat sequence loops back to re-run EQS. This prevents the companion from staying in cover that's been flanked. The cover EQS query should run every ~3-5 seconds during combat, not every frame.

### Key Blackboard Keys
| Key | Type | Set By |
|-----|------|--------|
| `BB_FollowTarget` | Object (AActor) | Controller on Possess — the player |
| `BB_CombatTarget` | Object (AActor) | Perception update |
| `BB_ReviveTarget` | Object (AActor) | DBNO delegate + BT service scan |
| `BB_CoverLocation` | Vector | EQS result |
| `BB_FollowLocation` | Vector | Formation calculation |
| `BB_CurrentState` | Enum | BT services |
| `BB_AmmoCount` | Int | Weapon component |

### DBNO Detection
Two complementary approaches for detecting allies that need reviving:

1. **Direct delegate binding (primary, instant):** In `ACompanionAIController::OnPossess`, bind to `UHealthComponent::OnDBNO` on the player character. When DBNO fires, immediately set `BB_ReviveTarget`. This gives instant response — no polling delay.

2. **Perception-based scan (secondary, backup):** BT service scans perceived actors for DBNO state every 0.25s. Catches cases where the companion gains LOS on a DBNO ally it wasn't already tracking (e.g., in co-op with multiple players).

See `references/companion-bt.md` for delegate binding implementation.

### Formation Following
- Offset: behind and to the side of the player (configurable via `UAIFormationComponent`)
- Default: `(-200, 150, 0)` relative to player forward
- Adapt offset if multiple companions (co-op): spread formation
- Smooth interpolation of target point — don't snap
- If too far (>1500 units), sprint to catch up
- If path blocked, find nearest nav-reachable point to ideal offset

### AI Perception Setup
```cpp
// In ACompanionAIController constructor
PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));

// Sight config
auto* SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
SightConfig->SightRadius = 2500.f;
SightConfig->LoseSightRadius = 3000.f;
SightConfig->PeripheralVisionAngleDegrees = 120.f;
SightConfig->DetectionByAffiliation.bDetectEnemies = true;
SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
SightConfig->DetectionByAffiliation.bDetectFriendlies = true; // for DBNO allies
SightConfig->SetMaxAge(5.f);

// Hearing config
auto* HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
HearingConfig->HearingRange = 3000.f;
HearingConfig->SetMaxAge(3.f);

PerceptionComponent->ConfigureSense(*SightConfig);
PerceptionComponent->ConfigureSense(*HearingConfig);
PerceptionComponent->SetDominantSense(UAISense_Sight::StaticClass());
```

### Affiliation Setup (IGenericTeamAgentInterface)
Implement `IGenericTeamAgentInterface` on AI controllers for automatic perception affiliation:
```cpp
// Companion controller — same team as player
FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(0); }

// Enemy controller — hostile team
FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(1); }
```
Perception's `DetectionByAffiliation` uses this automatically — no manual faction tag checking needed in perception handlers. For non-AI-controlled actors (players), implement `IGenericTeamAgentInterface` on `APlayerController` with `FGenericTeamId(0)`.

### Weapon Accuracy Simulation
- Spread starts wide on first shot
- Settles over time while holding aim on target
- Resets on target switch or movement
- Configurable per difficulty: `FCompanionAccuracyConfig` in DataAsset

### Co-op Specifics (Multiplayer)
- Companion follows the **host player** — use `GetWorld()->GetFirstPlayerController()` on the server, which returns the listen server host's controller. Avoid `PlayerState->GetOwner()` which may not return what you expect
- Revive priority: closest DBNO player regardless of who they are
- All AI runs on server — companion pawn replicates state to clients
- Companion state (follow/combat/revive) replicated for client UI

See `references/companion-bt.md` for full BT task and service implementations.

## Enemy AI

### Archetypes

| Archetype | Behavior | Key Config |
|-----------|----------|------------|
| **Grunt** | Baseline. Patrol → detect → take cover → engage. | Medium range, medium accuracy, standard health |
| **Rusher** | Closes distance aggressively. Minimal use of cover. Sprint toward player. | Short range, fast movement, low health, high damage up close |
| **Heavy** | Anchors position. Absorbs damage. Suppresses with sustained fire. | Long engagement, slow movement, high health, large weapon |
| **Sniper** | Holds sightlines at range. Repositions if flanked. | Long range, high accuracy, low fire rate, low health |
| **Officer** | Buffs nearby enemies. Coordinates squad behavior. Stays behind front line. | Medium range, buff aura component, squad command logic |

All archetypes use the same `AEnemyCharacter` class. Behavioral differences come from:
1. **UEnemyArchetypeData** — DataAsset sets stats (health, speed, accuracy, engagement range)
2. **BT subtree injection** — DataAsset references an archetype-specific `UBehaviorTree` for the combat branch
3. **Conditional components** — Officer archetype adds `USquadBuffComponent` in `BeginPlay`

### Enemy BT Structure (Base — all archetypes share)
```
Root (Selector)
├── [1] Combat Sequence
│   ├── Decorator: HasCombatTarget? (Observer Aborts: Both)
│   ├── Service: UpdateCombatInfo (range, LOS, ammo)
│   ├── [Archetype-specific combat subtree — injected via RunBehaviorDynamic]
│   └── BTTask: HandleReload (if needed)
│
├── [2] Alert/Search Sequence
│   ├── Decorator: HasLastKnownLocation? (Observer Aborts: Both)
│   ├── BTTask: MoveTo(BB_LastKnownLocation)
│   ├── BTTask: LookAround(Duration=3.0)
│   └── BTTask: ClearAlert → return to patrol
│
└── [3] Patrol Sequence
    ├── BTTask: GetNextPatrolPoint → BB_PatrolTarget
    └── BTTask: MoveTo(BB_PatrolTarget, AcceptanceRadius=50)
```

Archetype-specific combat is injected as a subtree reference so the base BT stays clean.

### Archetype Combat Subtrees

See `references/enemy-archetypes.md` for full subtree definitions.

**Grunt:** Move to cover → peek and fire → suppress if player advances
**Rusher:** Sprint to close range → fire while moving → melee if very close
**Heavy:** Hold position → sustained fire → turn slowly → suppress area
**Sniper:** Acquire sightline → wait for shot → fire → reposition if detected
**Officer:** Stay behind squad → apply buff aura → call out targets → retreat if exposed

### Officer Squad Coordination
- `USquadBuffComponent` added conditionally to enemies with Officer archetype
- Buff: accuracy multiplier to squad members within radius. **Tracks buffed actors** — stores them in a `TSet` to prevent stacking from multiple officers and to correctly remove buffs on death
- Callout: officer sets `BB_PriorityTarget` on squad members' blackboards
- If officer dies, `USquadBuffComponent::RemoveAllBuffs()` iterates tracked set and resets multipliers

### Enemy Density Escalation
- Mission phases tracked in `AGameState` (e.g. `EMissionPhase::Infiltration`, `Extraction`)
- Extraction phase: `UEnemySpawnManager` increases spawn rate, unlocks heavier archetypes
- Configure via DataTable: phase → spawn weights per archetype

## EQS Patterns

### Companion Cover Query
```
Generator: Points around querier (Grid, Radius=1500, Density=200)
Tests:
1. Trace to BB_CombatTarget     — Prefer: blocked (this IS cover). Weight: 1.0
2. Distance to BB_CombatTarget  — Prefer: 800-1200 range (scoring curve). Weight: 0.8
3. Distance to BB_FollowTarget  — Prefer: closer to player (don't wander). Weight: 0.7
4. PathLength to test point     — Filter: must be reachable
5. Dot product (point→enemy vs point→player) — Prefer: between enemy and player. Weight: 0.3
```

### Enemy Flank Query (future)
```
Generator: Points on ring around BB_CombatTarget (Radius=800)
Tests:
1. Trace to BB_CombatTarget        — Prefer: has LOS
2. Trace FROM BB_CombatTarget      — Prefer: NOT visible from target's facing
3. Distance to nearest ally        — Prefer: spread out (avoid clustering)
4. PathLength                      — Filter: reachable, prefer shorter
```

See `references/eqs-patterns.md` for C++ EQS task implementation and full editor setup details.

## AI Performance Budget

### Throttling Guidelines
- **BT Services:** 0.25-0.5s interval with `RandomDeviation` to stagger across AI. Never every frame.
- **EQS Queries:** Run every 3-5 seconds during combat, not every BT tick. Cover re-evaluation doesn't need to be instant.
- **Perception:** Max age settings prevent stale data buildup. Don't call `GetCurrentlyPerceivedActors()` outside of service ticks.
- **AI Count Budget:** Profile with `stat AI` and `stat EQS`. Target: 20 AI simultaneously active without exceeding 4ms total AI frame cost.
- **Staggering:** Use `RandomDeviation` on all BT service intervals. If 20 enemies run EQS on the same frame, you'll hitch.
- **LOD for AI:** Enemies far from all players can reduce BT tick rate or disable perception. Use `NetCullDistanceSquared` to control this.
- **Object pooling:** Required for enemies if spawning frequently during extraction phase.

### Frame Budget Target
```
Total AI budget per frame: ~4ms at 60fps (25% of 16ms frame)
├── BT evaluation: ~0.5ms for 20 active AI
├── EQS queries: ~1.0ms (staggered, 2-3 queries per frame max)
├── Perception: ~1.0ms for 20 active AI
├── Pathfinding: ~1.0ms (async, spread across frames)
└── Overhead: ~0.5ms
```

## Build.cs Dependencies
Any class using AI needs these modules:
```csharp
"AIModule",
"GameplayTasks",
"GameplayTags",
"NavigationSystem"
```
For EQS: `"EnvironmentQuery"`
For perception: already in `AIModule`

## Implementation Timeline

### Prototype Phase
- [ ] Basic companion: follow player in formation, detect enemies, shoot at them
- [ ] UHealthComponent with damage and death
- [ ] AWeaponBase with firing and basic accuracy
- [ ] Static/patrolling enemy targets (grunt only)
- [ ] Basic nav mesh coverage

### Middle Phase
- [ ] Companion EQS cover-seeking
- [ ] Companion revive (DBNO system)
- [ ] Enemy BTs for all archetypes
- [ ] AI Perception (sight + hearing) for both
- [ ] IGenericTeamAgentInterface faction system

### Final Phase
- [ ] Officer squad coordination
- [ ] Enemy density escalation per mission phase
- [ ] Companion accuracy settling
- [ ] Co-op companion behavior
- [ ] Voice barks (stretch)
- [ ] Adaptive aggression (stretch)
- [ ] Low-health retreat (stretch)

## Critical Rules for AI Code
- All AI Controllers run on **server only** — never assume client execution
- All AI decisions happen on server — replicate results for client visuals
- Cache perception results — don't query `GetCurrentlyPerceivedActors()` every frame
- BT Services run at intervals (0.25-0.5s) not every frame — set appropriate intervals with `RandomDeviation`
- EQS queries are async — don't block on results, use callbacks
- Null-check Blackboard before every `GetValueAs*` call
- Null-check pawn in every BT Task `ExecuteTask` — pawn can be destroyed mid-execution
- `MoveTo` can fail — handle `EPathFollowingResult::Blocked` and `OffPath`
- Object pooling for enemies if spawning frequently during extraction phase
- BT Tasks that store per-instance state **must** use `NodeMemory` (via `GetInstanceMemorySize()`), not member variables — BT nodes are shared across instances
- Use `TWeakObjectPtr` for any UObject pointer stored across async boundaries (EQS callbacks, timers)
- Use `IGenericTeamAgentInterface` for faction checks — don't assume actors have `AAIController` (players use `APlayerController`)
