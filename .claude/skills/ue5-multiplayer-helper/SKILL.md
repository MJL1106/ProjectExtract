---
name: ue5-multiplayer-helper
description: UE5 multiplayer and replication guidance. Use when the user works on networked gameplay — replication, RPCs, authority, server/client behavior, player state, game state, character movement networking, or any question about how things work across server and clients.
---

# UE5 Multiplayer Helper

## Purpose
Provide correct multiplayer replication patterns for UE5 C++. Replication bugs are the hardest to debug — this skill prevents them at the code-writing stage.

## Instructions

### Core Concepts

**Authority Model:**
- Server is authoritative for gameplay state
- Clients predict movement but server validates
- Never trust client data for game-critical logic

**Who Owns What:**
```
Server Only:        AGameModeBase (does NOT exist on clients)
Server + Clients:   AGameStateBase, APlayerState (replicated)
Owning Client:      APlayerController (exists on server + owning client only)
All Clients:        APawn/ACharacter (replicated to all relevant clients)
```

**Network Roles (`GetLocalRole()` / `GetRemoteRole()`):**
| Role | Meaning |
|------|---------|
| `ROLE_Authority` | Server's version of the actor |
| `ROLE_AutonomousProxy` | Client's own pawn (locally controlled) |
| `ROLE_SimulatedProxy` | Another player's pawn on your client |
| `ROLE_None` | Not replicated |

### Property Replication

See `references/replication-patterns.md` for complete code examples.

**The Three Steps:**
1. Mark the property with `Replicated` or `ReplicatedUsing = OnRep_FuncName` in UPROPERTY
2. Register with `DOREPLIFETIME` in `GetLifetimeReplicatedProps` (always call `Super::`)
3. Set `bReplicates = true` in constructor

**CRITICAL: OnRep only fires on clients.** If the server also needs to react to a property change, call the response logic directly after modifying the property on the server. Do not rely on OnRep for server-side behavior.

**Replication Conditions — use them to save bandwidth:**
| Condition | Use When |
|-----------|----------|
| `COND_None` | Default — replicate to everyone |
| `COND_OwnerOnly` | Personal data: ammo, abilities, inventory |
| `COND_SkipOwner` | Other players seeing your state (they already know locally) |
| `COND_SimulatedOnly` | Visual-only data for remote players |
| `COND_AutonomousOnly` | Data only the controlling player needs |
| `COND_InitialOnly` | Sent once: team assignment, character class, cosmetics |
| `COND_Custom` | Advanced: manual relevancy control via `PreReplication` |

### RPCs

**Rules:**
| Type | Called On | Executes On | Use For |
|------|----------|-------------|---------|
| `Server, Reliable` | Client | Server | Gameplay actions (fire, interact, use ability) |
| `Client, Reliable` | Server | Owning client | Important UI feedback (damage numbers, quest complete) |
| `Client, Unreliable` | Server | Owning client | Non-critical client feedback |
| `NetMulticast, Unreliable` | Server | Server + all clients | Cosmetic effects (VFX, SFX, animations) |
| `NetMulticast, Reliable` | Server | Server + all clients | Rare important events (round start, player death) — use sparingly |

**Reliable vs Unreliable:**
- `Reliable` — guaranteed, ordered. For gameplay-critical events only
- `Unreliable` — may be dropped. For cosmetics, frequent updates
- NEVER call Reliable RPCs in Tick — saturates the reliable buffer, causes disconnects
- NEVER call NetMulticast from a client — it won't replicate

**WithValidation** — add to Server RPCs for anti-cheat:
```cpp
UFUNCTION(Server, Reliable, WithValidation)
void ServerUseAbility(int32 AbilityIndex);
```
Validate against: ability cooldowns, valid enum ranges, impossible states, rate limiting. Not just arbitrary magnitude checks.

### Network Relevancy

Actors stop replicating to clients when outside their relevancy distance.

**Key Properties:**
| Property | Default | Purpose |
|----------|---------|---------|
| `NetCullDistanceSquared` | Varies | Distance squared beyond which actor is not relevant |
| `bAlwaysRelevant` | false | Set true for GameState, important managers |
| `bOnlyRelevantToOwner` | false | Set true for inventory, personal UI actors |
| `bNetLoadOnClient` | true | Set false for server-only placed actors |

For an FPS: players and nearby AI should always be relevant within engagement range. Distant AI can have lower cull distance.

### Bandwidth Management

**NetUpdateFrequency — tune per actor type:**
| Actor Type | Suggested Frequency | Why |
|------------|-------------------|-----|
| Player pawn | 100 (default) | Needs responsive movement |
| AI enemy (in combat) | 30-60 | Needs smooth but not as responsive |
| AI enemy (idle/patrol) | 10-20 | Low priority |
| Projectiles | 60-100 | Fast-moving, needs accuracy |
| Pickup items | 5-10 | Rarely changes state |
| GameState | 10 | Score/timer updates are infrequent |

```cpp
AMyAIEnemy::AMyAIEnemy()
{
    bReplicates = true;
    SetReplicateMovement(true);
    NetUpdateFrequency = 30.f;
    MinNetUpdateFrequency = 10.f; // Can drop to this when not changing
}
```

**Push Model Replication (UE5 optimization):**
Instead of the engine checking every replicated property each frame, explicitly mark dirty:
```cpp
#include "Net/Core/PushModel/PushModel.h"

void AMyActor::SetHealth(float NewHealth)
{
    if (HasAuthority())
    {
        CurrentHealth = NewHealth;
        MARK_PROPERTY_DIRTY_FROM_NAME(AMyActor, CurrentHealth, this);

        // Server-side response (OnRep won't fire here)
        HandleHealthChanged();
    }
}
```

### Character Movement Component Networking

The CMC is the most complex networking piece in an FPS. Key concepts:

**Client Prediction:**
- Client executes movement locally for instant feel
- Sends moves to server
- Server validates and corrects if needed
- Client replays unacknowledged moves on correction

**Key Settings:**
```cpp
// In Character constructor
UCharacterMovementComponent* CMC = GetCharacterMovement();
CMC->bUseFlatBaseForFloorChecks = true; // Better for networked games
// NetworkMaxSmoothUpdateDistance, NetworkSimulatedSmoothLocationTime etc.
// are tuned in the CMC defaults — adjust for your game's feel
```

**Custom Movement Modes:**
When adding custom movement (wall running, sliding, grapple), you must handle it in the CMC's networking pipeline. See `references/cmc-networking.md` for the full pattern with `FSavedMove` and `FNetworkPredictionData`.

### AI in Multiplayer

**Architecture:**
- AI Controllers exist on **server only** — no replication needed
- AI Pawns replicate state to clients via property replication
- Behavior Trees / State Machines run **server-side only**
- Clients see AI via: replicated movement, replicated state properties, multicast VFX

**AI Perception:**
- `UAIPerceptionComponent` runs server-side
- Perception results (who the AI is targeting, alert state) should be replicated if clients need to show it (e.g. enemy health bars, alert indicators)

**Pattern: Replicated AI State for Client UI**
```cpp
// In AI Pawn header
UPROPERTY(ReplicatedUsing = OnRep_AIState, BlueprintReadOnly, Category = "AI")
EAIBehaviorState CurrentAIState; // Idle, Patrol, Alert, Combat, Dead

UFUNCTION()
void OnRep_AIState();

// In AI Pawn source
void AMyAIPawn::OnRep_AIState()
{
    // Client-side: update UI indicator, play state transition animation
    UpdateAIStateWidget();
}

// Server sets this from the AI Controller:
void AMyAIController::SetAIState(EAIBehaviorState NewState)
{
    if (auto* AIPawn = Cast<AMyAIPawn>(GetPawn()))
    {
        AIPawn->CurrentAIState = NewState;
        // Server-side response (OnRep won't fire on server)
        AIPawn->UpdateAIStateWidget();
    }
}
```

**AI Companion Specifics:**
- Companion AI follows the same rules as enemy AI
- `bAlwaysRelevant = true` if companion must always be visible to owner
- Companion commands (follow, hold position, attack target) are Server RPCs from the owning player

### Listen Server vs Dedicated Server

**Listen Server (one player is also the server):**
- The host player's `PlayerController` is on the server — Server RPCs execute immediately
- Be careful: `IsLocallyControlled()` returns true for the host pawn on the server
- Test both as host AND as client — many bugs only appear on one side

**Dedicated Server:**
- No local player on the server at all
- `GetFirstLocalPlayerController()` returns null on dedicated server — always check
- Preferred for competitive FPS

**Testing approach:**
- Use PIE with 2+ players: one as listen server, others as clients
- Check Output Log for Net warnings
- Test: join, leave, rejoin — replication state should be consistent

### Common Pitfalls

See `references/multiplayer-pitfalls.md` for the full table with fixes.

Key ones:
- Modifying replicated property on client → only server modifies
- OnRep expected to fire on server → it doesn't, call logic directly
- Reliable RPC spam → use property replication for frequent updates
- GameMode accessed on client → it's null, use GameState
- Missing `Super::GetLifetimeReplicatedProps` → parent properties won't replicate
- `HasAuthority()` not checked before state modification → clients will desync
