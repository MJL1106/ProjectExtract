# Replication Patterns — Complete Code Examples

## Basic Property Replication

### Header
```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MyReplicatedCharacter.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, float, NewHealth);

UCLASS(Blueprintable)
class MYPROJECT_API AMyReplicatedCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AMyReplicatedCharacter();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintCallable, Category = "Combat")
    void ApplyDamage(float DamageAmount, AController* InstigatorController);

protected:
    virtual void BeginPlay() override;

    // --- Replicated State ---
    UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth, BlueprintReadOnly, Category = "Combat")
    float CurrentHealth;

    UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "Combat")
    float MaxHealth = 100.f;

    UPROPERTY(ReplicatedUsing = OnRep_bIsDead, BlueprintReadOnly, Category = "Combat")
    bool bIsDead = false;

    // --- OnRep Functions ---
    UFUNCTION()
    void OnRep_CurrentHealth();

    UFUNCTION()
    void OnRep_bIsDead();

    // --- Events ---
    UPROPERTY(BlueprintAssignable, Category = "Events")
    FOnHealthChanged OnHealthChanged;

    // --- Shared logic (called by both server directly and clients via OnRep) ---
    void HandleHealthChanged();
    void HandleDeath();

    // --- RPCs ---
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastPlayHitReaction();

    UFUNCTION(NetMulticast, Reliable)
    void MulticastPlayDeathEffects();

    UFUNCTION(Client, Reliable)
    void ClientNotifyDamageDealt(float DamageAmount);
};
```

### Source
```cpp
#include "MyReplicatedCharacter.h"
#include "Net/UnrealNetwork.h"

AMyReplicatedCharacter::AMyReplicatedCharacter()
{
    bReplicates = true;
    SetReplicateMovement(true);

    PrimaryActorTick.bCanEverTick = false;
    NetUpdateFrequency = 60.f;
    MinNetUpdateFrequency = 20.f;
}

void AMyReplicatedCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AMyReplicatedCharacter, CurrentHealth);
    DOREPLIFETIME(AMyReplicatedCharacter, MaxHealth);
    DOREPLIFETIME(AMyReplicatedCharacter, bIsDead);
}

void AMyReplicatedCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        CurrentHealth = MaxHealth;
    }
}

void AMyReplicatedCharacter::ApplyDamage(float DamageAmount, AController* InstigatorController)
{
    // Only server modifies gameplay state
    if (!HasAuthority()) return;
    if (bIsDead) return;
    if (DamageAmount <= 0.f) return;

    const float OldHealth = CurrentHealth;
    CurrentHealth = FMath::Clamp(CurrentHealth - DamageAmount, 0.f, MaxHealth);

    // Server-side response (OnRep does NOT fire on server)
    HandleHealthChanged();

    // Cosmetic feedback for all clients
    MulticastPlayHitReaction();

    // Notify the attacker specifically
    if (IsValid(InstigatorController))
    {
        if (auto* AttackerPawn = Cast<AMyReplicatedCharacter>(InstigatorController->GetPawn()))
        {
            AttackerPawn->ClientNotifyDamageDealt(DamageAmount);
        }
    }

    if (CurrentHealth <= 0.f)
    {
        bIsDead = true;
        // Server-side response
        HandleDeath();
        // Tell everyone
        MulticastPlayDeathEffects();
    }
}

// --- OnRep (clients only) ---

void AMyReplicatedCharacter::OnRep_CurrentHealth()
{
    HandleHealthChanged();
}

void AMyReplicatedCharacter::OnRep_bIsDead()
{
    if (bIsDead)
    {
        HandleDeath();
    }
}

// --- Shared logic (runs on both server and clients) ---

void AMyReplicatedCharacter::HandleHealthChanged()
{
    OnHealthChanged.Broadcast(CurrentHealth);
    // Update health bar widget, play hurt overlay, etc.
}

void AMyReplicatedCharacter::HandleDeath()
{
    // Ragdoll, disable input, etc.
}

// --- RPCs ---

void AMyReplicatedCharacter::MulticastPlayHitReaction_Implementation()
{
    // Cosmetic only — play animation, spawn blood VFX
}

void AMyReplicatedCharacter::MulticastPlayDeathEffects_Implementation()
{
    // Cosmetic only — play death sound, spawn death VFX
}

void AMyReplicatedCharacter::ClientNotifyDamageDealt_Implementation(float DamageAmount)
{
    // Show hit marker on attacker's HUD
}
```

## Server RPC with Validation

```cpp
// Header
UFUNCTION(Server, Reliable, WithValidation)
void ServerUseAbility(int32 AbilityIndex);

// Source
void AMyCharacter::ServerUseAbility_Implementation(int32 AbilityIndex)
{
    if (!IsValid(AbilityComponent)) return;

    AbilityComponent->ActivateAbility(AbilityIndex);
}

bool AMyCharacter::ServerUseAbility_Validate(int32 AbilityIndex)
{
    // Validate enum range
    if (AbilityIndex < 0 || AbilityIndex >= MaxAbilitySlots)
    {
        return false;
    }

    // Rate limiting — could also check cooldown
    return true;
}
```

## GameState Replication

```cpp
// Header
UCLASS()
class MYPROJECT_API AMyGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    AMyGameState();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void AddTeamScore(int32 TeamIndex, int32 Points);

protected:
    UPROPERTY(ReplicatedUsing = OnRep_TeamScores, BlueprintReadOnly, Category = "Score")
    TArray<int32> TeamScores;

    UPROPERTY(ReplicatedUsing = OnRep_MatchState, BlueprintReadOnly, Category = "Match")
    EMatchState CurrentMatchState;

    UFUNCTION()
    void OnRep_TeamScores();

    UFUNCTION()
    void OnRep_MatchState();
};

// Source
AMyGameState::AMyGameState()
{
    bAlwaysRelevant = true; // GameState must always replicate
    NetUpdateFrequency = 10.f;
}

void AMyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AMyGameState, TeamScores);
    DOREPLIFETIME(AMyGameState, CurrentMatchState);
}

void AMyGameState::AddTeamScore(int32 TeamIndex, int32 Points)
{
    if (!HasAuthority()) return;

    if (TeamScores.IsValidIndex(TeamIndex))
    {
        TeamScores[TeamIndex] += Points;
        // Server-side response
        OnRep_TeamScores();
    }
}
```
