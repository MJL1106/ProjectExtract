# GC Crashes / Stale References

## Common UE5-specific causes

- **UObject pointer without `UPROPERTY()`** — GC doesn't know about it, collects the object, pointer goes stale. This is the #1 silent killer.
- **Storing UObject* in a non-UPROPERTY container** — `TArray<UObject*>` in a struct without UPROPERTY. GC can't see it.
- **Lambda capturing UObject raw pointer** — GC runs, object dies, lambda fires later with garbage pointer. Capture `TWeakObjectPtr` instead.
- **Delegate bound to destroyed actor** — if you don't unbind in EndPlay, the delegate fires on a GC'd object. Use `RemoveAll(this)` in EndPlay.
- **Timer firing after actor destroyed** — `FTimerHandle` not cleared in EndPlay. Timer fires, `this` is garbage.
- **`AddDynamic` double-bind** — fires handler twice, second time object may be mid-destruction. Guard with `IsAlreadyBound()`.

## Investigation steps

1. If crash is intermittent / timing-dependent, suspect GC
2. Search for raw `UObject*` members without `UPROPERTY()` — especially in custom structs
3. Check if any lambdas or delegates capture `this` or raw pointers
4. Verify all timers are cleared and delegates unbound in EndPlay
5. Use `TWeakObjectPtr` and check `IsValid()` before access for non-owning references
