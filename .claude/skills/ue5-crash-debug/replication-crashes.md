# Multiplayer / Replication Crashes

## Common UE5-specific causes

- **Client calling server-only logic** — no `HasAuthority()` guard. Both server and client run BeginPlay, Tick, etc.
- **Spawning on client** — actors spawned on client without `HasAuthority()` guard create duplicates. Server spawns, replication handles clients.
- **RPC on null PlayerController** — `ServerRPC` requires a valid owning connection. AI pawns don't have one.
- **Accessing `PlayerState` before replication** — on clients, `PlayerState` may be null for a few frames after join. Null-check or use `OnRep_PlayerState`.
- **`DOREPLIFETIME` missing for replicated property** — property marked `Replicated` but not registered. Silent desync or crash.
- **NetSerialize on invalid data** — custom struct serialization receiving garbage from network. Validate before use.
- **Movement component authority mismatch** — `SetActorLocation` on a character with CMC bypasses movement networking entirely.

## Investigation steps

1. Does the crash only happen on client, only on server, or both? This narrows it immediately.
2. Client-only crash → you're accessing something that only exists on server (authority-only actor, server-side component)
3. Server-only crash → probably an RPC from client with unexpected/null data
4. Both → likely a BeginPlay issue where you're not guarding with HasAuthority
