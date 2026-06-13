# Null Pointer / Access Violation

## Common UE5-specific causes

- **Unchecked `Cast<>()`** — Cast returns null if type doesn't match. Always `if (auto* X = Cast<T>(Obj))`.
- **`GetComponent<T>()` on actor without that component** — returns null silently.
- **Accessing player controller before possession** — `GetController()` is null in BeginPlay for pawns spawned before possession.
- **Widget `BindWidget` pointing to missing BP widget** — hard crash. Use `BindWidgetOptional` + null-check.
- **`GetOwner()` on a component added at runtime** — null if spawned standalone. Check before use.
- **`UInputAction*` null in Enhanced Input** — crash when `BindAction` receives null. Always null-check input action pointers.
- **`GetWorld()` during teardown** — returns null. Never chain `GetWorld()->GetTimerManager()` without checking.

## Investigation steps

1. Look at the callstack — find the last line in YOUR code (not engine code)
2. Check every pointer on that line — one of them is null
3. If it's a Cast, check what type the source actually is with `GetClass()->GetName()`
4. If it's a component, verify it exists in the Blueprint or is created in constructor
