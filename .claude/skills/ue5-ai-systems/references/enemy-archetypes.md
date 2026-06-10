# Enemy Archetypes — BT Subtrees and Configuration

## Data-Driven Archetype Config

Use a DataAsset per archetype so designers can tune without code changes:

```cpp
// Header
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EnemyArchetypeData.generated.h"

UENUM(BlueprintType)
enum class EEnemyArchetype : uint8
{
    Grunt,
    Rusher,
    Heavy,
    Sniper,
    Officer
};

UCLASS(BlueprintType)
class MYPROJECT_API UEnemyArchetypeData : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    EEnemyArchetype ArchetypeType;

    // --- Combat ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
    float PreferredEngagementRange = 1000.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
    float MaxEngagementRange = 2000.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BaseAccuracy = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
    float FireRate = 0.2f; // Seconds between shots

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    bool bUseCover = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
    bool bAllowCoverPeek = true;

    // --- Movement ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
    float WalkSpeed = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
    float SprintSpeed = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
    bool bCanSprint = true;

    // --- Health ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "0.0"))
    float MaxHealth = 100.f;

    // --- Behavior ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
    TObjectPtr<UBehaviorTree> CombatSubtree; // Archetype-specific combat BT subtree

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior", meta = (ClampMin = "0.0"))
    float AlertDuration = 5.f; // How long to search before returning to patrol

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior", meta = (ClampMin = "0.0"))
    float PerceptionSightRadius = 2000.f;

    // --- Officer-Specific ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Officer", meta = (EditCondition = "ArchetypeType == EEnemyArchetype::Officer"))
    float BuffRadius = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Officer", meta = (EditCondition = "ArchetypeType == EEnemyArchetype::Officer", ClampMin = "0.0", ClampMax = "1.0"))
    float AccuracyBuffPercent = 0.15f;

    // --- Weapon ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
    TSubclassOf<AWeaponBase> DefaultWeaponClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
    int32 StartingAmmo = 120;
};
```

## Single Enemy Character — Archetype Applied at Runtime

All enemies use one `AEnemyCharacter` class. The DataAsset configures everything in `BeginPlay`:

```cpp
// In AEnemyCharacter header
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Archetype")
TObjectPtr<UEnemyArchetypeData> ArchetypeData;

// In AEnemyCharacter::BeginPlay
void AEnemyCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (!IsValid(ArchetypeData)) return;

    // Apply stats from DataAsset
    if (auto* HealthComp = FindComponentByClass<UHealthComponent>())
    {
        HealthComp->SetMaxHealth(ArchetypeData->MaxHealth);
    }

    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->MaxWalkSpeed = ArchetypeData->WalkSpeed;
    }

    // Officer-specific: add squad buff component at runtime
    if (ArchetypeData->ArchetypeType == EEnemyArchetype::Officer)
    {
        auto* SquadBuff = NewObject<USquadBuffComponent>(this);
        if (IsValid(SquadBuff))
        {
            SquadBuff->BuffRadius = ArchetypeData->BuffRadius;
            SquadBuff->AccuracyBuffPercent = ArchetypeData->AccuracyBuffPercent;
            SquadBuff->RegisterComponent();
        }
    }

    // The AI Controller reads ArchetypeData->CombatSubtree to inject
    // the correct combat branch via RunBehaviorDynamic or subtree reference
}
```

## Grunt — Combat Subtree

Standard cover-based engagement:

```
Grunt Combat Selector
├── Decorator: NeedsReload? → BTTask: Reload
├── Decorator: InCoverAndHasLOS? → BTTask: PeekAndFire
├── Sequence: FindAndMoveToCover
│   ├── BTTask: RunEQS_FindCover → BB_CoverLocation
│   └── BTTask: MoveTo(BB_CoverLocation)
└── Fallback: BTTask: FireFromPosition (no cover available)
```

## Rusher — Combat Subtree

Aggressive close-range approach:

```
Rusher Combat Selector
├── Decorator: InMeleeRange?(200) → BTTask: MeleeAttack
├── Sequence: CloseDistance
│   ├── BTTask: Sprint → SetMovementSpeed(SprintSpeed)
│   ├── BTTask: MoveTo(BB_CombatTarget, AcceptanceRadius=300)
│   └── BTTask: FireWhileMoving (reduced accuracy)
└── Fallback: BTTask: FireFromPosition
```

Key: Rusher ignores cover (`bUseCover = false`). Moves directly toward player. Higher movement speed, lower health — glass cannon.

## Heavy — Combat Subtree

Anchor and suppress:

```
Heavy Combat Selector
├── Decorator: NeedsReload? → BTTask: Reload (longer reload time)
├── Decorator: HasAnchorPosition?
│   ├── BTTask: RotateTowardTarget (slow turn rate)
│   └── BTTask: SustainedFire (high ammo consumption, wide spread suppression)
└── Sequence: EstablishAnchor
    ├── BTTask: FindAnchorPosition (open area with sightlines)
    └── BTTask: MoveTo(BB_AnchorPosition) → SetAnchor
```

Key: Heavy has slow turn rate (`RotationRate.Yaw = 90`). Vulnerable to flanking. High health, high damage, slow.

## Sniper — Combat Subtree

Long-range precision:

```
Sniper Combat Selector
├── Decorator: IsExposed?(enemy has LOS to me AND close) → Sequence: Reposition
│   ├── BTTask: RunEQS_FindSniperNest → BB_SniperPosition
│   └── BTTask: MoveTo(BB_SniperPosition)
├── Sequence: EngageFromRange
│   ├── Decorator: HasClearLOS?
│   ├── BTTask: AimAtTarget(Duration=1.5) // Aiming delay before firing
│   └── BTTask: FireSingleShot (high accuracy, high damage)
└── Fallback: BTTask: HoldPosition
```

Key: Sniper has long aim time before firing (telegraph for player to react). Repositions if player closes distance below `MinEngagementRange`.

## Officer — Combat Subtree

Support and coordinate:

```
Officer Combat Selector
├── Service: UpdateSquadMembers (find allies in BuffRadius)
├── Service: ApplyBuffAura (accuracy boost to squad)
├── Decorator: IsExposed? → Sequence: Retreat
│   ├── BTTask: FindPositionBehindSquad
│   └── BTTask: MoveTo(BB_SafePosition)
├── Sequence: Coordinate
│   ├── BTTask: CalloutTarget → Set BB_PriorityTarget on squad BBs
│   └── BTTask: FireFromRear (conservative, low aggression)
└── Fallback: BTTask: FollowSquad
```

### Squad Buff Implementation

Tracks buffed actors in a `TSet` to prevent stacking and ensure clean removal on death.

```cpp
// USquadBuffComponent header (added to AEnemyCharacter conditionally for Officer archetype)
UCLASS()
class MYPROJECT_API USquadBuffComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Buff")
    float BuffRadius = 1500.f;

    UPROPERTY(EditAnywhere, Category = "Buff", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AccuracyBuffPercent = 0.15f;

    void ApplyBuffToSquad();
    void RemoveAllBuffs();

private:
    // Track who is buffed to prevent stacking and enable clean removal
    UPROPERTY()
    TSet<TWeakObjectPtr<UWeaponComponent>> BuffedWeapons;
};

// Source
void USquadBuffComponent::ApplyBuffToSquad()
{
    // Run on timer (every 1-2s), not every frame
    AActor* Owner = GetOwner();
    if (!IsValid(Owner)) return;

    const FVector OwnerLocation = Owner->GetActorLocation();

    // Remove buffs from actors that moved out of range or became invalid
    TSet<TWeakObjectPtr<UWeaponComponent>> StillInRange;

    TArray<AActor*> NearbyActors;
    // Use overlap sphere or cached list from perception

    for (AActor* Actor : NearbyActors)
    {
        if (!IsValid(Actor) || Actor == Owner) continue;

        // Check if same faction — use AEnemyCharacter, not a subclass
        auto* EnemyChar = Cast<AEnemyCharacter>(Actor);
        if (!IsValid(EnemyChar)) continue;

        const float DistSq = FVector::DistSquared(OwnerLocation, Actor->GetActorLocation());
        if (DistSq > BuffRadius * BuffRadius) continue;

        if (auto* WeaponComp = EnemyChar->FindComponentByClass<UWeaponComponent>())
        {
            TWeakObjectPtr<UWeaponComponent> WeakWeapon(WeaponComp);

            if (!BuffedWeapons.Contains(WeakWeapon))
            {
                // New target — apply buff
                WeaponComp->SetAccuracyMultiplier(1.f + AccuracyBuffPercent);
            }

            StillInRange.Add(WeakWeapon);
        }
    }

    // Remove buff from actors no longer in range
    for (const TWeakObjectPtr<UWeaponComponent>& WeakWeapon : BuffedWeapons)
    {
        if (!StillInRange.Contains(WeakWeapon) && WeakWeapon.IsValid())
        {
            WeakWeapon->SetAccuracyMultiplier(1.f); // Reset to default
        }
    }

    BuffedWeapons = MoveTemp(StillInRange);
}

void USquadBuffComponent::RemoveAllBuffs()
{
    for (const TWeakObjectPtr<UWeaponComponent>& WeakWeapon : BuffedWeapons)
    {
        if (WeakWeapon.IsValid())
        {
            WeakWeapon->SetAccuracyMultiplier(1.f);
        }
    }
    BuffedWeapons.Empty();
}
```

Death cleanup on the enemy character:

```cpp
// In AEnemyCharacter — handles all archetype death logic
void AEnemyCharacter::OnDeath()
{
    // Officer cleanup: remove squad buffs
    if (auto* SquadBuff = FindComponentByClass<USquadBuffComponent>())
    {
        SquadBuff->RemoveAllBuffs();
    }

    // Common death logic for all archetypes...
}
```

## Shared: Patrol Point System

```cpp
// Patrol points are placed in the level as APatrolPath actors
// Each APatrolPath contains a spline or array of FVector waypoints
// Enemy AI Controller stores reference to its assigned APatrolPath

// In AEnemyAIController
UPROPERTY(EditInstanceOnly, Category = "AI")
TObjectPtr<APatrolPath> AssignedPatrolPath;

int32 CurrentPatrolIndex = 0;

FVector GetNextPatrolPoint()
{
    if (!IsValid(AssignedPatrolPath)) return FVector::ZeroVector;

    CurrentPatrolIndex = (CurrentPatrolIndex + 1) % AssignedPatrolPath->GetPointCount();
    return AssignedPatrolPath->GetPointAt(CurrentPatrolIndex);
}
```

## Shared: Alert → Combat → Search Flow

Uses NodeMemory for the `TimeSinceLastSeen` timer — BT services are shared instances.

```cpp
// BT Service: UpdateThreatState (runs on all enemy types)

struct FThreatStateMemory
{
    float TimeSinceLastSeen = 0.f;
};

// In class declaration:
virtual uint16 GetInstanceMemorySize() const override { return sizeof(FThreatStateMemory); }

void UBTService_UpdateThreatState::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    FThreatStateMemory* Memory = CastInstanceNodeMemory<FThreatStateMemory>(NodeMemory);

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!IsValid(BB)) return;

    AActor* CombatTarget = Cast<AActor>(BB->GetValueAsObject(TEXT("BB_CombatTarget")));

    if (IsValid(CombatTarget))
    {
        // Has active target → combat
        BB->SetValueAsEnum(TEXT("BB_ThreatState"), static_cast<uint8>(EThreatState::Combat));
        BB->SetValueAsVector(TEXT("BB_LastKnownLocation"), CombatTarget->GetActorLocation());
        Memory->TimeSinceLastSeen = 0.f;
    }
    else if (BB->IsVectorValueSet(TEXT("BB_LastKnownLocation")))
    {
        // Had a target, lost it → search
        Memory->TimeSinceLastSeen += DeltaSeconds;

        // AlertDuration should come from the archetype DataAsset via the controller
        AAIController* Controller = OwnerComp.GetAIOwner();
        float Duration = 5.f; // fallback
        if (IsValid(Controller))
        {
            if (auto* EnemyChar = Cast<AEnemyCharacter>(Controller->GetPawn()))
            {
                if (IsValid(EnemyChar->ArchetypeData))
                {
                    Duration = EnemyChar->ArchetypeData->AlertDuration;
                }
            }
        }

        if (Memory->TimeSinceLastSeen < Duration)
        {
            BB->SetValueAsEnum(TEXT("BB_ThreatState"), static_cast<uint8>(EThreatState::Searching));
        }
        else
        {
            // Search timed out → return to patrol
            BB->SetValueAsEnum(TEXT("BB_ThreatState"), static_cast<uint8>(EThreatState::Patrol));
            BB->ClearValue(TEXT("BB_LastKnownLocation"));
        }
    }
    else
    {
        BB->SetValueAsEnum(TEXT("BB_ThreatState"), static_cast<uint8>(EThreatState::Patrol));
    }
}
```
