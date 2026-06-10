# Companion BT — Task and Service Implementations

## BT Service: UpdateCompanionState

Runs every 0.25s. Evaluates priorities and updates blackboard.

```cpp
// Header
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_UpdateCompanionState.generated.h"

UCLASS()
class MYPROJECT_API UBTService_UpdateCompanionState : public UBTService
{
    GENERATED_BODY()

public:
    UBTService_UpdateCompanionState();

protected:
    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector BB_CombatTarget;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector BB_ReviveTarget;

    UPROPERTY(EditAnywhere, Category = "Config")
    float DBNOScanRadius = 3000.f;
};

// Source
#include "BTService_UpdateCompanionState.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "HealthComponent.h"

UBTService_UpdateCompanionState::UBTService_UpdateCompanionState()
{
    NodeName = TEXT("Update Companion State");
    Interval = 0.25f;
    RandomDeviation = 0.05f;
}

void UBTService_UpdateCompanionState::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!IsValid(BB)) return;

    AAIController* Controller = OwnerComp.GetAIOwner();
    if (!IsValid(Controller)) return;

    APawn* ControlledPawn = Controller->GetPawn();
    if (!IsValid(ControlledPawn)) return;

    UAIPerceptionComponent* PerceptionComp = Controller->GetPerceptionComponent();
    if (!IsValid(PerceptionComp)) return;

    // --- Priority 1: Scan for DBNO allies ---
    // This is the backup scan — primary detection is via direct delegate binding
    // (see ACompanionAIController::OnPossess below)
    AActor* ClosestDBNO = nullptr;
    float ClosestDistSq = DBNOScanRadius * DBNOScanRadius;

    TArray<AActor*> PerceivedActors;
    PerceptionComp->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);

    for (AActor* Actor : PerceivedActors)
    {
        if (!IsValid(Actor)) continue;

        // Check if friendly and DBNO
        if (auto* HealthComp = Actor->FindComponentByClass<UHealthComponent>())
        {
            if (HealthComp->IsDBNO())
            {
                const float DistSq = FVector::DistSquared(ControlledPawn->GetActorLocation(), Actor->GetActorLocation());
                if (DistSq < ClosestDistSq)
                {
                    ClosestDistSq = DistSq;
                    ClosestDBNO = Actor;
                }
            }
        }
    }

    BB->SetValueAsObject(BB_ReviveTarget.SelectedKeyName, ClosestDBNO);

    // --- Priority 2: Combat target is set by perception update handler ---
    // (handled in ACompanionAIController::OnPerceptionUpdated)
}
```

## Companion AI Controller — DBNO Delegate Binding

Primary method for instant DBNO detection. Bound in `OnPossess`, unbound in `OnUnPossess`.

```cpp
// In ACompanionAIController header
UPROPERTY()
TWeakObjectPtr<UHealthComponent> CachedPlayerHealthComp;

void OnPlayerDBNO(AActor* DBNOActor);

// In ACompanionAIController source
void ACompanionAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    // Bind to player's DBNO delegate for instant response
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        if (APawn* PlayerPawn = PC->GetPawn())
        {
            if (auto* HealthComp = PlayerPawn->FindComponentByClass<UHealthComponent>())
            {
                CachedPlayerHealthComp = HealthComp;
                HealthComp->OnDBNO.AddDynamic(this, &ACompanionAIController::OnPlayerDBNO);
            }
        }
    }

    // Set follow target
    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
        {
            BB->SetValueAsObject(TEXT("BB_FollowTarget"), PC->GetPawn());
        }
    }
}

void ACompanionAIController::OnUnPossess()
{
    // Unbind delegate to prevent dangling references
    if (CachedPlayerHealthComp.IsValid())
    {
        CachedPlayerHealthComp->OnDBNO.RemoveDynamic(this, &ACompanionAIController::OnPlayerDBNO);
        CachedPlayerHealthComp.Reset();
    }

    Super::OnUnPossess();
}

void ACompanionAIController::OnPlayerDBNO(AActor* DBNOActor)
{
    if (!IsValid(DBNOActor)) return;

    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        BB->SetValueAsObject(TEXT("BB_ReviveTarget"), DBNOActor);
    }
}
```

## BT Task: CalculateFormationPoint

```cpp
// Header
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_CalculateFormationPoint.generated.h"

UCLASS()
class MYPROJECT_API UBTTask_CalculateFormationPoint : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_CalculateFormationPoint();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

protected:
    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector BB_FollowTarget;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector BB_FollowLocation;

    UPROPERTY(EditAnywhere, Category = "Formation")
    FVector FormationOffset = FVector(-200.f, 150.f, 0.f);
};

// Source
#include "BTTask_CalculateFormationPoint.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"

UBTTask_CalculateFormationPoint::UBTTask_CalculateFormationPoint()
{
    NodeName = TEXT("Calculate Formation Point");
}

EBTNodeResult::Type UBTTask_CalculateFormationPoint::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!IsValid(BB)) return EBTNodeResult::Failed;

    AActor* FollowTarget = Cast<AActor>(BB->GetValueAsObject(BB_FollowTarget.SelectedKeyName));
    if (!IsValid(FollowTarget)) return EBTNodeResult::Failed;

    // Calculate world-space formation point
    const FVector TargetForward = FollowTarget->GetActorForwardVector();
    const FVector TargetRight = FollowTarget->GetActorRightVector();
    const FVector TargetLocation = FollowTarget->GetActorLocation();

    FVector IdealPoint = TargetLocation
        + TargetForward * FormationOffset.X
        + TargetRight * FormationOffset.Y
        + FVector::UpVector * FormationOffset.Z;

    // Project onto nav mesh to ensure reachability
    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (IsValid(NavSys))
    {
        FNavLocation NavResult;
        if (NavSys->ProjectPointToNavigation(IdealPoint, NavResult, FVector(200.f, 200.f, 200.f)))
        {
            IdealPoint = NavResult.Location;
        }
        // If projection fails, use the ideal point anyway — MoveTo will handle pathfinding
    }

    BB->SetValueAsVector(BB_FollowLocation.SelectedKeyName, IdealPoint);
    return EBTNodeResult::Succeeded;
}
```

## BT Task: PerformRevive

Uses `NodeMemory` for per-instance state. BT nodes are shared across all AI instances — member variables would corrupt across multiple companions.

```cpp
// Header
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_PerformRevive.generated.h"

struct FReviveTaskMemory
{
    float ElapsedTime = 0.f;
};

UCLASS()
class MYPROJECT_API UBTTask_PerformRevive : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_PerformRevive();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
    virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual uint16 GetInstanceMemorySize() const override { return sizeof(FReviveTaskMemory); }

protected:
    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector BB_ReviveTarget;

    UPROPERTY(EditAnywhere, Category = "Config")
    float ReviveDuration = 3.f;
};

// Source
#include "BTTask_PerformRevive.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "HealthComponent.h"

UBTTask_PerformRevive::UBTTask_PerformRevive()
{
    NodeName = TEXT("Perform Revive");
    bNotifyTick = true;
    bNotifyTaskFinished = true;
}

EBTNodeResult::Type UBTTask_PerformRevive::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!IsValid(BB)) return EBTNodeResult::Failed;

    AActor* Target = Cast<AActor>(BB->GetValueAsObject(BB_ReviveTarget.SelectedKeyName));
    if (!IsValid(Target)) return EBTNodeResult::Failed;

    auto* HealthComp = Target->FindComponentByClass<UHealthComponent>();
    if (!IsValid(HealthComp) || !HealthComp->IsDBNO())
    {
        return EBTNodeResult::Failed;
    }

    // Initialize per-instance memory
    FReviveTaskMemory* Memory = CastInstanceNodeMemory<FReviveTaskMemory>(NodeMemory);
    Memory->ElapsedTime = 0.f;

    // Play revive animation / montage on the companion pawn
    return EBTNodeResult::InProgress;
}

void UBTTask_PerformRevive::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    FReviveTaskMemory* Memory = CastInstanceNodeMemory<FReviveTaskMemory>(NodeMemory);
    Memory->ElapsedTime += DeltaSeconds;

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!IsValid(BB))
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    // Check target still valid and still DBNO
    AActor* Target = Cast<AActor>(BB->GetValueAsObject(BB_ReviveTarget.SelectedKeyName));
    if (!IsValid(Target))
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    auto* HealthComp = Target->FindComponentByClass<UHealthComponent>();
    if (!IsValid(HealthComp) || !HealthComp->IsDBNO())
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    if (Memory->ElapsedTime >= ReviveDuration)
    {
        HealthComp->Revive();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
    }
}

EBTNodeResult::Type UBTTask_PerformRevive::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    FReviveTaskMemory* Memory = CastInstanceNodeMemory<FReviveTaskMemory>(NodeMemory);
    Memory->ElapsedTime = 0.f;
    // Cancel revive animation
    return EBTNodeResult::Aborted;
}
```

## BT Task: FireWeapon (Latent — with Fire Rate)

Latent task that fires for a configurable duration before returning, allowing the BT to re-evaluate cover and target. Instant-return fire tasks cause the BT to re-run the full combat sequence every frame.

```cpp
// Header
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FireWeapon.generated.h"

struct FFireWeaponMemory
{
    float ElapsedTime = 0.f;
    float TimeSinceLastShot = 0.f;
};

UCLASS()
class MYPROJECT_API UBTTask_FireWeapon : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_FireWeapon();

    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
    virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual uint16 GetInstanceMemorySize() const override { return sizeof(FFireWeaponMemory); }

protected:
    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector BB_CombatTarget;

    UPROPERTY(EditAnywhere, Category = "Config")
    float BurstDuration = 3.f; // How long to fire before re-evaluating

    UPROPERTY(EditAnywhere, Category = "Config")
    float TimeBetweenShots = 0.2f;
};

// Source
#include "BTTask_FireWeapon.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "WeaponComponent.h"

UBTTask_FireWeapon::UBTTask_FireWeapon()
{
    NodeName = TEXT("Fire Weapon");
    bNotifyTick = true;
    bNotifyTaskFinished = true;
}

EBTNodeResult::Type UBTTask_FireWeapon::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    AAIController* Controller = OwnerComp.GetAIOwner();
    if (!IsValid(Controller)) return EBTNodeResult::Failed;

    APawn* Pawn = Controller->GetPawn();
    if (!IsValid(Pawn)) return EBTNodeResult::Failed;

    auto* WeaponComp = Pawn->FindComponentByClass<UWeaponComponent>();
    if (!IsValid(WeaponComp)) return EBTNodeResult::Failed;

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!IsValid(BB)) return EBTNodeResult::Failed;

    AActor* Target = Cast<AActor>(BB->GetValueAsObject(BB_CombatTarget.SelectedKeyName));
    if (!IsValid(Target)) return EBTNodeResult::Failed;

    if (WeaponComp->NeedsReload())
    {
        WeaponComp->StartReload();
        return EBTNodeResult::Failed; // BT will retry after reload
    }

    FFireWeaponMemory* Memory = CastInstanceNodeMemory<FFireWeaponMemory>(NodeMemory);
    Memory->ElapsedTime = 0.f;
    Memory->TimeSinceLastShot = TimeBetweenShots; // Fire immediately on first tick

    return EBTNodeResult::InProgress;
}

void UBTTask_FireWeapon::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    FFireWeaponMemory* Memory = CastInstanceNodeMemory<FFireWeaponMemory>(NodeMemory);
    Memory->ElapsedTime += DeltaSeconds;
    Memory->TimeSinceLastShot += DeltaSeconds;

    // Burst duration elapsed — return so BT can re-evaluate cover
    if (Memory->ElapsedTime >= BurstDuration)
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return;
    }

    AAIController* Controller = OwnerComp.GetAIOwner();
    if (!IsValid(Controller))
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    APawn* Pawn = Controller->GetPawn();
    if (!IsValid(Pawn))
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    auto* WeaponComp = Pawn->FindComponentByClass<UWeaponComponent>();
    if (!IsValid(WeaponComp))
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    if (WeaponComp->NeedsReload())
    {
        WeaponComp->StartReload();
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!IsValid(BB))
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    AActor* Target = Cast<AActor>(BB->GetValueAsObject(BB_CombatTarget.SelectedKeyName));
    if (!IsValid(Target))
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    // Fire at configured rate
    if (Memory->TimeSinceLastShot >= TimeBetweenShots)
    {
        WeaponComp->AIFireAtTarget(Target);
        Memory->TimeSinceLastShot = 0.f;
    }
}

EBTNodeResult::Type UBTTask_FireWeapon::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    // Stop firing — weapon component handles cleanup
    return EBTNodeResult::Aborted;
}
```

## Companion AI Controller — Perception Handler

```cpp
void ACompanionAIController::OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors)
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!IsValid(BB)) return;

    AActor* BestTarget = nullptr;
    float BestScore = 0.f;

    APawn* ControlledPawn = GetPawn();
    if (!IsValid(ControlledPawn)) return;

    UAIPerceptionComponent* PerceptionComp = GetPerceptionComponent();
    if (!IsValid(PerceptionComp)) return;

    for (AActor* Actor : UpdatedActors)
    {
        if (!IsValid(Actor)) continue;

        FActorPerceptionBlueprintInfo Info;
        PerceptionComp->GetActorsPerception(Actor, Info);

        // Only target enemies
        if (!Info.Target.IsValid()) continue;

        // Check faction via IGenericTeamAgentInterface — works for both AI and player controllers
        const IGenericTeamAgentInterface* OtherTeamAgent = nullptr;

        if (const APawn* OtherPawn = Cast<APawn>(Actor))
        {
            if (AController* OtherController = OtherPawn->GetController())
            {
                OtherTeamAgent = Cast<IGenericTeamAgentInterface>(OtherController);
            }
        }

        if (!OtherTeamAgent)
        {
            // Actor has no team interface — skip (could be a prop, door, etc.)
            continue;
        }

        if (GetGenericTeamId() == OtherTeamAgent->GetGenericTeamId())
        {
            continue; // Same team — don't target friendlies
        }

        // Check if stimulus is active (currently perceived)
        bool bCurrentlyPerceived = false;
        for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
        {
            if (Stimulus.WasSuccessfullySensed())
            {
                bCurrentlyPerceived = true;
                break;
            }
        }

        if (!bCurrentlyPerceived) continue;

        // Score: prefer closest enemy
        const float DistSq = FVector::DistSquared(ControlledPawn->GetActorLocation(), Actor->GetActorLocation());
        const float Score = 1.f / FMath::Max(DistSq, 1.f);

        if (Score > BestScore)
        {
            BestScore = Score;
            BestTarget = Actor;
        }
    }

    BB->SetValueAsObject(TEXT("BB_CombatTarget"), BestTarget);
}
```
