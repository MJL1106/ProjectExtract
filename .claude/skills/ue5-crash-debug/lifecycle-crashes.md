# Lifecycle Crashes (BeginPlay / Constructor / EndPlay)

## Common UE5-specific causes

- **Spawning actors in the constructor** — constructors run during CDO (Class Default Object) creation. No world exists yet. Spawn in BeginPlay.
- **`GetWorld()` in constructor** — returns null. World doesn't exist during CDO.
- **Calling `Destroy()` in constructor** — undefined behavior. CDO isn't a real actor.
- **Missing `Super::BeginPlay()`** — components won't initialize. Always call Super first.
- **Order-dependent BeginPlay** — Actor A's BeginPlay accesses Actor B, but B hasn't had BeginPlay yet. Use timers or events instead of assuming order.
- **Accessing `PlayerController` in pawn BeginPlay** — controller may not be assigned yet. Bind to `OnPossessedPawnChanged` or `ReceiveController`.
- **EndPlay accessing already-destroyed subsystems** — during shutdown, subsystems may be gone. Null-check everything in EndPlay.

## Investigation steps

1. Check if the crash callstack includes `ConstructorHelpers` or `CDO` — you're doing something in the constructor that needs a world
2. If crash is in BeginPlay, check if you're depending on another actor being ready
3. If crash is in EndPlay, check if you're accessing systems that shut down before your actor
