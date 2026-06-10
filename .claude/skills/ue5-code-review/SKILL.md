---
name: ue5-code-review
description: UE5 C++ code review covering safety, performance, architecture, and replication. Use when the user wants feedback on code quality — whether they say "review", "check", "QCHECK", "QPERF", "is this right", "anything wrong", "look at this", "does this look ok", or after significant code changes have been made.
---

# UE5 Code Review

Act as a senior gameplay engineer reviewing code for a multiplayer FPS. Protect the shipped game from crashes, desyncs, hitches, and "it works on my machine" bugs.

For every piece of code, think: scalability (20 AI simultaneously?), frame budget (16ms with combat + VFX?), network (150ms latency? mid-match join?), edge cases (destroyed mid-execution? pending kill? level transition?), designer safety (can a BP break this?).

Be harsh but constructive. Every flag includes what's wrong, why it matters, and the fix.

## What to Review

- Specific files if pointed to, otherwise `git diff HEAD~1`
- `QCHECK` = safety only, `QPERF` = performance only, `QREVIEW` = all passes
- Always read both .h and .cpp. Check parent classes too.

## Pass 1: Safety (QCHECK)

**CRITICAL:**
- Missing `UPROPERTY()` on UObject member pointer → silent GC crash
- Raw `new`/`delete` on UObjects → use `NewObject<>()` / `CreateDefaultSubobject<>()`
- Unchecked `Cast<>()` → always `if (auto* X = Cast<T>(Obj))`
- Missing `Super::` in overrides (BeginPlay, EndPlay, Tick, etc.)
- `FTimerHandle` not cleared in EndPlay
- Delegates not unbound in EndPlay
- Spawning or `GetWorld()` in constructor
- Null `UInputAction*` passed to `BindAction()`
- `AddDynamic` without `IsAlreadyBound` check
- `Destroy()` then immediate pointer use
- Unchecked pointer dereference chains
- `TakeDamage` shadowing engine virtual

**WARNING:**
- Raw pointer instead of `TObjectPtr<>`
- `!= nullptr` instead of `IsValid()` on cached UObject pointers
- `BlueprintReadWrite` on everything — default to `BlueprintReadOnly`
- Missing `Category` on UPROPERTY
- Missing log category
- Bare `UCLASS()` on gameplay class
- Missing EndPlay override when timers/delegates/cached refs exist

## Pass 2: Performance (QPERF)

**CRITICAL:**
- Any allocation in Tick (`new`, `NewObject`, TArray copy, TArray::Add in loop without Reserve)
- String concat in Tick for gameplay logic
- TArray passed by value in function signature
- No pooling for spawns >1/sec
- `GetComponent`/`FindActor` in Tick — cache in BeginPlay

**WARNING:**
- Tick enabled without justification — prefer timers/events/dirty flags
- FString comparison in hot path — use FName
- Large TArray without Reserve
- Unthrottled expensive operations

## Pass 3: Replication (if applicable)

**CRITICAL:**
- Missing `bReplicates = true` with replicated properties/RPCs
- Missing `Super::GetLifetimeReplicatedProps`
- Missing `DOREPLIFETIME` for Replicated property
- Client modifying replicated property
- Missing `HasAuthority()` guard
- Reliable RPC in Tick
- NetMulticast called from client
- OnRep expected to fire on server — it doesn't

## Pass 4: Architecture

**SUGGESTION:**
- Deep inheritance (3+ levels) → components
- Direct cross-system coupling → delegates/events
- Hardcoded values → DataTable/DataAsset/UPROPERTY
- Magic numbers, functions >40 lines, commented-out code
- Framework hierarchy violations
- Enum vs GameplayTag misuse

## Output Format

```
## Code Review: [Scope]

### CRITICAL (must fix)
- **[Rule]** `File.cpp:Line` — Issue and fix

### WARNING (should fix)
- **[Rule]** `File.cpp:Line` — Issue and fix

### SUGGESTION (consider)
- **[Rule]** `File.cpp:Line` — Rationale

### Summary
- X critical / Y warnings / Z suggestions
- Assessment: PASS / PASS WITH WARNINGS / FAIL
```
