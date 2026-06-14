---
name: ue5-crash-debug
description: Diagnose UE5 crashes, access violations, null pointers, assertion failures, freezes, editor crashes, PIE crashes, build failures, linker errors, and compile errors. Use when the user says something crashed, broke, froze, won't compile, or shows an error log.
---

# UE5 Crash & Error Debugger

Match the symptom below and read the linked file for investigation steps.

| Symptom | Read |
|---------|------|
| Access violation, null pointer, garbage pointer | [nullptr-crashes.md](nullptr-crashes.md) |
| GC crash, object randomly invalid, stale reference | [gc-crashes.md](gc-crashes.md) |
| Crash in BeginPlay, constructor, or EndPlay | [lifecycle-crashes.md](lifecycle-crashes.md) |
| Multiplayer crash, RPC crash, replication error | [replication-crashes.md](replication-crashes.md) |
| Won't compile, linker error, unresolved external | [build-errors.md](build-errors.md) |
| Freeze, infinite loop, editor hangs | [freeze-patterns.md](freeze-patterns.md) |

## General approach

Read the callstack or log output. Identify which category fits. Read the spoke file for UE5-specific causes Codex wouldn't know by default. Fix the root cause, not the symptom.

## Gotchas (add a line each time we hit a new one)

- `Destroy()` is deferred — using a pointer immediately after `Destroy()` won't crash *that frame*, but will next GC pass
- `GetWorld()` returns null during teardown — always null-check before chaining `GetWorld()->GetFirstPlayerController()`
