# AI Companion: Implementation Roadmap

ProjectExtract, May 2026
Target: companion feature-complete by early July 2026 (~6 weeks)

---

## Purpose of this document

This is the execution plan for taking the companion from its current prototype state to the vision described in the two GDDs:

- [`companion-gdd-gameplay-2026-05-23.md`](companion-gdd-gameplay-2026-05-23.md) — player-facing design
- [`companion-gdd-technical-2026-05-23.md`](companion-gdd-technical-2026-05-23.md) — architecture

This roadmap is consumed by future Claude sessions. Each phase is self-contained: read the goal, do the C++ work, hand off the editor work, run the verification, commit, move on. Phases are ordered by dependency.

---

## Timeline

| Phase | Topic | Estimate | Cumulative |
| --- | --- | --- | --- |
| 1 | Posture-aware behaviour + player switching | 4 days | Week 1 |
| 2 | Companion DBNO + player-side revive | 5 days | Week 2 |
| 3 | Command surface foundation (ping + core commands) | 6 days | Week 3 |
| 4 | Command surface extension (wheel + remaining commands) | 5 days | Week 4 |
| 5 | VIP missions (escort, VIP AI, cower, fail check) | 5 days | Week 4-5 |
| 6 | Auto-suggest + HUD layer | 4 days | Week 5 |
| 7 | Bark system + first character DataAsset | 4 days | Week 5-6 |
| 8 | Tutorial integration | 3 days | Week 6 |

Total: ~36 days of focused work over 6 weeks (allows slack for editor wiring, asset acquisition, and reviewer-fix loops).

**Not in this roadmap:** the cover-progression / cover-to-cover advancement work is being handled in a separate chat. Assume it lands during Phase 1 or 2 and doesn't block anything here.

---

## Current state snapshot

Working at commit `3ec6f97` (May 17):

| System | State | Notes |
| --- | --- | --- |
| Follow + sprint catch-up | Working | Formation offsets driven by `FCompanionPostureProfile` in tuning DA |
| Combat (cover-based) | Working | Authored slot system (`AAICoverSlot` + `UCoverRegistrySubsystem`), peek matrix (Stand / Quick / Hold / Reposition / StandUpAndReposition / CornerPeek), reload gates, watchdog timer, suppression gates, low-ready aim |
| Revive player (DBNO) | Working | `BTTask_RevivePlayer` with full edge-case handling |
| Traversal mirror | Working | Shared `UTraversalComponent`, mirror task, warp safety nets (soft / hard / Z-mismatch) |
| Posture infrastructure | Partial | `ECompanionPosture` enum (Exploration / Combat / Stealth), `BB_Posture` key, replicated property, `FCompanionPostureProfile` with formation + scoring weights. **Not yet consumed for fire policy, audio, or cover stickiness.** |
| Health widget | Working | `UWidgetComponent` anchored on head, `WBP_CompanionHealth` |
| Companion DBNO | **Missing** | `HandleDeath` calls `DestroyAfterDeath` (3s). No downed state on companion. |
| Player-side revive | **Missing** | Player has DBNO but companion cannot revive trigger from player side; only auto via existing task. |
| Command surface | **Missing** | No ping system, no wheel, no command branch in BT |
| VIP missions | **Missing** | No escort branch, no VIP AI, no mission failure on team wipe |
| Auto-suggest | **Missing** | No `SuggestedMode` key, no HUD prompt |
| Bark system | **Missing** | No `UBarkComponent`, no character DataAsset |
| Tutorial | **Missing** | No in-fiction lesson triggers |

---

## Working conventions for this codebase

These are non-negotiable and stem from the project's existing patterns. Future Claude must follow them.

- **C++ stays asset-agnostic.** No `/Game/` paths via `ConstructorHelpers::FObjectFinder`. Designer assigns assets in BP subclasses via the in-engine MCP agent.
- **BP CDO overrides apply after the constructor.** If a property depends on a BP-tunable value at startup, re-apply in `BeginPlay`. (See the sprint-latch bug in `docs/companion_status_2026-04-30.md` §4 for the original incident.)
- **Replicated UPROPERTY** must carry `Replicated` or `ReplicatedUsing=OnRep_*` AND a `DOREPLIFETIME[_CONDITION]` entry in `GetLifetimeReplicatedProps`.
- **Public/Private subfolders** must be added to `Extraction.Build.cs` include arrays (the project uses explicit subfolder paths, not wildcard).
- **Single-line if blocks** with no braces: `if (condition) return;`.
- **`FTimerHandle`** must be cleared in `EndPlay` or `BeginDestroy`.
- **Composition over inheritance.** Class trees 3+ deep get refactored.
- **`TObjectPtr<>`** for owning UObject references; `TWeakObjectPtr<>` for non-owning.
- **Reserve TArrays** when size is known.
- **No magic numbers.** All tunables on the relevant DataAsset.

Commit convention: short imperative title + bullet list of what changed (match the recent commit style on the branch).

---

## Per-phase task structure

Each phase has six recurring sections:

1. **Goal** — one sentence on what this phase delivers
2. **Dependencies** — what must be in place first
3. **C++ work** — file-by-file task list with code intent (not full code; the implementing agent writes the code per project conventions)
4. **Editor work** — numbered list of in-editor steps for the user / in-engine agent
5. **Verification** — manual QA scenarios + build check
6. **Commits** — suggested commit boundaries

---

## Phase 1: Posture-aware behaviour + player switching

**Goal:** finish the mode system. Apply `ECompanionPosture` consistently across fire / audio / cover stickiness / movement, and add the player-facing input that switches it.

**Dependencies:** none. The enum, BB key, replication, and tuning profile already exist.

**Estimated duration:** 4 days.

### C++ work

#### Task 1.1 — Posture-aware fire policy in `BTTask_CompanionCombat`

File: `Extraction/Source/Extraction/Private/AI/Tasks/BTTask_CompanionCombat.cpp`

- Read `BB_Posture` at the top of `TickTask`.
- In **Stealth**, gate all fire on a new BB key `BB_TargetMarked` (Object/Actor). If `BB_TargetMarked != EnemyTarget`, `StopWeaponFire` and skip the peek-fire sub-tree. The existing peek selection logic is wrapped in `if (Posture != Stealth || TargetMarked == EnemyTarget) { ... }`.
- In **Exploration**, if `EnemyTarget` becomes valid, set `BB_Posture = Combat` (auto-flip).
- Suppress all peek montages in Stealth. Companion fires from cover with the existing stand-up-fire model but no peek anim.

#### Task 1.2 — Posture-aware movement in `BTTask_FollowPlayer`

File: `Extraction/Source/Extraction/Private/AI/Tasks/BTTask_FollowPlayer.cpp`

- Already reads tuning values from `FCompanionPostureProfile` via posture. Verify the profile selection logic: `Tuning->PostureProfiles.Find(Posture)`. Add a fallback to Exploration profile if the posture key is missing.
- In **Stealth**, force `SetSprinting(false)` regardless of distance. Override `SprintDistanceThreshold` to infinity for Stealth (or short-circuit the sprint check).
- In **Combat**, prefer the cover branch over formation when `BB_CombatTarget` is valid.

#### Task 1.3 — Cover stickiness in Stealth

File: `Extraction/Source/Extraction/Private/AI/Tasks/BTTask_CompanionCombat.cpp`

- The existing opportunistic-reposition logic (cover-still-good check) runs regardless of posture. In Stealth, disable reposition when the companion has not been detected (no recent damage, no marked target firing yet).
- New tunable on `UCompanionTuningDataAsset`: `bool bStealthLockCurrentSlot = true`.

#### Task 1.4 — Posture-driven enemy visual perception

File: `Extraction/Source/Extraction/Private/AI/CompanionAIController.cpp`

- Companion's sight perception still works as normal for *the companion's own* sight (it sees enemies).
- The reverse — enemies seeing the companion — is handled by extending enemy perception, not companion. Note for Phase 6 / future: enemy `UAIPerceptionComponent` sight stimulus needs a posture-aware filter that returns "no visual stimulus" when target is companion + companion's posture is Stealth + no recent sound stimulus on companion.
- This task in Phase 1 just **adds the helper**: `static bool ACompanionAIController::IsVisuallyDetectable(const AActor* Target)` returning `false` if Target is a companion in Stealth and has not made noise recently. Enemy AI integration is a parking-lot item for a later phase or noted as out-of-scope per "useful, not OP."

#### Task 1.5 — Player input: posture toggle hotkey

Files:
- `Extraction/Source/Extraction/Public/Player/ExtractionPlayerController.h` (or wherever player input is currently routed)
- `Extraction/Source/Extraction/Private/Player/ExtractionPlayerController.cpp`

- Add three new `UInputAction` UPROPERTY references (DataAsset slots): `SetCombatModeAction`, `SetStealthModeAction`, `SetExploreModeAction`.
- On triggered, call `ACompanionAIController::SetPosture(NewPosture)` on the player's companion (the companion is found via a per-player `TWeakObjectPtr<ACompanionCharacter> Companion` cached on the controller).
- A new method `ACompanionAIController::SetPosture(ECompanionPosture)` writes to the BB and updates the pawn's posture (or the pawn's `SetPosture` writes the BB itself — pick one path; recommend: pawn owns the property + replication, AIController is a passthrough that writes the BB on `OnRep_Posture`).

#### Task 1.6 — Mode HUD indicator widget

File: `Extraction/Source/Extraction/Public/UI/CompanionModeWidget.h` (new) + `.cpp` (new)

- Small UMG-bound widget showing the current posture as an icon (Combat / Stealth / Explore).
- Binds to `ACompanionCharacter::OnPostureChanged` delegate.
- Icon assets supplied by the editor side (placeholder text "C" / "S" / "E" in C++; designer swaps to icon images in the BP child class).

### Editor work

1. Add input mapping context entries for the three posture keys. Default to `1`, `2`, `3`.
2. Create input action assets `IA_SetCombatMode`, `IA_SetStealthMode`, `IA_SetExploreMode` in `Content/Core/Input/`.
3. Wire the new IAs into the player's input mapping context.
4. Add the three IA references to the `BP_ExtractionPlayerController` defaults.
5. Open `DA_CompanionTuning`. In `PostureProfiles`, verify all three keys (Exploration, Combat, Stealth) have profiles. Set Stealth's `MaxFollowSpeedMultiplier` to `0.5` (walk only). Confirm.
6. Create `WBP_CompanionMode` UMG widget child of the new `UCompanionModeWidget`. Place placeholder text or icon assets.
7. Add `WBP_CompanionMode` to the player HUD widget in a corner.
8. Add the new BB key `BB_TargetMarked` (Object, base class `AActor`) to `BB_Companion`.

### Verification

Build the editor target and confirm exit 0:

```
"/c/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" ExtractionEditor Win64 Development -Project="C:/Users/matth/Documents/Github/ProjectExtract/Extraction/Extraction.uproject" -WaitMutex
```

Manual QA scenarios:

| Scenario | Expected |
| --- | --- |
| Walk near enemy, press 1 (Combat) | Companion engages, holds cover, fires audibly |
| Press 2 (Stealth) with no marked target | Companion goes silent. Walks only. Does not fire on visible enemies. |
| Press 2 (Stealth), mark target (placeholder: set `BB_TargetMarked` via console), then fire | Companion fires on the marked target only |
| Press 3 (Explore) in safe area | Companion stays near player at walking pace |
| Walk into enemy with posture = Exploration | Posture auto-flips to Combat |
| HUD mode indicator | Updates immediately on each posture flip |

### Commits

- `Companion: posture-aware fire and audio in combat task`
- `Companion: stealth posture locks cover slot, disables sprint`
- `Companion: player input for posture toggle (1/2/3)`
- `Companion: mode HUD indicator widget`

---

## Phase 2: Companion DBNO + player-side revive

**Goal:** mutual revive loop. Companion can be downed (not killed), player can walk up and revive it. Mission only fails on team wipe.

**Dependencies:** none.

**Estimated duration:** 5 days.

### C++ work

#### Task 2.1 — Companion DBNO state in HealthComponent

File: `Extraction/Source/Extraction/Private/Components/HealthComponent.cpp` (the existing player health component is reused — confirm the file path; if companion has its own, refactor to share).

- Add a new state to the existing state machine: `EHealthState::Downed`. The component already handles Alive / Dead for the player.
- On HP reaching zero for a companion-owned health component:
  - Set state to `Downed`
  - Start a bleed timer (tunable: `DownedBleedDuration`, default 30s — longer than the player's bleedout)
  - Broadcast `OnDowned` delegate
  - Disable damage during the first 1s grace (`DownedDamageGracePeriod` tunable)
- On bleed timer expiry: transition to `Dead`, fire `OnDeath`.
- On `Revive(AActor* Reviver)` call: cancel bleed, restore to 30% HP, transition to `Alive`, fire `OnRevived`.

#### Task 2.2 — Companion-side downed reaction

File: `Extraction/Source/Extraction/Private/Companion/CompanionCharacter.cpp`

- Bind to `HealthComponent->OnDowned` and `OnRevived` (in `PostInitializeComponents`).
- On `OnDowned`:
  - `StopWeaponFire`
  - `SetAimTarget(nullptr)`
  - Disable capsule collision against pawns (keep world collision so companion stays on ground)
  - Play downed montage (asset assigned in BP)
  - Set new BB key `BB_IsDowned = true` via `AIController->GetBlackboardComponent()->SetValueAsBool(...)`
- On `OnRevived`:
  - Re-enable collision
  - Clear `BB_IsDowned`
  - Restore movement (the BT will resume normal branches)
- Remove the current `HandleDeath` → `DestroyAfterDeath` path for the normal case. Only fire it if `OnDeath` is reached (bleed-out).

#### Task 2.3 — BT top-level decorator: `BB_IsDowned`

File: editor wiring on `BT_Companion`. The C++ side: add the new BB key.

- Add `BB_IsDowned` (bool) to `BB_Companion`.
- Editor work below wires a top-priority "Downed" branch that aborts everything else.

#### Task 2.4 — Player-side revive interaction

Files:
- `Extraction/Source/Extraction/Public/Player/ExtractionCharacter.h` — add `bool CanReviveCompanion(const ACompanionCharacter* InCompanion) const`
- `Extraction/Source/Extraction/Private/Player/ExtractionCharacter.cpp` — implement
- New BTTask is **not** needed (this is a player action, not an AI behaviour)

Player-side flow:
- Player presses interact key (`E` or similar) while looking at a downed companion within `RevivePromptRadius` (~150uu).
- A revive prompt UMG widget appears, player holds the key for `RevivePlayerHoldDuration` (~3s).
- On completion: call `Companion->GetHealthComponent()->Revive(Player)`.
- Hold can be interrupted by movement, weapon fire, or taking damage.

#### Task 2.5 — Revive prompt widget

File: `Extraction/Source/Extraction/Public/UI/RevivePromptWidget.h` + `.cpp`

- UMG widget showing "Hold E to revive [CompanionName]"
- Bound to the player's interaction system (existing or new — check if player has an interaction subsystem; if not, create a minimal `UPlayerInteractionComponent`).
- Progress bar fills as the key is held.

#### Task 2.6 — Mission failure on team wipe

File: `Extraction/Source/Extraction/Public/GameMode/ExtractionGameMode.h` + `.cpp`

- GameMode listens for both player-DBNO and companion-DBNO events.
- Tracks `bool bPlayerDowned` and `bool bCompanionDowned`.
- When both are true simultaneously, fire `OnTeamWipe` → trigger checkpoint reload (or call existing `RestartLevel` / level transition logic).

### Editor work

1. Add `BB_IsDowned` (bool) to `BB_Companion`.
2. Add a new top-level branch on `BT_Companion`, leftmost (highest priority), called "Downed Self". Decorator: Blackboard, `BB_IsDowned Is Set`, Abort: Both. Single task underneath: `BTTask_WaitUntilNotDowned` (which is just a Wait-style task that succeeds when the decorator fires). Or simpler: a single "Idle" task that runs indefinitely; the decorator's "Abort Lower Priority" handles cancelling combat / follow.
3. Create or import the downed animation montage. Assign to `BP_Companion` defaults (new UPROPERTY: `UAnimMontage* DownedMontage`).
4. Create `WBP_RevivePrompt` UMG widget. Display "Hold E to revive [Name]" with a progress bar.
5. Wire the player's interact key to the player-side revive code path.

### Verification

Build the editor target. Then in PIE:

| Scenario | Expected |
| --- | --- |
| Take companion to 0 HP via enemy fire | Companion enters downed state. Plays downed montage. Stops firing. Not destroyed. |
| Walk up to downed companion, hold E for 3s | Companion stands up, restored to 30% HP, resumes BT. |
| Move away from companion mid-revive | Hold cancels. Companion stays downed. |
| Take damage mid-revive | Hold cancels. |
| Leave companion downed for 30s | Companion dies (transitions to Dead). |
| Down both player and companion at once | GameMode fires team wipe. Checkpoint reload. |
| Player downs first, companion-revive task triggers | Companion revives player (existing behaviour). |

### Commits

- `HealthComponent: add Downed state with bleed timer`
- `Companion: bind downed/revived events, play downed montage`
- `Companion: BT top-level downed branch + BB_IsDowned key`
- `Player: revive-companion interaction (hold-E within radius)`
- `Player: revive prompt UMG widget`
- `GameMode: mission fail on team wipe`

---

## Phase 3: Command surface foundation

**Goal:** ping system + core commands wired through the blackboard and a dedicated BT command branch.

**Dependencies:** Phase 1 (`BB_TargetMarked` exists). Phase 2 not strictly required but landing it first means the BT command branch can already coexist with the downed branch.

**Estimated duration:** 6 days.

### C++ work

#### Task 3.1 — Command enum + BB keys

Files:
- `Extraction/Source/Extraction/Public/Companion/CompanionCommandTypes.h` (new)

```text
UENUM(BlueprintType)
enum class ECommandedAction : uint8
{
    None,
    MarkTarget,
    MarkLoot,
    MoveToPoint,
    HoldPosition,
    Regroup,
    FocusFire,
    Suppress,
    UseAbility,
    Breach,
    MedRequest,
    ReviveRequest,
};
```

- Add BB keys: `BB_CommandedAction` (Enum: `ECommandedAction`), `BB_CommandTargetActor` (Object: AActor), `BB_CommandTargetLocation` (Vector).
- Add new static FName declarations on `ACompanionAIController` for these.

#### Task 3.2 — Player ping system

Files:
- `Extraction/Source/Extraction/Public/Player/CompanionCommandComponent.h` (new)
- `Extraction/Source/Extraction/Private/Player/CompanionCommandComponent.cpp` (new)

- Component on the player controller (or player pawn — pick the controller for cleaner authority).
- Method `void IssuePing()`:
  - Perform a camera trace (~5000uu) from the player's camera.
  - Classify the hit:
    - If hit actor is enemy → resolve to MarkTarget or FocusFire based on companion's posture
    - If hit actor implements `IInteractableLoot` (new interface, marker tag) → MarkLoot
    - If hit actor is a door (existing door class or implements `IInteractableDoor`) → Breach
    - If hit ground / no actor → MoveToPoint at hit location
  - Write to companion's BB via `ACompanionAIController::IssueCommand(ECommandedAction, AActor* Target, FVector Location)`.

#### Task 3.3 — Command issue/clear API on AIController

File: `Extraction/Source/Extraction/Public/AI/CompanionAIController.h` + `.cpp`

```text
UFUNCTION(BlueprintCallable)
void IssueCommand(ECommandedAction Action, AActor* TargetActor, FVector TargetLocation);

UFUNCTION(BlueprintCallable)
void ClearActiveCommand();
```

- Writes the three BB keys.
- Validates the command (e.g. MarkTarget requires `TargetActor != nullptr`).
- Logs via `LogCompanionAI`.

#### Task 3.4 — BT command branch (top-level priority)

Editor work — see below. C++ side: each command needs its own `BTTask`.

Tasks to create:

| File | Purpose |
| --- | --- |
| `Public/AI/Tasks/BTTask_MarkTarget.h/cpp` | Holds `BB_TargetMarked = BB_CommandTargetActor`. Returns Succeeded immediately (state-set task). |
| `Public/AI/Tasks/BTTask_MarkLoot.h/cpp` | Companion paths to the target, plays a "looting" animation, picks up any IInteractableLoot items, returns and transfers. |
| `Public/AI/Tasks/BTTask_MoveToCommandPoint.h/cpp` | Companion moves to `BB_CommandTargetLocation`. Succeeds on arrival. |
| `Public/AI/Tasks/BTTask_HoldPosition.h/cpp` | Companion holds at current location. Succeeds when `BB_CommandedAction != HoldPosition`. Persistent task. |
| `Public/AI/Tasks/BTTask_Regroup.h/cpp` | Companion paths to player. Succeeds when within formation distance. |
| `Public/AI/Tasks/BTTask_FocusFire.h/cpp` | Sets `BB_CombatTarget = BB_CommandTargetActor`, falls through to combat sub-tree. |
| `Public/AI/Tasks/BTTask_Breach.h/cpp` | Companion paths to door, plays breach animation, opens door, returns. |

Each task ends by clearing `BB_CommandedAction` (one-shot commands) or persists if it's HoldPosition (persistent until player issues a different command).

#### Task 3.5 — Player input bindings for ping

File: `Extraction/Source/Extraction/Private/Player/ExtractionPlayerController.cpp`

- New input action `IA_CompanionPing`.
- On triggered: call `CompanionCommandComponent->IssuePing()`.

#### Task 3.6 — `IInteractableLoot` interface + a basic LootActor class

Files:
- `Extraction/Source/Extraction/Public/Interaction/InteractableLoot.h` (new interface)
- `Extraction/Source/Extraction/Public/World/LootActor.h` + `.cpp` (new actor)

- Interface declares `GetLootContents()` returning a struct (ammo type + count, item refs).
- LootActor implements it. Holds a `FLootInventory` UPROPERTY with the contents.
- Used by `BTTask_MarkLoot` to determine what to transfer to the player.

### Editor work

1. Add the three new BB keys to `BB_Companion`: `BB_CommandedAction` (enum: ECommandedAction), `BB_CommandTargetActor` (Object: AActor), `BB_CommandTargetLocation` (Vector).
2. On `BT_Companion`, insert a new Selector branch between the Mirror Traversal branch and the Combat branch, called "Execute Command". Decorator: Blackboard, `BB_CommandedAction != None`, Abort: Both.
3. Under "Execute Command", add a Selector. Each child is a sub-tree gated by a `BTDecorator_BlackboardEnum` checking `BB_CommandedAction == [specific value]`.
4. Wire each new BT task class into the matching child.
5. Add `IA_CompanionPing` input action asset. Default bind: `Q` (held disables-wheel in Phase 4; tap = ping).
6. Add input mapping context entry for `IA_CompanionPing`.
7. Create `BP_LootActor` placeholder. Place 2-3 instances in the test level for verification.

### Verification

Build, then PIE scenarios:

| Scenario | Expected |
| --- | --- |
| Aim at enemy in Combat mode, tap Q | Companion focuses fire on that enemy |
| Aim at enemy in Stealth mode, tap Q | Companion marks target. Will fire on next player shot. |
| Aim at LootActor, tap Q | Companion paths to it, picks up, returns, transfers ammo to player |
| Aim at ground 30m away, tap Q | Companion moves to that point and holds there |
| At a point, tap Q | Companion holds. Player walks away — companion stays. |
| Player flips to Combat mode (1) while companion is Holding | Hold command persists. Posture changed but command still active. |
| Player issues a new command | Previous command cleared. |
| Companion goes DBNO mid-command | Command branch aborts, downed branch runs. |

### Commits

- `Companion: ECommandedAction enum + BB keys`
- `Player: CompanionCommandComponent with ping system`
- `Companion: BT command branch and core command tasks (mark/move/hold/regroup)`
- `Companion: focus fire and mark loot command tasks`
- `Companion: breach command + IInteractableDoor interface`
- `World: LootActor + IInteractableLoot interface`

---

## Phase 4: Command surface extension (wheel + remaining commands)

**Goal:** the long-tail commands that need a menu pick rather than a context trace.

**Dependencies:** Phase 3.

**Estimated duration:** 5 days.

### C++ work

#### Task 4.1 — Command wheel widget

Files:
- `Extraction/Source/Extraction/Public/UI/CommandWheelWidget.h` + `.cpp` (new)

- UMG widget shown while the player holds the wheel key.
- Radial layout with 6 slots: Suppress, Use Ability, Med Request, Revive Request, Breach (manual variant), Hold Position.
- Mouse position determines selected slot.
- Release key: issues the command.

#### Task 4.2 — Wheel input integration

File: `Extraction/Source/Extraction/Private/Player/ExtractionPlayerController.cpp`

- `IA_CompanionPing` triggered (tap) = ping.
- `IA_CompanionPing` held (>0.2s) = show wheel; release issues selected command.

#### Task 4.3 — Suppress task

Files:
- `Extraction/Source/Extraction/Public/AI/Tasks/BTTask_Suppress.h/cpp`

- Companion fires sustained bursts at `BB_CommandTargetLocation` (not at an actor).
- Holds current cover. Aim is toward the target location.
- Succeeds after `SuppressDuration` (tunable, default 5s) or when ammo runs low.

#### Task 4.4 — Use Ability task

Files:
- `Extraction/Source/Extraction/Public/AI/Tasks/BTTask_UseAbility.h/cpp`
- `Extraction/Source/Extraction/Public/Companion/CompanionAbilityTypes.h` (new)

- Enum: `EAbilityType` (Frag, Flash, Smoke).
- Companion throws the selected ability toward `BB_CommandTargetLocation`.
- Plays throw montage, instantiates the relevant projectile actor (assets supplied by editor; class references on `CompanionCharacter`).
- One-shot, cleared on completion.

#### Task 4.5 — Med Request handling

- Player command sets `BB_CommandedAction = MedRequest, BB_CommandTargetActor = Player`.
- Companion paths to player, plays a "share med" animation, restores player HP by a fixed amount (`MedShareAmount` tunable, default 30 HP).
- Cooldown on the companion side: `MedRequestCooldown` (default 60s).

File: `Extraction/Source/Extraction/Public/AI/Tasks/BTTask_ShareMed.h/cpp`

#### Task 4.6 — Revive Request (explicit player ask)

- Same as the player-DBNO auto-revive but triggered by command rather than by player downed state.
- Useful when the player is *about to go down* and wants the companion close.
- Implementation: re-uses the existing `BTTask_RevivePlayer` but with the BB precondition `BB_CommandedAction == ReviveRequest`.

#### Task 4.7 — Loot inventory pickup logic

File: `Extraction/Source/Extraction/Private/AI/Tasks/BTTask_MarkLoot.cpp` (extend)

- On arrival at loot:
  - Read `FLootInventory` from the loot actor
  - For each ammo entry matching the player's current weapon's caliber, add to a "pending transfer" buffer
  - Play looting animation
  - Path back to player
  - On reaching player, call player's `ReceiveLootTransfer(FLootInventory)`.
- Player side: `void AExtractionCharacter::ReceiveLootTransfer(const FLootInventory&)` adds ammo to the player's reserve.

### Editor work

1. Create `WBP_CommandWheel` UMG widget child of `UCommandWheelWidget`. Six radial slots, icon + label per slot.
2. Add icon assets for: Suppress, Frag, Flash, Smoke, Med, Revive Request, Breach, Hold Position. From marketplace icon pack.
3. Add throw montages for the companion (`AM_CompanionThrowFrag`, etc.) from marketplace anim pack. Assign references on `BP_Companion`.
4. Add grenade / flash / smoke actor classes (from marketplace or existing player set, reused).
5. Add the "share med" animation montage. Assign reference.
6. Wire the wheel show / hide to the `IA_CompanionPing` hold logic.

### Verification

| Scenario | Expected |
| --- | --- |
| Hold Q, select Suppress, aim at enemy position, release | Companion fires sustained bursts at the position for ~5s |
| Hold Q, select Frag, aim at room | Companion throws frag toward aim point |
| Hold Q, select Med Request | Companion comes to player, plays med animation, player HP restored |
| Tap Med Request again within 60s | No-op (cooldown) |
| Hold Q in midfire — player can still aim | Wheel UI shows but firing input remains responsive |
| Cancel wheel by moving cursor outside | No command issued |

### Commits

- `UI: CommandWheelWidget with radial selection`
- `Player: wheel input (tap = ping, hold = wheel)`
- `Companion: suppress task with sustained-fire toward location`
- `Companion: ability throw task (frag/flash/smoke)`
- `Companion: share med task + cooldown`
- `Companion: loot transfer flow (mark loot → fetch → deliver)`

---

## Phase 5: VIP missions

**Goal:** the shepherd companion role. A VIP AI that follows the companion, a BT branch that handles escort, mission failure when both team members are down.

**Dependencies:** Phase 2 (DBNO), Phase 3 (commands — for the player to swap who the VIP follows).

**Estimated duration:** 5 days.

### C++ work

#### Task 5.1 — VIP AI character

Files:
- `Extraction/Source/Extraction/Public/AI/VIPCharacter.h` (new)
- `Extraction/Source/Extraction/Private/AI/VIPCharacter.cpp` (new)
- `Extraction/Source/Extraction/Public/AI/VIPAIController.h` + `.cpp` (new)

- Simple follow-the-leader character. No combat. No detection logic.
- `SetFollowLeader(AActor* Leader)` — switches which actor the VIP follows.
- `EnterCowerState()` — stops movement, plays cower idle, ignores leader updates.
- `ExitCowerState()` — resumes following the current leader.
- Hostiles never select the VIP as a target (achieved via team tag or perception filter).

#### Task 5.2 — VIP team/perception integration

File: `Extraction/Source/Extraction/Private/AI/VIPCharacter.cpp` + `Public`

- VIP carries a gameplay tag `Team.VIP`.
- Enemy perception explicitly filters out actors tagged `Team.VIP` from sight/hearing stimuli (add this filter in the enemy controller's perception update).
- Cleaner: enemy AI's threat-priority scoring (which is already a TODO from the GDD) excludes VIPs by tag.

#### Task 5.3 — Escort branch on companion BT

C++ side: a new BB key + a new BT task.

- BB key: `BB_EscortingVIP` (bool), `BB_VIPActor` (Object: AActor).
- New task: `Extraction/Source/Extraction/Public/AI/Tasks/BTTask_EscortVIP.h/cpp`
  - Holds companion within `EscortRadius` (tunable, default 600uu) of `BB_VIPActor`.
  - When `BB_CombatTarget` is valid, the companion engages but only chooses cover positions within `EscortRadius` of the VIP (override the cover EQS query to add a distance-to-VIP weight).
  - The task succeeds when `BB_EscortingVIP` is cleared (mission complete).

#### Task 5.4 — Player command to swap VIP leader

- Add new command to the enum: `ECommandedAction::SetVIPLeader`. Or reuse `HoldPosition` semantics — when the player issues "Stay With Me" while a VIP is in play, the VIP's `FollowLeader` swaps to the player.
- Simpler implementation: the wheel gets a new entry "VIP Stay With Me" / "VIP Go With Companion" that appears only when a VIP is present in the mission. Calls `VIPCharacter->SetFollowLeader(Player or Companion)`.

#### Task 5.5 — Companion-downed → VIP cower

File: `Extraction/Source/Extraction/Private/AI/VIPCharacter.cpp`

- Bind to companion's `OnDowned` and `OnRevived` (via GameState lookup or direct bind on spawn).
- On companion downed: `EnterCowerState()`.
- On companion revived: `ExitCowerState()`.

#### Task 5.6 — Mission setup

File: `Extraction/Source/Extraction/Public/GameMode/ExtractionGameMode.h/cpp` (extend Phase 2's team-wipe addition)

- New `EMissionType` enum on the GameMode: `VIPRescue`, `IntelGrab`.
- Per-mission setup: VIP missions spawn a VIPCharacter, set `BB_EscortingVIP = true` on the companion, set `BB_VIPActor` to the VIP.
- Mission complete when VIP reaches the extraction zone.

### Editor work

1. Create `BP_VIPCharacter` from `AVIPCharacter`. Assign a skeletal mesh + idle/walk/cower animations from marketplace.
2. Create `BP_VIPAIController` from `AVIPAIController`. Assign to `BP_VIPCharacter`.
3. Add gameplay tag `Team.VIP` in the GameplayTagsList.
4. Add the new BB keys `BB_EscortingVIP` and `BB_VIPActor` to `BB_Companion`.
5. On `BT_Companion`, add the new "Escort VIP" branch between "Execute Command" and "Combat". Decorator: `BB_EscortingVIP Is Set`. Wire `BTTask_EscortVIP`.
6. Add VIP-related wheel entries (visible only when `BB_EscortingVIP` is true; or context-suppress them in the wheel widget).
7. Create a test level (or extend the existing test level) with a VIP spawn point + extraction zone. Wire mission start to spawn the VIP and set `BB_EscortingVIP`.

### Verification

| Scenario | Expected |
| --- | --- |
| Spawn into VIP mission | VIP appears, follows companion |
| Enemy fires at VIP | VIP cannot be damaged. No reaction from the enemy AI (VIP is filtered from threat list) |
| Companion engages enemy in cover | VIP follows along and crouches near the companion's cover |
| Player issues "VIP Stay With Me" | VIP swaps to follow the player. Companion continues normally. |
| Companion goes DBNO | VIP cowers in place. Plays scared idle. |
| Player revives companion | VIP resumes following. |
| Player and companion both DBNO | Mission fails. Checkpoint reload. |
| VIP reaches extraction zone | Mission complete. |

### Commits

- `AI: VIPCharacter + VIPAIController (follow-leader, cower)`
- `AI: VIP team tag and enemy perception filter`
- `Companion: BT escort branch + BTTask_EscortVIP`
- `Player: VIP leader swap command via wheel`
- `AI: companion-downed triggers VIP cower`
- `GameMode: VIPRescue mission type + setup`

---

## Phase 6: Auto-suggest + HUD layer

**Goal:** the auto-suggest prompt + the marker / indicator HUD elements that have accumulated through earlier phases.

**Dependencies:** Phase 1 (mode), Phase 3 (commands — for command-state indicator).

**Estimated duration:** 4 days.

### C++ work

#### Task 6.1 — `SuggestedMode` BB key + auto-suggest service

Files:
- BB key: `BB_SuggestedMode` (enum: ECompanionPosture)
- `Extraction/Source/Extraction/Public/Player/PostureSuggestionComponent.h` + `.cpp` (new, on the player controller)

- Component ticks every 0.5s.
- Watches gameplay state: weapon silenced flag, player crouched flag, enemies aware flag (from any enemy AI), enemies-in-perception count.
- Applies rules per the GDD table:

| Trigger | Suggested |
| --- | --- |
| Player fires unsilenced weapon while enemies aware | Combat |
| Player crouched + suppressor + no enemies alerted | Stealth |
| No enemies in perception for 30s | Explore |

- Writes the suggested mode to the BB via `AIController->GetBlackboardComponent()->SetValueAsEnum(BB_SuggestedMode, ...)`.
- Includes a debounce: never suggest the same mode twice in a row, never re-suggest within 10s of the player accepting/ignoring.

#### Task 6.2 — Suggestion toast UMG widget

Files:
- `Extraction/Source/Extraction/Public/UI/ModeSuggestionToastWidget.h` + `.cpp`

- Pops up when `BB_SuggestedMode` is set and differs from current mode.
- Shows "[Mode] suggested — Tab to accept" with a 5s timeout.
- On Tab: calls `AIController->SetPosture(SuggestedMode)`. On timeout: clears `BB_SuggestedMode`.

#### Task 6.3 — Enemy-spotted marker system

Files:
- `Extraction/Source/Extraction/Public/AI/CompanionPerceptionEvents.h` (new)
- `Extraction/Source/Extraction/Public/UI/EnemyMarkerWidget.h` + `.cpp`

- Companion's `AIPerceptionComponent` already fires `OnTargetPerceptionUpdated` events.
- On a new perception "Sensed" event for an enemy: broadcast `OnEnemySpotted(AActor* Target)` via the AIController.
- HUD listens, instantiates a world-space `EnemyMarkerWidget` pinned to the target for 5 seconds. Re-spotting refreshes the timer.

#### Task 6.4 — Command-state indicator widget

File: `Extraction/Source/Extraction/Public/UI/CommandStateWidget.h` + `.cpp`

- Binds to `BB_CommandedAction` (via the AIController exposing a delegate that fires on BB changes).
- Shows the current command as text: "Holding", "Marking [target name]", "Reviving", "Looting", "Idle".

#### Task 6.5 — Low-ammo warning

- The companion already has `GetCurrentAmmo` and reload logic.
- Add a delegate `FOnLowAmmoChanged` on `AWeaponBase` that fires when ammo drops below `LowAmmoThreshold` (default 1/3 mag).
- HUD widget binds, shows a low-ammo icon for the companion.

### Editor work

1. Add `BB_SuggestedMode` enum key to `BB_Companion`.
2. Create `WBP_ModeSuggestionToast` UMG widget.
3. Create `WBP_EnemyMarker` UMG widget (icon + small label). World-space variant.
4. Create `WBP_CommandStateIndicator` UMG widget. Add to the player HUD.
5. Create `WBP_CompanionLowAmmoIcon` UMG widget. Add to the player HUD near the companion HP bar.
6. Marketplace asset: marker icons, mode icons (Combat / Stealth / Explore), command icons.

### Verification

| Scenario | Expected |
| --- | --- |
| Fire unsilenced rifle near enemy (any mode != Combat) | "Combat suggested" toast appears within 1s |
| Press Tab during toast | Posture flips to Combat |
| Ignore toast | Disappears after 5s, no mode change |
| Crouch with suppressor in safe area | "Stealth suggested" toast appears |
| Companion sees a new enemy | Marker appears on enemy, lasts 5s |
| Issue Hold command | Command-state indicator reads "Holding" |
| Companion magazine drops to 1/3 | Low-ammo icon flashes |

### Commits

- `Player: PostureSuggestionComponent with debounced auto-suggest`
- `UI: ModeSuggestionToast widget`
- `Companion: enemy-spotted marker pipeline`
- `UI: CommandStateIndicator widget`
- `Weapon: low-ammo delegate + HUD icon`

---

## Phase 7: Bark system + first character DataAsset

**Goal:** the voice layer. Mode-filtered audio events with per-character voice banks. First character ships with a complete bark set.

**Dependencies:** Phase 1 (posture), Phase 6 (the events the barks subscribe to are already firing).

**Estimated duration:** 4 days.

### C++ work

#### Task 7.1 — `UBarkComponent`

Files:
- `Extraction/Source/Extraction/Public/Companion/BarkComponent.h` + `.cpp` (new)

- ActorComponent attached to the companion.
- Subscribes to gameplay events:
  - `Perception->OnEnemySpotted` → threat-alert bucket
  - `Weapon->OnReloadStart` → self-state bucket
  - `Health->OnDowned`, `OnHit` → self-state bucket
  - `AIController->OnCommandReceived` → command-response bucket
  - `GameMode->OnObjectiveUpdated` → mission-state bucket
- On event: look up the line set from the active character DataAsset, filter by current posture, pick a random line, play via the companion's `UAudioComponent` (attached at the mouth socket).

#### Task 7.2 — Character DataAsset

Files:
- `Extraction/Source/Extraction/Public/Companion/CompanionCharacterDataAsset.h` + `.cpp`

```text
USTRUCT(BlueprintType)
struct FBarkBucket
{
    UPROPERTY(EditAnywhere)
    TArray<TObjectPtr<USoundBase>> CombatLines;

    UPROPERTY(EditAnywhere)
    TArray<TObjectPtr<USoundBase>> StealthWhisperLines;

    UPROPERTY(EditAnywhere)
    TArray<TObjectPtr<USoundBase>> ExploreLines;
};

UCLASS()
class UCompanionCharacterDataAsset : public UDataAsset
{
    UPROPERTY(EditAnywhere, Category="Identity")
    FName DisplayName;

    UPROPERTY(EditAnywhere, Category="Identity")
    TSoftObjectPtr<USkeletalMesh> Mesh;

    UPROPERTY(EditAnywhere, Category="Identity")
    TSoftClassPtr<UAnimInstance> AnimBP;

    UPROPERTY(EditAnywhere, Category="Voice")
    FBarkBucket ThreatAlert;

    UPROPERTY(EditAnywhere, Category="Voice")
    FBarkBucket SelfState;

    UPROPERTY(EditAnywhere, Category="Voice")
    FBarkBucket MissionState;

    UPROPERTY(EditAnywhere, Category="Voice")
    FBarkBucket CommandResponse;
};
```

- The `BarkComponent` reads from the assigned `UCompanionCharacterDataAsset`.

#### Task 7.3 — Wire character DataAsset into companion spawn

File: `Extraction/Source/Extraction/Private/Companion/CompanionCharacter.cpp`

- Add `UPROPERTY(EditAnywhere) TObjectPtr<UCompanionCharacterDataAsset> CharacterData;`
- On BeginPlay: assign mesh, anim BP, and route bark playback to `BarkComponent` with this data.

### Editor work

1. Acquire one full character VO pack from the marketplace (full bark set with combat/stealth/whisper variants).
2. Create `DA_Companion_[CharacterName]` instance of `UCompanionCharacterDataAsset`. Populate all four buckets with the marketplace lines, ~30-50 lines total.
3. Create `BP_Companion_[CharacterName]` child of `BP_Companion`. Set `CharacterData = DA_Companion_[CharacterName]`.
4. Wire the first mission to spawn `BP_Companion_[CharacterName]` instead of the base `BP_Companion`.

### Verification

| Scenario | Expected |
| --- | --- |
| Companion sees an enemy in Combat mode | Plays a random "Contact!"-bucket line, full volume |
| Same in Stealth mode | Plays a whisper variant (if no whisper variant exists, no audio plays) |
| Companion reloads in Combat | Plays "Reloading!" |
| Companion is hit | Plays a "Hit!" line |
| Companion enters DBNO | Plays an "I'm down!" line |
| Player issues a command | Plays a command-response line ("On it", "Moving up", etc.) |
| Two threat events within 2s | Only one line plays (debounce) |

### Commits

- `Companion: UBarkComponent with event-driven mode-filtered playback`
- `Companion: CompanionCharacterDataAsset with bark bucket struct`
- `Companion: wire CharacterData into pawn spawn and bark system`

---

## Phase 8: Tutorial integration

**Goal:** in-fiction onboarding via the companion's first-mission dialogue. No separate tutorial level, no pause overlays.

**Dependencies:** Phases 1-7. Everything the tutorial teaches must exist.

**Estimated duration:** 3 days.

### C++ work

#### Task 8.1 — Tutorial trigger system

Files:
- `Extraction/Source/Extraction/Public/Tutorial/TutorialTriggerComponent.h` + `.cpp` (new)

- Component on the player.
- Tracks "has this lesson played" via a `TSet<FName>` of triggered lesson IDs.
- Lesson IDs: `Lesson_MarkTarget`, `Lesson_ModeSwitch`, `Lesson_Hold`, `Lesson_Revive`, `Lesson_Loot`, `Lesson_Wheel`.
- Each lesson is a `FTutorialLesson` struct with: ID, audio cue (`USoundBase*`), text (for subtitle), trigger condition lambda.

#### Task 8.2 — Lesson trigger conditions

The conditions register on player events:

| Lesson | Trigger |
| --- | --- |
| Mark Target | First time the player aims at an enemy in any mode |
| Mode Switch | First time the player has been in combat for 30s without switching mode |
| Hold | First time the player approaches a "junction" volume in the test level |
| Revive | First time the companion enters DBNO |
| Loot | First time the player approaches a LootActor |
| Wheel | First time the player's HP drops below 50% |

(Conditions are extensible; the test mission designer can add new lessons in editor.)

#### Task 8.3 — First-mission bark integration

- The first mission's character DataAsset includes the tutorial lines as a "FirstMission" optional bark bucket.
- TutorialTriggerComponent calls the BarkComponent's `PlayLine(Sound, Subtitle)` directly when a lesson fires.

### Editor work

1. Record / acquire the tutorial-line VO (6 lessons, ~15-30 lines). Marketplace or self-recorded.
2. Add tutorial lines to the first character's `DA_Companion_[Name]` in a new "FirstMission" section.
3. Place trigger volumes in the test level for spatially-gated lessons (Hold, Loot).
4. Tag the first mission as the tutorial mission in the GameMode setup.

### Verification

| Scenario | Expected |
| --- | --- |
| Start the first mission, aim at first enemy | Companion says "Tag him with Q and we'll drop them together" |
| Combat for 30s without mode flip | "Switching to stealth, quieten up" (or similar) |
| Approach a corridor junction | "Give me a sec, hold here" |
| Companion enters DBNO for the first time | "I'm hit, get over here" + revive prompt visible |
| Approach a LootActor for the first time | "Mark anything useful and I'll grab it" |
| HP drops below 50% | "Hold Q if you need a med" |
| Replay the first mission | Lessons do not re-fire (already triggered) |

### Commits

- `Tutorial: TutorialTriggerComponent with lesson registry`
- `Tutorial: lesson trigger conditions wired to gameplay events`
- `Tutorial: first-mission bark bucket and integration`

---

## Stretch goals (parking lot)

Not planned in detail. Each can be picked up after the core lands:

| Goal | One-line approach |
| --- | --- |
| Class system (stealth / aggressive / balanced) | Add `UCompanionClassDataAsset` (loadout, mode bias, cover preferences). Route through the existing tuning lookup. |
| Second companion slot | `TeamRoster` array on GameState. Per-companion AIController + BT. Arbitration on command dispatch and revive priority. |
| Walk-up silent knife kill | New BT task gated by `BB_TargetMarked + Posture == Stealth + Distance < MeleeThreshold`. Animation pair. |
| Persistent character recurrence | Save-game flag per character. Reunion bark line in the DataAsset. |
| Banter / downtime lines | New "Downtime" bark bucket. Timer-driven playback in Explore mode when no enemies. |
| Autonomous tactical combat | Tactical decision service in the Combat sub-tree. New sub-trees for flank / push / retreat. |

---

## How to use this document (for future Claude)

When picking up a phase:

1. **Read the Goal and Current State sections** at the top of this document.
2. **Read the relevant GDD section** for the player-facing intent ([gameplay](companion-gdd-gameplay-2026-05-23.md)) and architectural intent ([technical](companion-gdd-technical-2026-05-23.md)).
3. **Read the phase's C++ tasks in order.** Each task lists exact file paths.
4. **Implement task-by-task.** Follow the project's coding conventions (no `/Game/` paths in C++, replicate properly, clear timers, single-line `if`s, no magic numbers).
5. **Dispatch to subagents** when the task fits an existing agent's description (per `CLAUDE.md` Workflow section — `ue5-cpp-implementer` for solo C++ work, `agent-teams:team-spawn` preset `feature` for parallelisable work).
6. **Hand off editor work** to the user / in-engine MCP agent using the `inengine-checklist` skill for each phase's Editor Work section.
7. **Build before declaring done.** Standard command:

```
"/c/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" ExtractionEditor Win64 Development -Project="C:/Users/matth/Documents/Github/ProjectExtract/Extraction/Extraction.uproject" -WaitMutex
```

8. **Run the Verification scenarios in editor.** Manual QA, not automation (automation tests are a future polish task, not required per phase).
9. **Commit per the suggested boundaries.** Match the existing commit-message style on this branch: short imperative title + bullet list of what changed. No `Co-Authored-By: Claude` trailer.
10. **Mark the phase complete and move to the next.**

If a phase deviates significantly from the plan during execution (a system turns out to need different shape than described), update this document with the actual shape. The roadmap is the living source of truth for the companion build.

---

## Related documents

- [`companion-gdd-gameplay-2026-05-23.md`](companion-gdd-gameplay-2026-05-23.md) — player-facing design
- [`companion-gdd-technical-2026-05-23.md`](companion-gdd-technical-2026-05-23.md) — architecture overview
- [`companion_status_2026-04-30.md`](companion_status_2026-04-30.md) — older state snapshot (now partially outdated; this roadmap supersedes the "next steps" section)
- [`companion_movement_plan.md`](companion_movement_plan.md) — earlier three-direction brief
- [`agent_docs/companion_testing.md`](../agent_docs/companion_testing.md) — manual QA scenarios for the existing prototype
- [`agent_docs/companion_traversal_status.md`](../agent_docs/companion_traversal_status.md) — traversal subsystem details
