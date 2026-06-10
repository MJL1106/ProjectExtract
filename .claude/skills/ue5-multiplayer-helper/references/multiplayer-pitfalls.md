# Multiplayer Pitfalls

## Will Desync or Crash

| Pitfall | Fix |
|---------|-----|
| Modifying replicated property on client | Only modify on server — clients get updates via replication |
| Calling Server RPC from server | Check `IsLocallyControlled()` before calling Server RPCs |
| Reliable RPC called every Tick | Use property replication for frequent updates, RPCs for events only |
| Forgetting `Super::GetLifetimeReplicatedProps` | Always call Super — parent class properties won't replicate |
| Not setting `bReplicates = true` | Actor won't replicate at all — nothing works |
| GameMode logic expecting client access | GameMode is server-only — use GameState for data clients need |
| PlayerController accessed from wrong client | PlayerController only exists on server + owning client |
| OnRep expected to fire on server | It doesn't. Call response logic directly on server after modifying property |
| NetMulticast called from client | Multicast only works when called on server |
| Large struct in Reliable RPC params | Can overflow reliable buffer. Use property replication for bulk data |
| Missing `Net/UnrealNetwork.h` include | DOREPLIFETIME macros won't compile |
| `HasAuthority()` not checked before state change | Client will modify local state that gets overwritten by replication |
| Accessing `GetFirstLocalPlayerController()` on dedicated server | Returns null — always check |

## Will Cause Bugs

| Pitfall | Fix |
|---------|-----|
| OnRep not firing | Property must actually change value. Setting same value won't trigger OnRep |
| Missing `SetReplicateMovement(true)` | Actor position won't replicate even with bReplicates |
| Default NetUpdateFrequency (100Hz) on all actors | Wastes bandwidth. Tune per actor type |
| No NetCullDistanceSquared tuning | Distant irrelevant actors keep replicating — set cull distance |
| bAlwaysRelevant on too many actors | Network bandwidth explosion. Only for truly global actors (GameState, critical managers) |
| Listen server host testing only | Many bugs only appear on clients. Always test as non-host client |
| Custom movement mode without CMC networking | Movement desync. Must implement FSavedMove for prediction |
| Assuming actor exists on all clients | Actors outside relevancy distance don't exist on that client |
| Replicated TArray with frequent small changes | Entire array re-sent on any change. Consider using individual properties or FastArraySerializer |
| Not handling mid-game join | Late-joining player may miss initial state. Use `COND_InitialOnly` and ensure GetLifetimeReplicatedProps covers all needed state |

## AI-Specific Multiplayer Pitfalls

| Pitfall | Fix |
|---------|-----|
| Running AI logic on client | AI Controllers only exist on server. All AI decisions are server-side |
| AI state not visible to clients | Replicate key AI state (combat state, target, alert level) for UI |
| AI companion not always visible | Set `bAlwaysRelevant = true` on companion pawn if owner must always see it |
| AI perception results used on client | Perception runs server-only. Replicate what clients need to show |
| Spawning AI on client | Only spawn on server. `SpawnActor` with `ESpawnActorCollisionHandlingMethod` on server, let replication handle clients |
