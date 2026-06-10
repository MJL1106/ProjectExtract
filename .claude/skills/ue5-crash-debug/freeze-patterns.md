# Freezes / Infinite Loops / Editor Hangs

## Common UE5-specific causes

- **`while` loop without exit condition** — easy to write `while (bCondition)` where the condition never changes. Always have a max-iteration safety break.
- **Recursive BeginPlay spawning** — Actor A spawns Actor B in BeginPlay, Actor B spawns Actor A. Infinite recursion.
- **`Tick` doing heavy work without throttling** — not a freeze per se, but drops to 0 FPS. Use timers or frame counters.
- **Blocking load on game thread** — `LoadObject<>()` or `StaticLoadObject()` with a huge asset. Use `StreamableManager` async loading.
- **EQS query with no valid results + retry loop** — AI keeps re-running query every frame because it never finds valid results.
- **Delegate broadcast triggering itself** — delegate handler modifies state that triggers the same delegate. Infinite recursion.
- **Nav mesh generation on complex geometry** — editor hangs during nav mesh build. Not a code bug, but feels like a freeze.

## Investigation steps

1. If editor hangs, attach Visual Studio debugger and break — check the callstack to see where it's stuck
2. If game freezes but editor is fine, check Tick functions and while loops
3. If it's intermittent, suspect a delegate feedback loop or AI query loop
4. Add `ensure(IterationCount++ < 10000)` to suspicious loops during debugging
